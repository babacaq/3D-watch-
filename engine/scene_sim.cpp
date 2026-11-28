// Симуляции сцены: рэгдолл по костям, инерция предметов на костях (оружие, сумка), ткань и пружины вершин.
// Всё считается заранее (bake) для кадров проекта и затем только проигрывается, поэтому перемотка таймлайна мгновенна
// и результат детерминирован. OpenGL здесь не используется.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>

#include "log.h"
#include "physics.h"
#include "scene.h"

namespace eng {

namespace {

Quat rotFromTo(Vec3 a, Vec3 b) {
    a = normalize(a);
    b = normalize(b);
    float d = dot(a, b);
    if (d > 0.999999f) return Quat();
    if (d < -0.999999f) {
        Vec3 ax = std::fabs(a.x) < 0.9f ? Vec3(1, 0, 0) : Vec3(0, 0, 1);
        return quatAxisAngle(cross(a, ax), kPi);
    }
    Vec3 c = cross(a, b);
    return normalize(Quat(c.x, c.y, c.z, 1.f + d));
}

inline Vec3 rotateV(const Quat& q, const Vec3& v) {
    Vec3 u(q.x, q.y, q.z);
    Vec3 t = cross(u, v) * 2.f;
    return v + t * q.w + cross(u, t);
}

inline bool fin3(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// ---- сварка вершин меша по положению (швы UV не должны рвать ткань) ----
struct Weld {
    std::vector<int> vmap;  // вершина -> частица
    std::vector<Vec3> rest;  // положение частицы в пространстве узла меша
    std::vector<std::pair<int, int>> edges, bends;
    int count() const { return (int)rest.size(); }
};

void buildWeld(const MeshData& md, Weld& w) {
    struct K {
        int x, y, z;
        bool operator==(const K& o) const { return x == o.x && y == o.y && z == o.z; }
    };
    struct KH {
        size_t operator()(const K& k) const { return (size_t)((uint32_t)k.x * 73856093u) ^ (size_t)((uint32_t)k.y * 19349663u) ^ (size_t)((uint32_t)k.z * 83492791u); }
    };
    std::unordered_map<K, int, KH> map;
    w.vmap.assign(md.verts.size(), 0);
    w.rest.clear();
    for (size_t i = 0; i < md.verts.size(); i++) {
        const Vertex& v = md.verts[i];
        K k{(int)std::lround(v.p[0] * 2000.f), (int)std::lround(v.p[1] * 2000.f), (int)std::lround(v.p[2] * 2000.f)};
        auto it = map.find(k);
        if (it == map.end()) {
            it = map.emplace(k, (int)w.rest.size()).first;
            w.rest.push_back({v.p[0], v.p[1], v.p[2]});
        }
        w.vmap[i] = it->second;
    }
    std::vector<uint64_t> es;
    std::unordered_map<uint64_t, std::vector<int>> opp;
    auto key = [](int a, int b) { if (a > b) std::swap(a, b); return ((uint64_t)(uint32_t)a << 32) | (uint32_t)b; };
    for (size_t t = 0; t + 2 < md.indices.size(); t += 3) {
        int p[3];
        bool ok = true;
        for (int k = 0; k < 3; k++) {
            uint32_t vi = md.indices[t + k];
            if (vi >= md.verts.size()) { ok = false; break; }
            p[k] = w.vmap[vi];
        }
        if (!ok || p[0] == p[1] || p[1] == p[2] || p[0] == p[2]) continue;
        for (int k = 0; k < 3; k++) {
            int a = p[k], b = p[(k + 1) % 3], c = p[(k + 2) % 3];
            uint64_t kk = key(a, b);
            es.push_back(kk);
            auto& v = opp[kk];
            if (std::find(v.begin(), v.end(), c) == v.end()) v.push_back(c);
        }
    }
    std::sort(es.begin(), es.end());
    es.erase(std::unique(es.begin(), es.end()), es.end());
    w.edges.clear();
    w.bends.clear();
    for (uint64_t k : es) w.edges.push_back({(int)(k >> 32), (int)(k & 0xffffffffu)});
    for (auto& kv : opp)
        if (kv.second.size() == 2) {
            int a = kv.second[0], b = kv.second[1];
            if (a != b) w.bends.push_back({std::min(a, b), std::max(a, b)});
        }
    std::sort(w.bends.begin(), w.bends.end());
    w.bends.erase(std::unique(w.bends.begin(), w.bends.end()), w.bends.end());
}

Vec3 skinPoint(const Vertex& v, const std::vector<Mat4>& jm) {
    Vec3 p(v.p[0], v.p[1], v.p[2]), acc;
    float ws = 0.f;
    for (int k = 0; k < 4; k++) {
        if (v.w[k] <= 0.f || v.j[k] >= jm.size()) continue;
        acc = acc + transformPoint(jm[v.j[k]], p) * v.w[k];
        ws += v.w[k];
    }
    return ws > 1e-6f ? acc * (1.f / ws) : p;
}

constexpr int kMaxClothParticles = 6000;
constexpr size_t kMaxClothBytes = 96u * 1024u * 1024u;

}  // namespace

// ============================================================ ткань: поддержка
std::vector<int> Scene::clothCandidates(const Object& o) const {
    std::vector<int> out;
    if (o.type != ObjType::Model || o.model < 0 || o.model >= (int)models.size() || !models[o.model].model) return out;
    const Model& m = *models[o.model].model;
    for (size_t mi = 0; mi < m.meshes.size(); mi++) {
        const MeshData& md = m.meshes[mi];
        if (md.verts.empty() || md.indices.size() < 3) continue;
        if (md.skin >= (int)m.skins.size()) continue;
        out.push_back((int)mi);
    }
    return out;
}

bool Scene::clothMeshOn(const Object& o, int mesh) const {
    if (o.type != ObjType::Model || o.model < 0 || o.model >= (int)models.size() || !models[o.model].model) return false;
    const Model& m = *models[o.model].model;
    if (mesh < 0 || mesh >= (int)m.meshes.size() || m.meshes[mesh].verts.empty()) return false;
    if (o.cloth.meshes.empty()) return m.meshes[mesh].skin < 0;  // по умолчанию: все части без скелета
    return std::find(o.cloth.meshes.begin(), o.cloth.meshes.end(), mesh) != o.cloth.meshes.end();
}

bool Scene::clothSupported(const Object& o, std::string* why) const {
    if (o.type != ObjType::Model || o.model < 0 || o.model >= (int)models.size() || !models[o.model].model) {
        if (why) *why = "Ткань работает только для моделей";
        return false;
    }
    const Model& m = *models[o.model].model;
    int total = 0, meshes = 0;
    for (size_t mi = 0; mi < m.meshes.size(); mi++) {
        if (!clothMeshOn(o, (int)mi)) continue;
        Weld w;
        buildWeld(m.meshes[mi], w);
        total += w.count();
        meshes++;
    }
    if (meshes == 0) {
        if (why) *why = o.cloth.meshes.empty() ? "Выберите части модели, которые должны быть тканью (кнопка «Выбрать части»)" : "Выбранные части не найдены в модели";
        return false;
    }
    if (total > kMaxClothParticles) {
        if (why) *why = "Слишком много вершин для ткани (" + std::to_string(total) + ", максимум " + std::to_string(kMaxClothParticles) + "). Выберите меньше частей";
        return false;
    }
    return true;
}

void Scene::releaseGL() {
    for (auto& o : objects) {
        if (!o->clothRt) continue;
        for (auto& m : o->clothRt->meshes) m.release();
        o->clothRt.reset();
    }
}

// ============================================================ рэгдолл
void Scene::bakeRagdolls() {
    const int fps = std::max(1, proj.fps);
    for (auto& op : objects) {
        Object& o = *op;
        if (o.type != ObjType::Model || !o.rag.enabled || o.model < 0 || o.model >= (int)models.size() || !models[o.model].model) continue;
        const Model& m = *models[o.model].model;
        std::vector<int> bones = boneNodes(o);
        if (bones.size() < 2) continue;
        const int nb = (int)bones.size();
        std::vector<int> idxOf(m.nodes.size(), -1);
        for (int i = 0; i < nb; i++) idxOf[bones[i]] = i;
        std::vector<int> par(nb, -1);
        std::vector<std::vector<int>> kids(nb);
        for (int i = 0; i < nb; i++) {
            int p = boneParent(o, bones[i]);
            if (p >= 0 && idxOf[p] >= 0) { par[i] = idxOf[p]; kids[idxOf[p]].push_back(i); }
        }
        // размер поддерева: основную цепочку (позвоночник) выбираем по самой «тяжёлой» ветке
        std::vector<int> sub(nb, 1);
        for (int pass = 0; pass < nb; pass++) {
            bool ch = false;
            for (int i = 0; i < nb; i++) {
                int s = 1;
                for (int c : kids[i]) s += sub[c];
                if (s != sub[i]) { sub[i] = s; ch = true; }
            }
            if (!ch) break;
        }

        const int S = std::max(0, o.rag.start);
        int last = std::max(proj.frameEnd, S);
        last = std::min(last, S + 6000);

        // исходные позы: кадр S-1 (скорости) и кадр S
        evaluate((float)(S - 1));
        std::vector<Vec3> Jprev(nb);
        for (int i = 0; i < nb; i++) boneWorldPos(o, bones[i], Jprev[i]);
        evaluate((float)S);
        std::vector<Mat4> W0(nb);
        std::vector<Vec3> J(nb);
        for (int i = 0; i < nb; i++) {
            if (!o.anim || bones[i] >= (int)o.anim->world.size()) { W0[i] = Mat4(); J[i] = Vec3(); continue; }
            W0[i] = o.world * o.fit * o.anim->world[bones[i]];
            J[i] = transformPoint(W0[i], {0, 0, 0});
        }

        // тела: по одному на кость, вдоль отрезка сустав - следующий сустав
        struct Seg { Vec3 c; Quat q; float L = 0.05f, r = 0.03f; int body = -1; Vec3 dir{0, 1, 0}; };
        std::vector<Seg> seg(nb);
        float sumL = 0;
        for (int i = 0; i < nb; i++) {
            int main = -1;
            for (int c : kids[i]) if (main < 0 || sub[c] > sub[main]) main = c;
            Vec3 dir;
            float L;
            if (main >= 0) {
                dir = J[main] - J[i];
                L = length(dir);
            } else if (par[i] >= 0) {
                dir = J[i] - J[par[i]];
                L = 0.5f * length(dir);
            } else { dir = Vec3(0, 1, 0); L = 0.1f; }
            if (length(dir) < 1e-5f) dir = (par[i] >= 0 && length(J[i] - J[par[i]]) > 1e-5f) ? J[i] - J[par[i]] : Vec3(0, 1, 0);
            dir = normalize(dir);
            L = std::max(L, 0.06f);
            seg[i].dir = dir;
            seg[i].L = L;
            seg[i].c = J[i] + dir * (L * 0.5f);
            seg[i].q = rotFromTo(Vec3(0, 1, 0), dir);
            seg[i].r = clampf(L * 0.2f * o.rag.thick, 0.025f, 0.14f * std::max(0.3f, o.rag.thick));
            sumL += L;
        }

        PhysicsWorld w;
        w.setGravity({0, -9.81f, 0});
        w.setGroundY(0.f);
        w.setGroundEnabled(render.floor);
        const float limit = clampf(o.rag.limit, 10.f, 170.f) * kPi / 180.f;
        for (int i = 0; i < nb; i++) {
            BodyDesc d;
            d.shape = ShapeType::Box;
            d.half = {seg[i].r, seg[i].L * 0.5f, seg[i].r};
            d.pos = seg[i].c;
            d.rot = seg[i].q;
            d.mass = std::max(0.2f, o.rag.mass * seg[i].L / std::max(1e-3f, sumL));
            d.friction = clampf(o.rag.friction, 0.f, 2.f);
            d.restitution = clampf(o.rag.bounce, 0.f, 0.9f);
            Vec3 v = o.rag.push;
            if (o.rag.inherit) {
                Vec3 dv = (J[i] - Jprev[i]) * (float)fps;
                float sp = length(dv);
                if (sp > 30.f) dv = dv * (30.f / sp);
                if (fin3(dv)) v = v + dv;
            }
            d.vel = v;
            seg[i].body = w.addBody(d);
        }
        for (int i = 0; i < nb; i++) {
            if (seg[i].body < 0) continue;
            if (par[i] >= 0 && seg[par[i]].body >= 0) {
                w.addBallJoint(seg[i].body, seg[par[i]].body, J[i], limit);
                w.ignorePair(seg[i].body, seg[par[i]].body);
                if (par[par[i]] >= 0 && seg[par[par[i]]].body >= 0) w.ignorePair(seg[i].body, seg[par[par[i]]].body);
            }
            if (par[i] >= 0)
                for (int sib : kids[par[i]])
                    if (sib != i && seg[sib].body >= 0) w.ignorePair(seg[i].body, seg[sib].body);
        }
        std::vector<Mat4> Tinv(nb);
        for (int i = 0; i < nb; i++) Tinv[i] = inverse(translate(seg[i].c) * fromQuat(seg[i].q));

        // препятствия: неподвижные предметы сцены
        std::vector<Object*> col;
        for (auto& oq : objects)
            if (oq.get() != &o && oq->type == ObjType::Model && oq->phys.enabled && oq->phys.kind == 1 && oq->model >= 0 && oq->model < (int)models.size() && models[oq->model].model)
                col.push_back(oq.get());

        RagBake rb;
        rb.first = S;
        rb.last = last;
        rb.has.assign(m.nodes.size(), 0);
        for (int i = 0; i < nb; i++) rb.has[bones[i]] = 1;
        int subSteps = std::max(1, (int)std::lround(120.0 / fps));
        float dtSub = 1.f / (float)fps / (float)subSteps;
        for (int f = S; f <= last; f++) {
            evaluate((float)f);
            w.clearStaticBoxes();
            for (Object* c : col) {
                Vec3 cc, half, lc;
                Quat q;
                if (boxOfObject(*c, cc, q, half, lc)) w.addStaticBox(cc, q, half);
            }
            Mat4 Minv = inverse(o.world * o.fit);
            std::vector<Mat4> mats(m.nodes.size());
            for (int i = 0; i < nb; i++) {
                if (seg[i].body < 0) continue;
                const Body& b = w.bodies()[(size_t)seg[i].body];
                Mat4 Wn = translate(b.pos) * fromQuat(b.rot) * Tinv[i] * W0[i];
                mats[bones[i]] = Minv * Wn;
            }
            rb.mats.push_back(std::move(mats));
            for (int k = 0; k < subSteps; k++) w.step(dtSub);
        }
        rag_[o.id] = std::move(rb);
    }
}

// ============================================================ инерция предметов на костях
void Scene::bakeSecondary() {
    const int a = proj.frameStart;
    const int b = std::min(proj.frameEnd, a + 6000);
    const int N = b - a + 1;
    const int fps = std::max(1, proj.fps);
    for (auto& op : objects) {
        Object& o = *op;
        if (o.type != ObjType::Model || !o.sec.enabled || o.model < 0 || o.model >= (int)models.size() || !models[o.model].model || o.parents.empty()) continue;
        const Model& m = *models[o.model].model;
        Vec3 center = m.center();
        std::vector<Vec3> P((size_t)N), U((size_t)N);
        std::vector<char> att((size_t)N, 0);
        float Lsum = 0;
        int Lcount = 0;
        for (int i = 0; i < N; i++) {
            evaluate((float)(a + i));
            att[i] = (activeParent(o, (float)(a + i)) != nullptr) ? 1 : 0;
            Vec3 pv = transformPoint(o.world, {0, 0, 0});
            Vec3 cw = transformPoint(o.world * o.fit, center);
            Vec3 d = cw - pv;
            float len = length(d);
            P[i] = pv;
            U[i] = len > 0.03f ? d * (1.f / len) : normalize(transformDir(o.world, {0, 1, 0}));
            if (att[i] && len > 0.03f) { Lsum += len; Lcount++; }
        }
        float L = Lcount ? Lsum / Lcount : 0.3f;
        L = std::max(L, 0.12f);
        SecBake sb;
        sb.first = a;
        sb.q.assign((size_t)N, Quat());
        const SecondaryProps& sp = o.sec;
        const float k = clampf(sp.stiffness, 1.f, 400.f), damp = clampf(sp.damping, 0.f, 60.f);
        const float sag = clampf(sp.gravity, 0.f, 3.f) * 0.08f * L;
        int subSteps = std::max(2, (int)std::lround(240.0 / fps));
        float dt = 1.f / (float)fps / (float)subSteps;
        Vec3 c, v;
        for (int i = 0; i < N; i++) {
            if (!att[i]) { c = P[i] + U[i] * L; v = Vec3(); continue; }
            if (i == 0 || !att[i - 1]) { c = P[i] + U[i] * L - Vec3(0, sag, 0); v = Vec3(); }
            Vec3 dirNow = normalize(c - P[i]);
            Quat q = rotFromTo(U[i], dirNow);
            if (sp.amount != 1.f) q = normalize(slerp(Quat(), q, sp.amount));
            sb.q[i] = q;
            if (i + 1 < N && att[i + 1]) {
                for (int s = 1; s <= subSteps; s++) {
                    float al = (float)s / subSteps;
                    Vec3 Pa = lerp(P[i], P[i + 1], al);
                    Vec3 Ua = normalize(lerp(U[i], U[i + 1], al));
                    Vec3 target = Pa + Ua * L - Vec3(0, sag, 0);
                    Vec3 acc = (target - c) * k - v * damp;
                    v = v + acc * dt;
                    c = c + v * dt;
                    Vec3 r = c - Pa;
                    float rl = length(r);
                    if (rl > 1e-6f) c = Pa + r * (L / rl);   // рычаг не растягивается
                    if (!fin3(c)) { c = Pa + Ua * L; v = Vec3(); }
                }
            }
        }
        sec_[o.id] = std::move(sb);
    }
}

// ============================================================ ткань и пружины вершин
void Scene::bakeCloth() {
    const int a = proj.frameStart;
    const int fps = std::max(1, proj.fps);
    for (auto& op : objects) {
        Object& o = *op;
        std::string why;
        if (o.type != ObjType::Model || !o.cloth.enabled || !clothSupported(o, &why)) continue;
        const Model& m = *models[o.model].model;
        const ClothProps& cp = o.cloth;

        struct Sim {
            int mesh = 0, node = 0, skin = -1;
            std::vector<int> rep;  // частица -> одна из вершин (для скелетной ткани)
            Weld w;
            std::vector<char> pinned;
            std::vector<Vec3> x, xp, goalCur, goalNext;
            Mat4 Mcur, Mnext;
            std::vector<float> restLen, bendLen;
        };
        std::vector<Sim> sims;
        int totalP = 0;
        for (int ni : m.order)
            for (int mi : m.nodes[ni].meshes) {
                const MeshData& md = m.meshes[mi];
                if (!clothMeshOn(o, mi)) continue;
                Sim s;
                s.mesh = mi;
                s.node = ni;
                s.skin = md.skin;
                buildWeld(md, s.w);
                s.rep.assign((size_t)s.w.count(), -1);
                for (size_t vi = 0; vi < md.verts.size(); vi++) if (s.rep[(size_t)s.w.vmap[vi]] < 0) s.rep[(size_t)s.w.vmap[vi]] = (int)vi;
                totalP += s.w.count();
                sims.push_back(std::move(s));
            }
        if (sims.empty()) continue;
        int N = std::min(proj.frameEnd - a + 1, 6000);
        size_t maxFrames = kMaxClothBytes / (sizeof(Vec3) * (size_t)std::max(1, totalP));
        if ((size_t)N > maxFrames) N = (int)std::max<size_t>(2, maxFrames);

        ClothBake cb;
        cb.first = a;
        cb.last = a + N - 1;
        auto meshMat = [&](const Sim& s) {
            if (s.skin >= 0) return o.world * o.fit;  // скелетный меш: вершины считаются в пространстве модели
            return o.world * o.fit * (o.anim && s.node < (int)o.anim->world.size() ? o.anim->world[s.node] : Mat4());
        };
        auto goalOf = [&](const Sim& s, const Mat4& M, int p) {
            if (s.skin >= 0 && o.anim && s.skin < (int)o.anim->jointMat.size()) {
                const Vertex& v = m.meshes[(size_t)s.mesh].verts[(size_t)s.rep[(size_t)p]];
                return transformPoint(M, skinPoint(v, o.anim->jointMat[(size_t)s.skin]));
            }
            return transformPoint(M, s.w.rest[(size_t)p]);
        };

        evaluate((float)a);
        for (Sim& s : sims) {
            s.Mcur = meshMat(s);
            int P = s.w.count();
            s.goalCur.resize((size_t)P);
            for (int i = 0; i < P; i++) s.goalCur[i] = goalOf(s, s.Mcur, i);
            s.x = s.goalCur;
            s.xp = s.goalCur;
            s.goalNext = s.goalCur;
            s.Mnext = s.Mcur;
            // закреплённая часть: по высоте в мире на первом кадре
            float ymin = 1e30f, ymax = -1e30f;
            for (const Vec3& g : s.goalCur) { ymin = std::min(ymin, g.y); ymax = std::max(ymax, g.y); }
            float span = std::max(1e-4f, ymax - ymin);
            s.pinned.assign((size_t)P, 0);
            if (cp.pin > 0.f) {
                int cnt = 0;
                for (int i = 0; i < P; i++) {
                    bool pin = cp.pinSide == 0 ? s.goalCur[i].y >= ymax - cp.pin * span - 1e-5f : s.goalCur[i].y <= ymin + cp.pin * span + 1e-5f;
                    s.pinned[i] = pin ? 1 : 0;
                    cnt += pin ? 1 : 0;
                }
                if (cnt == 0) for (int i = 0; i < P; i++) if (std::fabs(s.goalCur[i].y - (cp.pinSide == 0 ? ymax : ymin)) < 1e-5f) s.pinned[i] = 1;
            }
        }
        std::vector<ClothMeshBake> mbs(sims.size());
        for (size_t si = 0; si < sims.size(); si++) {
            mbs[si].mesh = sims[si].mesh;
            mbs[si].particles = sims[si].w.count();
            mbs[si].vmap = sims[si].w.vmap;
            mbs[si].pos.reserve((size_t)N * (size_t)mbs[si].particles);
        }

        const int subSteps = std::max(2, (int)std::lround(120.0 / fps));
        const float dt = 1.f / (float)fps / (float)subSteps;
        const float damp = clampf(cp.damping, 0.f, 0.6f);
        const float stretch = 0.45f + 0.55f * clampf(cp.stiffness, 0.f, 1.f);
        const float bendK = 0.15f + 0.6f * clampf(cp.stiffness, 0.f, 1.f);
        const float shapeK = clampf(cp.shape, 0.f, 1.f) * 0.25f * dt * 120.f;
        const float wr = cp.windDir * kPi / 180.f;
        const Vec3 windDir(std::cos(wr), 0.f, std::sin(wr));
        const int iters = totalP > 2500 ? 3 : 5;

        for (int i = 0; i < N; i++) {
            const int f = a + i;
            // запись кадра: положения частиц в пространстве узла меша
            for (size_t si = 0; si < sims.size(); si++) {
                Sim& s = sims[si];
                Mat4 inv = inverse(s.Mcur);
                for (int p = 0; p < s.w.count(); p++) mbs[si].pos.push_back(transformPoint(inv, s.x[p]));
            }
            if (i + 1 >= N) break;
            evaluate((float)(f + 1));
            for (Sim& s : sims) {
                s.Mnext = meshMat(s);
                for (int p = 0; p < s.w.count(); p++) s.goalNext[p] = goalOf(s, s.Mnext, p);
            }
            for (int sub = 1; sub <= subSteps; sub++) {
                float al = (float)sub / subSteps;
                float time = ((float)f + al) / (float)fps;
                for (Sim& s : sims) {
                    const int P = s.w.count();
                    // интегрирование Верле
                    for (int p = 0; p < P; p++) {
                        Vec3 goal = lerp(s.goalCur[p], s.goalNext[p], al);
                        if (s.pinned[p]) { s.xp[p] = s.x[p]; s.x[p] = goal; continue; }
                        Vec3 acc(0, -9.81f * cp.gravity, 0);
                        if (cp.wind != 0.f) {
                            float ph = std::sin(time * 1.9f + s.x[p].x * 0.9f + s.x[p].z * 0.7f) * 0.5f + 0.5f;
                            acc = acc + windDir * (cp.wind * (0.55f + 0.9f * ph) * 2.2f);
                        }
                        Vec3 vel = (s.x[p] - s.xp[p]) * (1.f - damp);
                        s.xp[p] = s.x[p];
                        s.x[p] = s.x[p] + vel + acc * (dt * dt);
                        s.x[p] = s.x[p] + (goal - s.x[p]) * shapeK;  // пружина к исходной форме
                    }
                    // связи: длины берутся из «гладкой» формы на текущем кадре, поэтому масштаб и вращение объекта не рвут ткань
                    auto solve = [&](int ea, int eb, float k) {
                        Vec3 ga = lerp(s.goalCur[ea], s.goalNext[ea], al), gb = lerp(s.goalCur[eb], s.goalNext[eb], al);
                        float rl = length(ga - gb);
                        Vec3 d = s.x[eb] - s.x[ea];
                        float len = length(d);
                        if (len < 1e-7f || rl < 1e-7f) return;
                        float wa = s.pinned[ea] ? 0.f : 1.f, wb = s.pinned[eb] ? 0.f : 1.f;
                        float ws = wa + wb;
                        if (ws <= 0.f) return;
                        Vec3 corr = d * ((len - rl) / len * k / ws);
                        s.x[ea] = s.x[ea] + corr * wa;
                        s.x[eb] = s.x[eb] - corr * wb;
                    };
                    for (int it = 0; it < iters; it++) {
                        for (auto& e : s.w.edges) solve(e.first, e.second, stretch);
                        for (auto& e : s.w.bends) solve(e.first, e.second, bendK);
                        for (int p = 0; p < P; p++)
                            if (s.pinned[p]) s.x[p] = lerp(s.goalCur[p], s.goalNext[p], al);
                    }
                    // пол
                    if (render.floor)
                        for (int p = 0; p < P; p++) {
                            if (s.x[p].y < 0.006f) {
                                s.x[p].y = 0.006f;
                                s.xp[p].x = s.x[p].x + (s.xp[p].x - s.x[p].x) * 0.6f;
                                s.xp[p].z = s.x[p].z + (s.xp[p].z - s.x[p].z) * 0.6f;
                            }
                            if (!fin3(s.x[p])) { s.x[p] = lerp(s.goalCur[p], s.goalNext[p], al); s.xp[p] = s.x[p]; }
                        }
                }
            }
            for (Sim& s : sims) { s.goalCur = s.goalNext; s.Mcur = s.Mnext; }
        }
        // если кадров вышло меньше (лимит памяти), последний кадр держится до конца
        for (size_t si = 0; si < sims.size(); si++) mbs[si].pos.shrink_to_fit();
        cb.meshes = std::move(mbs);
        cb.last = a + (int)(cb.meshes.empty() || cb.meshes[0].particles == 0 ? 0 : cb.meshes[0].pos.size() / (size_t)cb.meshes[0].particles) - 1;
        cloth_[o.id] = std::move(cb);
    }
}

void Scene::applyCloth(Object& o, float f) {
    auto it = cloth_.find(o.id);
    if (it == cloth_.end() || it->second.meshes.empty() || o.model < 0 || o.model >= (int)models.size() || !models[o.model].model) return;
    const Model& m = *models[o.model].model;
    const ClothBake& cb = it->second;
    if (!o.clothRt) o.clothRt.reset(new ClothRt());
    ClothRt& rt = *o.clothRt;
    if (rt.meshes.size() != m.meshes.size()) {
        for (auto& mm : rt.meshes) mm.release();
        rt.meshes.clear();
        rt.meshes.resize(m.meshes.size());
    }
    // части, с которых ткань сняли, возвращаются к обычному мешу
    for (size_t mi = 0; mi < rt.meshes.size(); mi++) {
        if (rt.meshes[mi].verts.empty()) continue;
        bool still = false;
        for (const ClothMeshBake& mb : cb.meshes) if ((size_t)mb.mesh == mi) still = true;
        if (!still) { rt.meshes[mi].release(); rt.meshes[mi] = MeshData(); }
    }
    int frame = std::max(cb.first, std::min((int)std::lround(f), cb.last)) - cb.first;
    for (const ClothMeshBake& mb : cb.meshes) {
        if (mb.mesh < 0 || mb.mesh >= (int)m.meshes.size()) continue;
        MeshData& dst = rt.meshes[(size_t)mb.mesh];
        const MeshData& src = m.meshes[(size_t)mb.mesh];
        if (dst.verts.size() != src.verts.size()) {
            dst.release();
            dst = src;
            dst.vao = dst.vbo = dst.ibo = 0;  // копия без GL-объектов: их заведёт рендерер
            dst.dynamic = true;
            if (src.skin >= 0) { dst.skin = -1; dst.modelSpace = true; }  // скелетный меш теперь рисуется как есть, вершины уже в пространстве модели
        }
        size_t base = (size_t)frame * (size_t)mb.particles;
        if (base + (size_t)mb.particles > mb.pos.size()) continue;
        for (size_t v = 0; v < dst.verts.size(); v++) {
            const Vec3& p = mb.pos[base + (size_t)mb.vmap[v]];
            dst.verts[v].p[0] = p.x; dst.verts[v].p[1] = p.y; dst.verts[v].p[2] = p.z;
        }
        // нормали по сваренным частицам: шов UV не даёт разрыва освещения
        std::vector<Vec3> pn((size_t)mb.particles);
        for (size_t t = 0; t + 2 < dst.indices.size(); t += 3) {
            uint32_t i0 = dst.indices[t], i1 = dst.indices[t + 1], i2 = dst.indices[t + 2];
            if (i0 >= dst.verts.size() || i1 >= dst.verts.size() || i2 >= dst.verts.size()) continue;
            Vec3 p0(dst.verts[i0].p[0], dst.verts[i0].p[1], dst.verts[i0].p[2]);
            Vec3 p1(dst.verts[i1].p[0], dst.verts[i1].p[1], dst.verts[i1].p[2]);
            Vec3 p2(dst.verts[i2].p[0], dst.verts[i2].p[1], dst.verts[i2].p[2]);
            Vec3 n = cross(p1 - p0, p2 - p0);
            pn[(size_t)mb.vmap[i0]] += n;
            pn[(size_t)mb.vmap[i1]] += n;
            pn[(size_t)mb.vmap[i2]] += n;
        }
        for (size_t v = 0; v < dst.verts.size(); v++) {
            const Vec3& n = pn[(size_t)mb.vmap[v]];
            float l = length(n);
            if (l > 1e-12f) { dst.verts[v].n[0] = n.x / l; dst.verts[v].n[1] = n.y / l; dst.verts[v].n[2] = n.z / l; }
        }
        dst.stale = true;
    }
    rt.dirty = true;
}

}  // namespace eng
