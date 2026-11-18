#include "scene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "geometry.h"
#include "json.h"
#include "physics.h"

namespace eng {

// ============================================================ утилиты
Vec3 Scene::eulerFromMat(const Mat4& m) {
    // R = Rz(c) * Ry(b) * Rx(a)
    float r20 = m.at(2, 0), r21 = m.at(2, 1), r22 = m.at(2, 2), r10 = m.at(1, 0), r00 = m.at(0, 0);
    float b = -std::asin(clampf(r20, -1.f, 1.f));
    float a, c;
    if (std::fabs(r20) < 0.99999f) {
        a = std::atan2(r21, r22);
        c = std::atan2(r10, r00);
    } else {  // кардан
        a = std::atan2(-m.at(1, 2), m.at(1, 1));
        c = 0;
    }
    return {a, b, c};
}

static Mat4 rotationOnly(const Mat4& w, Vec3* scaleOut) {
    Vec3 c0{w.m[0], w.m[1], w.m[2]}, c1{w.m[4], w.m[5], w.m[6]}, c2{w.m[8], w.m[9], w.m[10]};
    Vec3 s{length(c0), length(c1), length(c2)};
    if (dot(cross(c0, c1), c2) < 0) s.x = -s.x;
    Mat4 r;
    Vec3 n0 = s.x != 0 ? c0 * (1.f / s.x) : Vec3(1, 0, 0), n1 = s.y != 0 ? c1 * (1.f / s.y) : Vec3(0, 1, 0),
         n2 = s.z != 0 ? c2 * (1.f / s.z) : Vec3(0, 0, 1);
    r.m[0] = n0.x; r.m[1] = n0.y; r.m[2] = n0.z;
    r.m[4] = n1.x; r.m[5] = n1.y; r.m[6] = n1.z;
    r.m[8] = n2.x; r.m[9] = n2.y; r.m[10] = n2.z;
    if (scaleOut) *scaleOut = s;
    return r;
}

static void decomposeTRS(const Mat4& w, Vec3& t, Vec3& r, Vec3& s) {
    t = {w.m[12], w.m[13], w.m[14]};
    Mat4 R = rotationOnly(w, &s);
    r = Scene::eulerFromMat(R);
}

static void aimObject(Object& o, Vec3 pos, Vec3 zAxis) {
    Vec3 z = normalize(zAxis), up = std::fabs(z.y) > 0.99f ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
    Vec3 x = normalize(cross(up, z)), y = cross(z, x);
    Mat4 R;
    R.m[0] = x.x; R.m[1] = x.y; R.m[2] = x.z;
    R.m[4] = y.x; R.m[5] = y.y; R.m[6] = y.z;
    R.m[8] = z.x; R.m[9] = z.y; R.m[10] = z.z;
    o.t = pos;
    o.r = Scene::eulerFromMat(R);
}

Object* Scene::find(int id) {
    for (auto& o : objects) if (o->id == id) return o.get();
    return nullptr;
}
const Object* Scene::find(int id) const {
    for (auto& o : objects) if (o->id == id) return o.get();
    return nullptr;
}
int Scene::indexOf(int id) const {
    for (size_t i = 0; i < objects.size(); i++) if (objects[i]->id == id) return (int)i;
    return -1;
}

std::string Scene::uniqueName(const std::string& base) const {
    auto exists = [&](const std::string& n) { for (auto& o : objects) if (o->name == n) return true; return false; };
    if (!exists(base)) return base;
    for (int i = 1; i < 10000; i++) {
        char b[16];
        std::snprintf(b, sizeof b, ".%03d", i);
        if (!exists(base + b)) return base + b;
    }
    return base;
}

// ============================================================ создание
Scene::Scene() { initDefault(); }

void Scene::clear() {
    objects.clear();
    models.clear();
    selected = -1;
    activeCamera = -1;
    nextId_ = 1;
    undo_.clear();
    redo_.clear();
    proj = ProjectSettings();
    render = RenderSettings();
    name = "Untitled";
    resetEffects();
    frame_ = 1.f;
    initDefault();
}

void Scene::initDefault() {
    int sun = addLight(LightKind::Sun);
    Object* s = find(sun);
    s->name = "Солнце";
    s->energy = 4.f;
    aimObject(*s, {4, 8, 4}, {0.45f, 0.8f, 0.4f});
    int cam = addCamera();
    Object* c = find(cam);
    Vec3 eye{3.4f, 1.9f, 5.2f}, target{0, 0.9f, 0};
    aimObject(*c, eye, eye - target);
    selected = -1;
    evaluate(1.f);
}

int Scene::addModel(std::unique_ptr<Model> m, const std::string& source, const std::string& nm) {
    if (!m) return -1;
    Object* o = new Object();
    o->id = nextId_++;
    o->type = ObjType::Model;
    o->name = uniqueName(nm.empty() ? "Модель" : nm);
    ModelEntry e;
    Vec3 size = m->bmax - m->bmin;
    float mx = std::max({size.x, size.y, size.z, 1e-4f});
    float sc = 2.0f / mx;
    Vec3 c = m->center();
    bool prim = source.rfind("prim:", 0) == 0;
    o->fit = prim ? Mat4() : trs(Vec3(-sc * c.x, -sc * m->bmin.y, -sc * c.z), Quat(), Vec3(sc, sc, sc));
    if (!m->clips.empty()) {
        ClipKey k;
        k.frame = proj.frameStart; k.clip = 0; k.loop = true;
        o->clipKeys.push_back(k);
    }
    e.model = std::move(m);
    e.source = source;
    models.push_back(std::move(e));
    o->model = (int)models.size() - 1;
    int nModel = 0;
    for (auto& ob : objects) if (ob->type == ObjType::Model) nModel++;
    o->t = {1.6f * nModel * (nModel % 2 ? 1 : -1) * 0.5f * ((nModel + 1) / 2), 0, 0};
    if (nModel == 0) o->t = {0, 0, 0};
    int id = o->id;
    objects.emplace_back(o);
    selected = id;
    evaluate(frame_);
    return id;
}

std::unique_ptr<Model> Scene::primitiveModel(const std::string& kindIn, std::string* titleOut) {
    std::string kind = kindIn.rfind("prim:", 0) == 0 ? kindIn.substr(5) : kindIn;
    auto m = std::make_unique<Model>();
    MeshData md;
    Color white(1, 1, 1, 1);
    std::string title = "Куб";
    if (kind == "cube") addBox(md, {0, 0, 0}, {0.5f, 0.5f, 0.5f}, white, -1);
    else if (kind == "sphere") { addSphere(md, {0, 0, 0}, {0.5f, 0.5f, 0.5f}, white, -1, 40, 24); title = "Сфера"; }
    else if (kind == "plane") { addBox(md, {0, 0, 0}, {1.5f, 0.01f, 1.5f}, white, -1); title = "Плоскость"; }
    else if (kind == "cylinder") { addCylinder(md, {0, -0.5f, 0}, {0, 0.5f, 0}, 0.4f, 0.4f, white, -1, 40, true); title = "Цилиндр"; }
    else { addCylinder(md, {0, -0.5f, 0}, {0, 0.5f, 0}, 0.45f, 0.0f, white, -1, 40, true); title = "Конус"; }
    md.skin = -1;
    md.roughness = 0.5f;
    md.metallic = 0.f;
    Node n;
    n.name = title;
    n.meshes.push_back(0);
    m->nodes.push_back(n);
    m->meshes.push_back(std::move(md));
    m->name = title;
    m->finalize();
    if (titleOut) *titleOut = title;
    return m;
}

int Scene::addPrimitive(const std::string& kind) {
    std::string title;
    auto m = primitiveModel(kind, &title);
    float lift = kind == "plane" ? 0.01f : 0.5f;
    int id = addModel(std::move(m), "prim:" + kind, title);
    Object* o = find(id);
    if (o) { o->t = {0, lift, 0}; o->matOverride = true; evaluate(frame_); }
    return id;
}

void Scene::setWorld(int id, const Mat4& W) {
    Object* o = find(id);
    if (!o) return;
    Mat4 L = W;
    if (const ParentKey* pk = activeParent(*o, frame_))
        if (const Object* p = find(pk->parent)) L = inverse(attachFrom(*p, pk->node)) * W;
    Vec3 t, r, s;
    decomposeTRS(L, t, r, s);
    setLocal(id, t, r, s);
}

int Scene::addLight(LightKind k) {
    Object* o = new Object();
    o->id = nextId_++;
    o->type = ObjType::Light;
    o->light = k;
    o->name = uniqueName(k == LightKind::Sun ? "Солнце" : "Свет");
    o->energy = k == LightKind::Sun ? 3.5f : 25.f;
    o->t = k == LightKind::Sun ? Vec3(4, 8, 4) : Vec3(1.5f, 2.5f, 1.5f);
    int id = o->id;
    objects.emplace_back(o);
    selected = id;
    return id;
}

int Scene::addCamera() {
    Object* o = new Object();
    o->id = nextId_++;
    o->type = ObjType::Camera;
    o->name = uniqueName("Камера");
    o->t = {3.4f, 1.9f, 5.2f};
    int id = o->id;
    objects.emplace_back(o);
    if (activeCamera < 0) activeCamera = id;
    selected = id;
    return id;
}

int Scene::addEffect(FxKind k) {
    static const char* names[] = {"Огонь", "Дым", "Вспышка", "Взрыв", "Искры", "Кровь"};
    Object* o = new Object();
    o->id = nextId_++;
    o->type = ObjType::Effect;
    o->fx = k;
    o->name = uniqueName(names[(int)k]);
    o->fxStart = (int)std::lround(frame_);
    o->fxEnd = k == FxKind::Explosion || k == FxKind::Muzzle || k == FxKind::Blood ? o->fxStart + (k == FxKind::Muzzle ? 24 : 12) : proj.frameEnd;
    if (k == FxKind::Muzzle) o->fxRate = 4;
    o->t = {0, 0.2f, 0};
    if (k == FxKind::Muzzle || k == FxKind::Sparks || k == FxKind::Blood) o->t = {0, 1.f, 0};
    int id = o->id;
    objects.emplace_back(o);
    selected = id;
    evaluate(frame_);
    return id;
}

int Scene::duplicate(int id) {
    const Object* s = find(id);
    if (!s) return -1;
    Object* o = new Object();
    o->id = nextId_++;
    o->type = s->type; o->visible = s->visible; o->t = s->t + Vec3(0.5f, 0, 0.5f); o->r = s->r; o->s = s->s;
    o->keys = s->keys; o->parents = s->parents; o->clipKeys = s->clipKeys; o->visKeys = s->visKeys;
    o->model = s->model; o->fit = s->fit; o->matOverride = s->matOverride;
    std::memcpy(o->color, s->color, sizeof o->color);
    o->metallic = s->metallic; o->roughness = s->roughness; o->emission = s->emission; o->castShadow = s->castShadow;
    o->light = s->light; std::memcpy(o->lcolor, s->lcolor, sizeof o->lcolor); o->energy = s->energy; o->lradius = s->lradius;
    o->fovDeg = s->fovDeg; o->fx = s->fx; o->fxStart = s->fxStart; o->fxEnd = s->fxEnd; o->fxScale = s->fxScale; o->fxRate = s->fxRate;
    o->fxVariant = s->fxVariant; o->fxPower = s->fxPower; o->fxPush = s->fxPush; o->phys = s->phys; o->boneTracks = s->boneTracks;
    o->name = uniqueName(s->name);
    for (auto& k : o->keys) k.t = k.t + Vec3(0.5f, 0, 0.5f);
    int nid = o->id;
    objects.emplace_back(o);
    selected = nid;
    evaluate(frame_);
    return nid;
}

void Scene::remove(int id) {
    int i = indexOf(id);
    if (i < 0) return;
    objects.erase(objects.begin() + i);
    for (auto& o : objects)
        for (auto& pk : o->parents) if (pk.parent == id) { pk.parent = -1; pk.node = -1; }
    if (selected == id) selected = -1;
    if (activeCamera == id) {
        activeCamera = -1;
        for (auto& o : objects) if (o->type == ObjType::Camera) { activeCamera = o->id; break; }
    }
    evaluate(frame_);
}

void Scene::rename(int id, const std::string& nm) {
    Object* o = find(id);
    if (o && !nm.empty()) o->name = nm;
}

// ============================================================ ключевые кадры
static void sortKeys(std::vector<TKey>& k) { std::sort(k.begin(), k.end(), [](const TKey& a, const TKey& b) { return a.frame < b.frame; }); }

void Scene::evalLocal(const Object& o, float f, Vec3& t, Vec3& r, Vec3& s) const {
    const auto& k = o.keys;
    if (k.empty()) { t = o.t; r = o.r; s = o.s; return; }
    if (f <= k.front().frame) { t = k.front().t; r = k.front().r; s = k.front().s; return; }
    if (f >= k.back().frame) { t = k.back().t; r = k.back().r; s = k.back().s; return; }
    size_t i = 0;
    while (i + 1 < k.size() && k[i + 1].frame <= f) i++;
    const TKey& a = k[i];
    const TKey& b = k[i + 1];
    float u = (f - a.frame) / std::max(1.f, (float)(b.frame - a.frame));
    if (a.ease == 2) u = 0;
    else if (a.ease == 1) u = u * u * (3.f - 2.f * u);
    t = lerp(a.t, b.t, u); r = lerp(a.r, b.r, u); s = lerp(a.s, b.s, u);
}

void Scene::insertKeyRaw(Object& o, int frame, Vec3 t, Vec3 r, Vec3 s) {
    for (auto& k : o.keys) if (k.frame == frame) { k.t = t; k.r = r; k.s = s; return; }
    TKey k; k.frame = frame; k.t = t; k.r = r; k.s = s;
    o.keys.push_back(k);
    sortKeys(o.keys);
}

bool Scene::hasKeyAt(const Object& o, int frame) const {
    for (auto& k : o.keys) if (k.frame == frame) return true;
    return false;
}

const ParentKey* Scene::activeParent(const Object& o, float f) const {
    const ParentKey* a = nullptr;
    for (auto& pk : o.parents) if (pk.frame <= f && (!a || pk.frame >= a->frame)) a = &pk;
    return a && a->parent >= 0 ? a : nullptr;
}
bool Scene::isAttached(const Object& o, float f) const { return activeParent(o, f) != nullptr; }

void Scene::localTRS(const Object& o, Vec3& t, Vec3& r, Vec3& s) const {
    if (const ParentKey* pk = activeParent(o, frame_)) { decomposeTRS(pk->offset, t, r, s); return; }
    evalLocal(o, frame_, t, r, s);
}

bool Scene::insertKey(int id, int frame) {
    Object* o = find(id);
    if (!o || isAttached(*o, (float)frame)) return false;
    Vec3 t, r, s;
    evalLocal(*o, (float)frame, t, r, s);
    if (frame == (int)std::lround(frame_)) localTRS(*o, t, r, s);
    insertKeyRaw(*o, frame, t, r, s);
    return true;
}

bool Scene::deleteKey(int id, int frame) {
    Object* o = find(id);
    if (!o) return false;
    size_t n = o->keys.size();
    o->keys.erase(std::remove_if(o->keys.begin(), o->keys.end(), [&](const TKey& k) { return k.frame == frame; }), o->keys.end());
    size_t pn = o->parents.size();
    o->parents.erase(std::remove_if(o->parents.begin(), o->parents.end(), [&](const ParentKey& k) { return k.frame == frame; }), o->parents.end());
    bool changed = n != o->keys.size() || pn != o->parents.size();
    if (changed) evaluate(frame_);
    return changed;
}

void Scene::setLocal(int id, Vec3 t, Vec3 r, Vec3 s) {
    Object* o = find(id);
    if (!o) return;
    int f = (int)std::lround(frame_);
    if (const ParentKey* pk = activeParent(*o, frame_)) {
        for (auto& k : o->parents) if (&k == pk) k.offset = trs(t, quatEuler(r.x, r.y, r.z), s);
    } else if (!o->keys.empty() || autoKey) {
        insertKeyRaw(*o, f, t, r, s);
    } else {
        o->t = t; o->r = r; o->s = s;
    }
    evaluate(frame_);
}

bool Scene::wouldCycle(int child, int parent) const {
    // можно ли дойти от parent вверх до child по любым ключам прицепления
    std::vector<int> stack{parent}, seen;
    while (!stack.empty()) {
        int x = stack.back(); stack.pop_back();
        if (x == child) return true;
        if (std::find(seen.begin(), seen.end(), x) != seen.end()) continue;
        seen.push_back(x);
        if (const Object* o = find(x)) for (auto& pk : o->parents) if (pk.parent >= 0) stack.push_back(pk.parent);
    }
    return false;
}

Mat4 Scene::attachFrom(const Object& p, int node) const {
    Mat4 W = p.world;
    if (node >= 0 && p.type == ObjType::Model && p.anim && node < (int)p.anim->world.size()) W = W * p.fit * p.anim->world[node];
    return W;
}

bool Scene::attach(int childId, int parentId, int node, int frame) {
    Object* c = find(childId);
    Object* p = find(parentId);
    if (!c || !p || childId == parentId || wouldCycle(childId, parentId)) return false;
    evaluate((float)frame);
    Mat4 W = c->world;
    if (!isAttached(*c, (float)frame)) {
        Vec3 t, r, s;
        decomposeTRS(W, t, r, s);
        insertKeyRaw(*c, frame, t, r, s);  // до прицепления объект остаётся там, где был
    }
    ParentKey pk;
    pk.frame = frame; pk.parent = parentId; pk.node = (p->type == ObjType::Model) ? node : -1;
    pk.offset = inverse(attachFrom(*p, pk.node)) * W;
    bool done = false;
    for (auto& k : c->parents) if (k.frame == frame) { k = pk; done = true; }
    if (!done) c->parents.push_back(pk);
    std::sort(c->parents.begin(), c->parents.end(), [](const ParentKey& a, const ParentKey& b) { return a.frame < b.frame; });
    evaluate((float)frame);
    return true;
}

bool Scene::detach(int childId, int frame) {
    Object* c = find(childId);
    if (!c) return false;
    evaluate((float)frame);
    if (!isAttached(*c, (float)frame)) return false;
    Vec3 t, r, s;
    decomposeTRS(c->world, t, r, s);
    insertKeyRaw(*c, frame, t, r, s);  // после отцепления объект продолжает с того же места
    ParentKey pk;
    pk.frame = frame; pk.parent = -1;
    bool done = false;
    for (auto& k : c->parents) if (k.frame == frame) { k = pk; done = true; }
    if (!done) c->parents.push_back(pk);
    std::sort(c->parents.begin(), c->parents.end(), [](const ParentKey& a, const ParentKey& b) { return a.frame < b.frame; });
    evaluate((float)frame);
    return true;
}

void Scene::setClip(int id, int frame, int clip, float speed, bool loop) {
    Object* o = find(id);
    if (!o) return;
    ClipKey k; k.frame = frame; k.clip = clip; k.speed = speed; k.loop = loop;
    bool done = false;
    for (auto& c : o->clipKeys) if (c.frame == frame) { c = k; done = true; }
    if (!done) o->clipKeys.push_back(k);
    std::sort(o->clipKeys.begin(), o->clipKeys.end(), [](const ClipKey& a, const ClipKey& b) { return a.frame < b.frame; });
    evaluate(frame_);
}

// ============================================================ вычисление кадра
void Scene::evalAnimation(Object& o, float f) {
    if (o.type != ObjType::Model || o.model < 0 || o.model >= (int)models.size() || !models[o.model].model) return;
    Model* m = models[o.model].model.get();
    if (!o.anim) { o.anim.reset(new Animator()); o.anim->setModel(m); }
    o.anim->deltas.clear();
    for (const BoneTrack& tr : o.boneTracks) {
        BoneDelta d;
        d.node = tr.node;
        if (boneDeltaAt(o, tr.node, f, d.t, d.r)) o.anim->deltas.push_back(d);
    }
    if (o.clipKeys.empty() || m->clips.empty()) { o.anim->evaluate(-1, 0); return; }
    int fps = std::max(1, proj.fps);
    size_t i = 0;
    bool before = f < o.clipKeys.front().frame;
    for (size_t k = 0; k < o.clipKeys.size(); k++) if (o.clipKeys[k].frame <= f) i = k;
    const ClipKey& cur = o.clipKeys[i];
    auto timeOf = [&](const ClipKey& k, float ff) {
        if (k.clip < 0 || k.clip >= (int)m->clips.size()) return 0.f;
        float t = std::max(0.f, ff - k.frame) / fps * k.speed;
        float d = m->clips[k.clip].duration;
        if (d > 1e-6f) t = k.loop ? std::fmod(t, d) : std::min(t, d);
        return t;
    };
    float tc = before ? 0.f : timeOf(cur, f);
    int blendFrames = std::max(1, (int)std::lround(0.25f * fps));
    if (i > 0 && !before && f - cur.frame < blendFrames) {
        const ClipKey& prev = o.clipKeys[i - 1];
        o.anim->evaluate(prev.clip, timeOf(prev, f), cur.clip, tc, (f - cur.frame) / (float)blendFrames);
    } else {
        o.anim->evaluate(cur.clip, tc);
    }
}

Mat4 Scene::worldOf(Object& o, float f, std::vector<char>& state) {
    int idx = indexOf(o.id);
    if (idx >= 0) {
        if (state[idx] == 2) return o.world;
        state[idx] = 1;
    }
    Vec3 t, r, s;
    evalLocal(o, f, t, r, s);
    Mat4 W = trs(t, quatEuler(r.x, r.y, r.z), s);
    if (const ParentKey* pk = activeParent(o, f)) {
        int pi = indexOf(pk->parent);
        if (pi >= 0 && state[pi] != 1) {
            Object& p = *objects[pi];
            worldOf(p, f, state);
            W = attachFrom(p, pk->node) * pk->offset;
        }
    }
    if (!physOff_ && o.type == ObjType::Model && o.phys.enabled && o.phys.kind == 0 && f >= (float)o.phys.start) {
        auto it = phys_.mats.find(o.id);
        if (it != phys_.mats.end() && !it->second.empty()) {
            int i = (int)std::lround(f) - o.phys.start;
            i = std::max(0, std::min(i, (int)it->second.size() - 1));
            W = it->second[i];
        }
    }
    o.world = W;
    if (idx >= 0) state[idx] = 2;
    return W;
}

void Scene::evaluate(float f) {
    if (!baking_) bakePhysics();
    frame_ = f;
    for (auto& o : objects) {
        o->visibleNow = o->visible;
        const VisKey* vk = nullptr;
        for (auto& k : o->visKeys) if (k.frame <= f && (!vk || k.frame >= vk->frame)) vk = &k;
        if (vk) o->visibleNow = vk->visible;
        evalAnimation(*o, f);
    }
    std::vector<char> state(objects.size(), 0);
    for (auto& o : objects) worldOf(*o, f, state);
}

// ============================================================ эффекты
void Scene::resetEffects() {
    fx_.clear();
    for (auto& o : objects) std::memset(o->fxAcc, 0, sizeof o->fxAcc);
    simFrame_ = -1000000;
}

static uint32_t hash3(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA6Bu ^ (c + 0x165667B1u) * 0xC2B2AE35u;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12;
    return h;
}

void Scene::stepEffects(int f) {
    float dt = 1.f / std::max(1, proj.fps) / 2.f;
    fx_.beginStep();
    for (int sub = 0; sub < 2; sub++) {
        for (auto& op : objects) {
            Object& o = *op;
            if (o.type != ObjType::Effect || !o.visibleNow || f < o.fxStart || f > o.fxEnd) continue;
            fx_.seed(hash3((uint32_t)o.id, (uint32_t)f, (uint32_t)sub));
            Vec3 pos = transformPoint(o.world, {0, 0, 0});
            Vec3 dir = normalize(transformDir(o.world, {0, 0, 1}));
            float sc = std::max(0.05f, o.fxScale);
            switch (o.fx) {
                case FxKind::Fire:
                    fx_.emitFire(pos, sc, dt, o.fxAcc);
                    fx_.emitSmoke(pos + Vec3(0, 0.45f * sc, 0), sc * 0.7f, dt * 0.6f, o.fxAcc);
                    fx_.emitSparks(pos + Vec3(0, 0.1f * sc, 0), {0, 1, 0}, sc * 0.25f, dt, o.fxAcc);
                    if (sub == 0) fx_.fireLight(pos, sc, (float)f / std::max(1, proj.fps));
                    break;
                case FxKind::Smoke: fx_.emitSmoke(pos, sc, dt, o.fxAcc); break;
                case FxKind::Sparks: fx_.emitSparks(pos, dir, sc, dt, o.fxAcc); break;
                case FxKind::Muzzle: {
                    bool shot = o.fxRate <= 0 ? f == o.fxStart : ((f - o.fxStart) % o.fxRate == 0);
                    if (sub == 0 && shot) fx_.muzzleFlash(pos, dir, sc, o.fxVariant, o.fxPower);
                    break;
                }
                case FxKind::Explosion:
                    if (sub == 0 && f == o.fxStart) fx_.explosion(pos, sc, o.fxPower);
                    break;
                case FxKind::Blood:
                    if (o.fxVariant == 0) { if (sub == 0 && f == o.fxStart) fx_.bloodBurst(pos, dir, sc, o.fxPower); }
                    else fx_.bloodFlow(pos, dir, sc, o.fxPower, dt, o.fxAcc);
                    break;
            }
        }
        fx_.update(dt, 0.f);
    }
}

void Scene::simulateTo(int frame) {
    int warm = 5 * std::max(1, proj.fps);
    bool anyFx = false;
    for (auto& o : objects) if (o->type == ObjType::Effect) { anyFx = true; if (o->fx == FxKind::Blood) warm = 1 << 20; }  // пятна крови остаются навсегда
    if (!anyFx) { evaluate((float)frame); simFrame_ = frame; return; }
    if (frame == simFrame_) { evaluate((float)frame); return; }
    int begin;
    if (simFrame_ > -999999 && frame > simFrame_ && frame - simFrame_ <= warm) begin = simFrame_ + 1;
    else { resetEffects(); begin = std::max(frame - warm, proj.frameStart); }
    for (int f = begin; f <= frame; f++) { evaluate((float)f); stepEffects(f); }
    simFrame_ = frame;
}

// ============================================================ кости
std::vector<int> Scene::boneNodes(const Object& o) const {
    std::vector<int> out;
    if (o.type != ObjType::Model || o.model < 0 || o.model >= (int)models.size() || !models[o.model].model) return out;
    const Model& m = *models[o.model].model;
    std::vector<char> seen(m.nodes.size(), 0);
    for (const Skin& sk : m.skins)
        for (int j : sk.joints) if (j >= 0 && j < (int)seen.size() && !seen[j]) { seen[j] = 1; out.push_back(j); }
    if (out.empty() && m.nodes.size() > 1)  // жёсткий скелет без скина: любой узел можно крутить
        for (int i : m.order) out.push_back(i);
    return out;
}

int Scene::boneCount(const Object& o) const { return (int)boneNodes(o).size(); }

bool Scene::boneDeltaAt(const Object& o, int node, float f, Vec3& t, Quat& r) const {
    for (const BoneTrack& tr : o.boneTracks) {
        if (tr.node != node || tr.keys.empty()) continue;
        const auto& k = tr.keys;
        if (f <= k.front().frame) { t = k.front().t; r = k.front().r; return true; }
        if (f >= k.back().frame) { t = k.back().t; r = k.back().r; return true; }
        size_t i = 0;
        while (i + 1 < k.size() && k[i + 1].frame <= f) i++;
        const BoneKey& a = k[i];
        const BoneKey& b = k[i + 1];
        float u = (f - a.frame) / std::max(1.f, (float)(b.frame - a.frame));
        if (a.ease == 2) u = 0;
        else if (a.ease == 1) u = u * u * (3.f - 2.f * u);
        t = lerp(a.t, b.t, u);
        r = slerp(a.r, b.r, u);
        return true;
    }
    return false;
}

bool Scene::boneHasKey(const Object& o, int node, int frame) const {
    for (const BoneTrack& tr : o.boneTracks)
        if (tr.node == node) for (const BoneKey& k : tr.keys) if (k.frame == frame) return true;
    return false;
}

bool Scene::setBoneKey(int id, int node, int frame, Vec3 t, Quat r) {
    Object* o = find(id);
    if (!o || o->type != ObjType::Model) return false;
    BoneTrack* tr = nullptr;
    for (auto& x : o->boneTracks) if (x.node == node) tr = &x;
    if (!tr) { o->boneTracks.push_back(BoneTrack()); tr = &o->boneTracks.back(); tr->node = node; }
    bool done = false;
    for (auto& k : tr->keys) if (k.frame == frame) { k.t = t; k.r = normalize(r); done = true; }
    if (!done) {
        BoneKey k; k.frame = frame; k.t = t; k.r = normalize(r);
        tr->keys.push_back(k);
        std::sort(tr->keys.begin(), tr->keys.end(), [](const BoneKey& a, const BoneKey& b) { return a.frame < b.frame; });
    }
    evaluate(frame_);
    return true;
}

bool Scene::deleteBoneKey(int id, int node, int frame) {
    Object* o = find(id);
    if (!o) return false;
    bool changed = false;
    for (auto& tr : o->boneTracks) {
        if (tr.node != node) continue;
        size_t n = tr.keys.size();
        tr.keys.erase(std::remove_if(tr.keys.begin(), tr.keys.end(), [&](const BoneKey& k) { return k.frame == frame; }), tr.keys.end());
        changed |= n != tr.keys.size();
    }
    o->boneTracks.erase(std::remove_if(o->boneTracks.begin(), o->boneTracks.end(), [](const BoneTrack& t) { return t.keys.empty(); }), o->boneTracks.end());
    if (changed) evaluate(frame_);
    return changed;
}

void Scene::clearBone(int id, int node) {
    Object* o = find(id);
    if (!o) return;
    o->boneTracks.erase(std::remove_if(o->boneTracks.begin(), o->boneTracks.end(), [&](const BoneTrack& t) { return t.node == node; }), o->boneTracks.end());
    evaluate(frame_);
}

// ============================================================ физика
static inline void hmix(uint64_t& h, uint32_t v) { h ^= v; h *= 1099511628211ull; }
static inline void hf(uint64_t& h, float f) { uint32_t u; std::memcpy(&u, &f, 4); hmix(h, u); }
static inline void hv(uint64_t& h, Vec3 v) { hf(h, v.x); hf(h, v.y); hf(h, v.z); }

uint64_t Scene::physSignature() const {
    uint64_t h = 1469598103934665603ull;
    hmix(h, (uint32_t)proj.fps); hmix(h, (uint32_t)proj.frameStart); hmix(h, (uint32_t)proj.frameEnd); hmix(h, render.floor ? 1u : 0u);
    for (const auto& op : objects) {
        const Object& o = *op;
        bool rel = o.type == ObjType::Model || (o.type == ObjType::Effect && o.fx == FxKind::Explosion);
        if (!rel) continue;
        hmix(h, (uint32_t)o.id); hmix(h, (uint32_t)o.model);
        hv(h, o.t); hv(h, o.r); hv(h, o.s);
        for (const auto& k : o.keys) { hmix(h, (uint32_t)k.frame); hv(h, k.t); hv(h, k.r); hv(h, k.s); hmix(h, (uint32_t)k.ease); }
        for (const auto& k : o.parents) { hmix(h, (uint32_t)k.frame); hmix(h, (uint32_t)k.parent); hmix(h, (uint32_t)k.node); for (int q = 0; q < 16; q++) hf(h, k.offset.m[q]); }
        for (const auto& k : o.clipKeys) { hmix(h, (uint32_t)k.frame); hmix(h, (uint32_t)k.clip); hf(h, k.speed); }
        for (const auto& tr : o.boneTracks) for (const auto& k : tr.keys) { hmix(h, (uint32_t)tr.node); hmix(h, (uint32_t)k.frame); hv(h, k.t); hf(h, k.r.x); hf(h, k.r.y); hf(h, k.r.z); hf(h, k.r.w); }
        const PhysProps& p = o.phys;
        hmix(h, p.enabled ? 1u : 0u); hmix(h, (uint32_t)p.kind); hmix(h, (uint32_t)p.shape); hf(h, p.mass); hf(h, p.bounce); hf(h, p.friction);
        hmix(h, (uint32_t)p.start); hmix(h, p.inherit ? 1u : 0u); hv(h, p.vel); hv(h, p.spin);
        if (o.type == ObjType::Effect) { hmix(h, (uint32_t)o.fxStart); hf(h, o.fxPower); hf(h, o.fxScale); hmix(h, o.fxPush ? 1u : 0u); }
    }
    return h;
}

bool Scene::physicsActive(const Object& o, float f) const {
    return o.type == ObjType::Model && o.phys.enabled && o.phys.kind == 0 && f >= (float)o.phys.start;
}

void Scene::bakePhysics() {
    if (baking_) return;
    uint64_t sig = physSignature();
    if (phys_.valid && phys_.sig == sig) return;
    phys_.mats.clear();
    phys_.valid = true;
    phys_.sig = sig;
    phys_.first = 0;
    phys_.last = -1;
    std::vector<Object*> dyn, col, boom;
    for (auto& op : objects) {
        Object* o = op.get();
        if (o->type == ObjType::Model && o->phys.enabled && o->model >= 0 && o->model < (int)models.size() && models[o->model].model) (o->phys.kind == 0 ? dyn : col).push_back(o);
        else if (o->type == ObjType::Effect && o->fx == FxKind::Explosion && o->fxPush) boom.push_back(o);
    }
    if (dyn.empty()) return;

    baking_ = true;
    physOff_ = true;
    float saved = frame_;
    int fps = std::max(1, proj.fps);
    int first = 1 << 30;
    for (Object* o : dyn) first = std::min(first, std::max(0, o->phys.start));
    int last = std::max(proj.frameEnd, first);
    last = std::min(last, first + 6000);
    phys_.first = first;
    phys_.last = last;

    struct Rec {
        Object* o;
        bool created = false;
        int body = -1;
        Mat4 W0, Tb0inv;
        Vec3 localC, half;
    };
    std::vector<Rec> recs;
    for (Object* o : dyn) { Rec r; r.o = o; recs.push_back(r); }

    PhysicsWorld w;
    w.setGravity({0, -9.81f, 0});
    w.setGroundY(0.f);
    w.setGroundEnabled(render.floor);
    int sub = std::max(1, (int)std::lround(120.0 / fps));
    float dtSub = 1.f / (float)fps / (float)sub;

    auto boxOf = [&](const Object& o, Vec3& centerW, Quat& q, Vec3& half, Vec3& localC) {
        const Model& m = *models[o.model].model;
        Mat4 M = o.world * o.fit;
        Vec3 t, sc;
        decompose(M, t, q, sc);
        q = normalize(q);
        localC = (m.bmin + m.bmax) * 0.5f;
        centerW = transformPoint(M, localC);
        Vec3 ext = (m.bmax - m.bmin) * 0.5f;
        half = {std::max(0.02f, std::fabs(sc.x) * ext.x), std::max(0.02f, std::fabs(sc.y) * ext.y), std::max(0.02f, std::fabs(sc.z) * ext.z)};
    };

    std::map<int, Mat4> prevW;
    for (int f = first - 1; f <= last; f++) {
        evaluate((float)f);
        if (f < first) { for (Rec& r : recs) prevW[r.o->id] = r.o->world; continue; }

        // тела появляются на своём стартовом кадре
        for (Rec& r : recs) {
            if (r.created || f < r.o->phys.start) continue;
            Vec3 c, half, lc;
            Quat q;
            boxOf(*r.o, c, q, half, lc);
            BodyDesc d;
            d.pos = c; d.rot = q;
            if (r.o->phys.shape == 1) { d.shape = ShapeType::Sphere; d.radius = std::max(0.02f, (half.x + half.y + half.z) / 3.f); }
            else { d.shape = ShapeType::Box; d.half = half; }
            d.mass = std::max(0.05f, r.o->phys.mass);
            d.friction = clampf(r.o->phys.friction, 0.f, 2.f);
            d.restitution = clampf(r.o->phys.bounce, 0.f, 0.98f);
            Vec3 v = r.o->phys.vel;
            if (r.o->phys.inherit && prevW.count(r.o->id)) {
                Vec3 cp = transformPoint(prevW[r.o->id] * r.o->fit, lc);
                Vec3 dv = (c - cp) * (float)fps;
                float sp = length(dv);
                if (sp > 40.f) dv = dv * (40.f / sp);
                v = v + dv;
            }
            d.vel = v;
            d.angVel = r.o->phys.spin;
            r.body = w.addBody(d);
            r.created = r.body >= 0;
            r.localC = lc; r.half = half;
            r.W0 = r.o->world;
            r.Tb0inv = inverse(translate(c) * fromQuat(q));
            phys_.mats[r.o->id];  // создаёт запись
        }

        // препятствия
        w.clearStaticBoxes();
        for (Object* o : col) {
            Vec3 c, half, lc;
            Quat q;
            boxOf(*o, c, q, half, lc);
            w.addStaticBox(c, q, half);
        }

        // взрывы толкают тела
        for (Object* e : boom) {
            if (f != e->fxStart || !e->visibleNow) continue;
            Vec3 c = transformPoint(e->world, {0, 0, 0});
            float pw = std::max(0.1f, e->fxPower), sc = std::max(0.1f, e->fxScale);
            float radius = (3.f + 5.f * pw) * sc, speed = 9.f * pw * std::sqrt(sc);
            for (Rec& r : recs) {
                if (!r.created) continue;
                const Body& b = w.bodies()[r.body];
                Vec3 dv = b.pos - c;
                float dist = length(dv);
                if (dist > radius) continue;
                float k = 1.f - dist / radius;
                Vec3 dir = normalize(dist > 1e-4f ? dv + Vec3(0, 0.35f * radius * 0.25f, 0) : Vec3(0, 1, 0));
                Vec3 side = normalize(cross(dir, Vec3(0.3f, 1, 0.2f)));
                float mass = b.invMass > 0 ? 1.f / b.invMass : 1.f;
                float size = b.shape == ShapeType::Sphere ? b.radius : length(b.half);
                w.applyImpulse(r.body, dir * (speed * k * k * mass), b.pos + side * (0.4f * size));
            }
        }

        // запись положения на начало кадра, затем шаг
        for (Rec& r : recs) {
            if (!r.created) continue;
            const Body& b = w.bodies()[r.body];
            Mat4 Tb = translate(b.pos) * fromQuat(b.rot);
            phys_.mats[r.o->id].push_back(Tb * r.Tb0inv * r.W0);
        }
        for (int k = 0; k < sub; k++) w.step(dtSub);
        for (Rec& r : recs) prevW[r.o->id] = r.o->world;
    }
    physOff_ = false;
    baking_ = false;
    evaluate(saved);
}

// ============================================================ выбор
bool Scene::localBox(const Object& o, Vec3& lo, Vec3& hi) const {
    if (o.type != ObjType::Model || o.model < 0 || o.model >= (int)models.size() || !models[o.model].model) return false;
    lo = models[o.model].model->bmin; hi = models[o.model].model->bmax;
    return true;
}

int Scene::pick(const Vec3& origin, const Vec3& dir, float* distOut) const {
    int best = -1;
    float bestT = 1e30f;
    for (auto& op : objects) {
        const Object& o = *op;
        Vec3 lo, hi;
        if (!o.visibleNow || !localBox(o, lo, hi)) continue;
        Mat4 inv = inverse(o.world * o.fit);
        Vec3 ro = transformPoint(inv, origin), rd = transformDir(inv, dir);
        float t0 = 0, t1 = 1e30f;
        bool ok = true;
        const float rr[3] = {ro.x, ro.y, ro.z}, dd[3] = {rd.x, rd.y, rd.z}, l[3] = {lo.x, lo.y, lo.z}, h[3] = {hi.x, hi.y, hi.z};
        for (int a = 0; a < 3 && ok; a++) {
            if (std::fabs(dd[a]) < 1e-9f) { if (rr[a] < l[a] || rr[a] > h[a]) ok = false; continue; }
            float ta = (l[a] - rr[a]) / dd[a], tb = (h[a] - rr[a]) / dd[a];
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta); t1 = std::min(t1, tb);
            if (t0 > t1) ok = false;
        }
        if (ok && t0 < bestT) { bestT = t0; best = o.id; }
    }
    if (distOut) *distOut = bestT;
    return best;
}

// ============================================================ JSON
namespace {
struct Out {
    std::string s;
    void raw(const std::string& x) { s += x; }
    void num(double v) { char b[32]; std::snprintf(b, sizeof b, "%.7g", v); s += b; }
    void str(const std::string& x) {
        s += '"';
        for (char c : x) {
            if (c == '"' || c == '\\') { s += '\\'; s += c; }
            else if (c == '\n') s += "\\n";
            else if ((unsigned char)c < 0x20) s += ' ';
            else s += c;
        }
        s += '"';
    }
    void key(const char* k) { s += '"'; s += k; s += "\":"; }
    void vec(Vec3 v) { s += '['; num(v.x); s += ','; num(v.y); s += ','; num(v.z); s += ']'; }
    void arr3(const float* p) { vec({p[0], p[1], p[2]}); }
};
Vec3 jvec(const Json& j, Vec3 def = {}) {
    if (j.type != Json::Arr || j.size() < 3) return def;
    return {(float)j[0].number(), (float)j[1].number(), (float)j[2].number()};
}
void jarr3(const Json& j, float* out) {
    if (j.type != Json::Arr || j.size() < 3) return;
    for (int i = 0; i < 3; i++) out[i] = (float)j[i].number();
}
bool jbool(const Json& j, bool def) { return j.type == Json::Bool ? j.b : def; }
}  // namespace

std::string Scene::toJson() const {
    Out o;
    o.raw("{");
    o.key("name"); o.str(name); o.raw(",");
    o.key("nextId"); o.num(nextId_); o.raw(",");
    o.key("selected"); o.num(selected); o.raw(",");
    o.key("activeCamera"); o.num(activeCamera); o.raw(",");
    o.key("frame"); o.num(frame_); o.raw(",");
    o.key("autoKey"); o.raw(autoKey ? "true," : "false,");
    o.key("proj"); o.raw("{"); o.key("fps"); o.num(proj.fps); o.raw(","); o.key("start"); o.num(proj.frameStart); o.raw(","); o.key("end"); o.num(proj.frameEnd); o.raw("},");
    const RenderSettings& r = render;
    o.key("render"); o.raw("{");
    o.key("exposure"); o.num(r.exposure); o.raw(","); o.key("contrast"); o.num(r.contrast); o.raw(","); o.key("saturation"); o.num(r.saturation); o.raw(",");
    o.key("ao"); o.raw(r.ao ? "true," : "false,"); o.key("aoStrength"); o.num(r.aoStrength); o.raw(",");
    o.key("bloom"); o.raw(r.bloom ? "true," : "false,"); o.key("bloomStrength"); o.num(r.bloomStrength); o.raw(",");
    o.key("shadows"); o.raw(r.shadows ? "true," : "false,"); o.key("shadowSoft"); o.num(r.shadowSoft); o.raw(","); o.key("contact"); o.raw(r.contactShadows ? "true," : "false,");
    o.key("skyBrightness"); o.num(r.skyBrightness); o.raw(",");
    o.key("skyTop"); o.arr3(r.skyTop); o.raw(","); o.key("skyHorizon"); o.arr3(r.skyHorizon); o.raw(","); o.key("skyGround"); o.arr3(r.skyGround); o.raw(",");
    o.key("floor"); o.raw(r.floor ? "true," : "false,"); o.key("floorColor"); o.arr3(r.floorColor); o.raw(","); o.key("floorRoughness"); o.num(r.floorRoughness); o.raw(",");
    o.key("vignette"); o.raw(r.vignette ? "true," : "false,");
    o.key("exportW"); o.num(r.exportW); o.raw(","); o.key("exportH"); o.num(r.exportH); o.raw(",");
    o.key("stillSamples"); o.num(r.stillSamples); o.raw(","); o.key("videoSamples"); o.num(r.videoSamples); o.raw(","); o.key("videoBitrate"); o.num(r.videoBitrateMbps);
    o.raw("},");
    o.key("objects"); o.raw("[");
    for (size_t i = 0; i < objects.size(); i++) {
        const Object& b = *objects[i];
        if (i) o.raw(",");
        o.raw("{");
        o.key("id"); o.num(b.id); o.raw(","); o.key("name"); o.str(b.name); o.raw(","); o.key("type"); o.num((int)b.type); o.raw(",");
        o.key("visible"); o.raw(b.visible ? "true," : "false,");
        o.key("t"); o.vec(b.t); o.raw(","); o.key("r"); o.vec(b.r); o.raw(","); o.key("s"); o.vec(b.s); o.raw(",");
        o.key("keys"); o.raw("[");
        for (size_t k = 0; k < b.keys.size(); k++) {
            const TKey& tk = b.keys[k];
            if (k) o.raw(",");
            o.raw("{"); o.key("f"); o.num(tk.frame); o.raw(","); o.key("t"); o.vec(tk.t); o.raw(","); o.key("r"); o.vec(tk.r); o.raw(","); o.key("s"); o.vec(tk.s); o.raw(","); o.key("e"); o.num(tk.ease); o.raw("}");
        }
        o.raw("],"); o.key("parents"); o.raw("[");
        for (size_t k = 0; k < b.parents.size(); k++) {
            const ParentKey& pk = b.parents[k];
            if (k) o.raw(",");
            o.raw("{"); o.key("f"); o.num(pk.frame); o.raw(","); o.key("p"); o.num(pk.parent); o.raw(","); o.key("n"); o.num(pk.node); o.raw(","); o.key("m"); o.raw("[");
            for (int q = 0; q < 16; q++) { if (q) o.raw(","); o.num(pk.offset.m[q]); }
            o.raw("]}");
        }
        o.raw("],"); o.key("clips"); o.raw("[");
        for (size_t k = 0; k < b.clipKeys.size(); k++) {
            const ClipKey& ck = b.clipKeys[k];
            if (k) o.raw(",");
            o.raw("{"); o.key("f"); o.num(ck.frame); o.raw(","); o.key("c"); o.num(ck.clip); o.raw(","); o.key("sp"); o.num(ck.speed); o.raw(","); o.key("l"); o.raw(ck.loop ? "true}" : "false}");
        }
        o.raw("],"); o.key("vis"); o.raw("[");
        for (size_t k = 0; k < b.visKeys.size(); k++) {
            if (k) o.raw(",");
            o.raw("{"); o.key("f"); o.num(b.visKeys[k].frame); o.raw(","); o.key("v"); o.raw(b.visKeys[k].visible ? "true}" : "false}");
        }
        o.raw("],");
        o.key("model"); o.num(b.model); o.raw(",");
        o.key("fit"); o.raw("[");
        for (int q = 0; q < 16; q++) { if (q) o.raw(","); o.num(b.fit.m[q]); }
        o.raw("],");
        o.key("matOverride"); o.raw(b.matOverride ? "true," : "false,"); o.key("color"); o.arr3(b.color); o.raw(",");
        o.key("metallic"); o.num(b.metallic); o.raw(","); o.key("roughness"); o.num(b.roughness); o.raw(","); o.key("emission"); o.num(b.emission); o.raw(",");
        o.key("castShadow"); o.raw(b.castShadow ? "true," : "false,");
        o.key("light"); o.num((int)b.light); o.raw(","); o.key("lcolor"); o.arr3(b.lcolor); o.raw(","); o.key("energy"); o.num(b.energy); o.raw(","); o.key("lradius"); o.num(b.lradius); o.raw(",");
        o.key("fov"); o.num(b.fovDeg); o.raw(",");
        o.key("fx"); o.num((int)b.fx); o.raw(","); o.key("fxStart"); o.num(b.fxStart); o.raw(","); o.key("fxEnd"); o.num(b.fxEnd); o.raw(",");
        o.key("fxScale"); o.num(b.fxScale); o.raw(","); o.key("fxRate"); o.num(b.fxRate); o.raw(",");
        o.key("fxVar"); o.num(b.fxVariant); o.raw(","); o.key("fxPower"); o.num(b.fxPower); o.raw(","); o.key("fxPush"); o.raw(b.fxPush ? "true," : "false,");
        o.key("phys"); o.raw("{"); o.key("on"); o.raw(b.phys.enabled ? "true," : "false,"); o.key("kind"); o.num(b.phys.kind); o.raw(",");
        o.key("shape"); o.num(b.phys.shape); o.raw(","); o.key("mass"); o.num(b.phys.mass); o.raw(","); o.key("bounce"); o.num(b.phys.bounce); o.raw(",");
        o.key("fric"); o.num(b.phys.friction); o.raw(","); o.key("start"); o.num(b.phys.start); o.raw(","); o.key("inherit"); o.raw(b.phys.inherit ? "true," : "false,");
        o.key("vel"); o.vec(b.phys.vel); o.raw(","); o.key("spin"); o.vec(b.phys.spin); o.raw("},");
        o.key("bones"); o.raw("[");
        for (size_t k = 0; k < b.boneTracks.size(); k++) {
            const BoneTrack& tr = b.boneTracks[k];
            if (k) o.raw(",");
            o.raw("{"); o.key("n"); o.num(tr.node); o.raw(","); o.key("k"); o.raw("[");
            for (size_t q = 0; q < tr.keys.size(); q++) {
                const BoneKey& bk = tr.keys[q];
                if (q) o.raw(",");
                o.raw("{"); o.key("f"); o.num(bk.frame); o.raw(","); o.key("t"); o.vec(bk.t); o.raw(","); o.key("q"); o.raw("[");
                o.num(bk.r.x); o.raw(","); o.num(bk.r.y); o.raw(","); o.num(bk.r.z); o.raw(","); o.num(bk.r.w); o.raw("],"); o.key("e"); o.num(bk.ease); o.raw("}");
            }
            o.raw("]}");
        }
        o.raw("]");
        o.raw("}");
    }
    o.raw("]}");
    return o.s;
}

bool Scene::applyScene(const Json& j, std::string* err) {
    if (j.type != Json::Obj) { if (err) *err = "не сцена"; return false; }
    std::vector<std::unique_ptr<Object>> newObjs;
    for (size_t i = 0; i < j["objects"].size(); i++) {
        const Json& b = j["objects"][i];
        std::unique_ptr<Object> o(new Object());
        o->id = b["id"].integer(0);
        o->name = b["name"].string();
        o->type = (ObjType)clampf((float)b["type"].integer(0), 0, 3);
        o->visible = jbool(b["visible"], true);
        o->t = jvec(b["t"]); o->r = jvec(b["r"]); o->s = jvec(b["s"], {1, 1, 1});
        for (size_t k = 0; k < b["keys"].size(); k++) {
            const Json& e = b["keys"][k];
            TKey tk; tk.frame = e["f"].integer(1); tk.t = jvec(e["t"]); tk.r = jvec(e["r"]); tk.s = jvec(e["s"], {1, 1, 1}); tk.ease = e["e"].integer(1);
            o->keys.push_back(tk);
        }
        sortKeys(o->keys);
        for (size_t k = 0; k < b["parents"].size(); k++) {
            const Json& e = b["parents"][k];
            ParentKey pk; pk.frame = e["f"].integer(1); pk.parent = e["p"].integer(-1); pk.node = e["n"].integer(-1);
            if (e["m"].size() == 16) for (int q = 0; q < 16; q++) pk.offset.m[q] = (float)e["m"][q].number();
            o->parents.push_back(pk);
        }
        for (size_t k = 0; k < b["clips"].size(); k++) {
            const Json& e = b["clips"][k];
            ClipKey ck; ck.frame = e["f"].integer(1); ck.clip = e["c"].integer(-1); ck.speed = (float)e["sp"].number(1); ck.loop = jbool(e["l"], true);
            o->clipKeys.push_back(ck);
        }
        for (size_t k = 0; k < b["vis"].size(); k++) {
            VisKey vk; vk.frame = b["vis"][k]["f"].integer(1); vk.visible = jbool(b["vis"][k]["v"], true);
            o->visKeys.push_back(vk);
        }
        o->model = b["model"].integer(-1);
        if (b["fit"].size() == 16) for (int q = 0; q < 16; q++) o->fit.m[q] = (float)b["fit"][q].number();
        o->matOverride = jbool(b["matOverride"], false);
        jarr3(b["color"], o->color);
        o->metallic = (float)b["metallic"].number(0); o->roughness = (float)b["roughness"].number(0.5); o->emission = (float)b["emission"].number(0);
        o->castShadow = jbool(b["castShadow"], true);
        o->light = (LightKind)b["light"].integer(1);
        jarr3(b["lcolor"], o->lcolor);
        o->energy = (float)b["energy"].number(20); o->lradius = (float)b["lradius"].number(8);
        o->fovDeg = (float)b["fov"].number(40);
        o->fx = (FxKind)clampf((float)b["fx"].integer(0), 0, (float)(kFxKinds - 1));
        o->fxVariant = b["fxVar"].integer(0); o->fxPower = (float)b["fxPower"].number(1); o->fxPush = jbool(b["fxPush"], true);
        {
            const Json& pj = b["phys"];
            o->phys.enabled = jbool(pj["on"], false); o->phys.kind = pj["kind"].integer(0); o->phys.shape = pj["shape"].integer(0);
            o->phys.mass = (float)pj["mass"].number(1); o->phys.bounce = (float)pj["bounce"].number(0.3); o->phys.friction = (float)pj["fric"].number(0.6);
            o->phys.start = pj["start"].integer(1); o->phys.inherit = jbool(pj["inherit"], true);
            o->phys.vel = jvec(pj["vel"]); o->phys.spin = jvec(pj["spin"]);
            for (size_t k = 0; k < b["bones"].size(); k++) {
                const Json& e = b["bones"][k];
                BoneTrack tr; tr.node = e["n"].integer(-1);
                for (size_t q = 0; q < e["k"].size(); q++) {
                    const Json& kk = e["k"][q];
                    BoneKey bk; bk.frame = kk["f"].integer(1); bk.t = jvec(kk["t"]); bk.ease = kk["e"].integer(1);
                    if (kk["q"].size() == 4) bk.r = normalize(Quat((float)kk["q"][0].number(), (float)kk["q"][1].number(), (float)kk["q"][2].number(), (float)kk["q"][3].number()));
                    tr.keys.push_back(bk);
                }
                if (tr.node >= 0 && !tr.keys.empty()) o->boneTracks.push_back(tr);
            }
        }
        o->fxStart = b["fxStart"].integer(1); o->fxEnd = b["fxEnd"].integer(120); o->fxScale = (float)b["fxScale"].number(1); o->fxRate = b["fxRate"].integer(0);
        if (o->type == ObjType::Model && (o->model < 0 || o->model >= (int)models.size())) continue;  // модель потеряна
        newObjs.push_back(std::move(o));
    }
    objects = std::move(newObjs);
    name = j["name"].string().empty() ? name : j["name"].string();
    nextId_ = std::max(nextId_, j["nextId"].integer(1));
    for (auto& o : objects) nextId_ = std::max(nextId_, o->id + 1);
    selected = find(j["selected"].integer(-1)) ? j["selected"].integer(-1) : -1;
    activeCamera = find(j["activeCamera"].integer(-1)) ? j["activeCamera"].integer(-1) : -1;
    if (activeCamera < 0) for (auto& o : objects) if (o->type == ObjType::Camera) { activeCamera = o->id; break; }
    autoKey = jbool(j["autoKey"], false);
    const Json& p = j["proj"];
    proj.fps = std::max(1, p["fps"].integer(24)); proj.frameStart = p["start"].integer(1); proj.frameEnd = std::max(proj.frameStart, p["end"].integer(120));
    const Json& r = j["render"];
    RenderSettings d;
    render = d;
    render.exposure = (float)r["exposure"].number(d.exposure); render.contrast = (float)r["contrast"].number(d.contrast); render.saturation = (float)r["saturation"].number(d.saturation);
    render.ao = jbool(r["ao"], d.ao); render.aoStrength = (float)r["aoStrength"].number(d.aoStrength);
    render.bloom = jbool(r["bloom"], d.bloom); render.bloomStrength = (float)r["bloomStrength"].number(d.bloomStrength);
    render.shadows = jbool(r["shadows"], d.shadows); render.shadowSoft = (float)r["shadowSoft"].number(d.shadowSoft); render.contactShadows = jbool(r["contact"], d.contactShadows);
    render.skyBrightness = (float)r["skyBrightness"].number(d.skyBrightness);
    jarr3(r["skyTop"], render.skyTop); jarr3(r["skyHorizon"], render.skyHorizon); jarr3(r["skyGround"], render.skyGround);
    render.floor = jbool(r["floor"], d.floor); jarr3(r["floorColor"], render.floorColor); render.floorRoughness = (float)r["floorRoughness"].number(d.floorRoughness);
    render.vignette = jbool(r["vignette"], d.vignette);
    render.exportW = r["exportW"].integer(d.exportW); render.exportH = r["exportH"].integer(d.exportH);
    render.stillSamples = r["stillSamples"].integer(d.stillSamples); render.videoSamples = r["videoSamples"].integer(d.videoSamples);
    render.videoBitrateMbps = r["videoBitrate"].integer(d.videoBitrateMbps);
    frame_ = (float)j["frame"].number(proj.frameStart);
    resetEffects();
    evaluate(frame_);
    return true;
}

bool Scene::fromJson(const std::string& text, std::string* err) {
    Json j;
    JsonParser p(text.data(), text.size());
    if (!p.parse(j)) { if (err) *err = "повреждённый JSON"; return false; }
    return applyScene(j, err);
}

std::string Scene::saveProject() const {
    Out o;
    o.raw("{\"format\":\"prismlab\",\"version\":1,\"models\":[");
    for (size_t i = 0; i < models.size(); i++) {
        if (i) o.raw(",");
        o.raw("{\"source\":"); o.str(models[i].source); o.raw("}");
    }
    o.raw("],\"scene\":");
    o.raw(toJson());
    o.raw("}");
    return o.s;
}

bool Scene::loadProject(const std::string& text, std::string* err) {
    Json j;
    JsonParser p(text.data(), text.size());
    if (!p.parse(j) || j["format"].string() != "prismlab") { if (err) *err = "это не проект PrismLab"; return false; }
    models.clear();
    for (size_t i = 0; i < j["models"].size(); i++) {
        ModelEntry e;
        e.source = j["models"][i]["source"].string();
        if (e.source.rfind("prim:", 0) == 0) e.model = primitiveModel(e.source);
        else if (modelLoader) e.model = modelLoader(e.source);
        if (!e.model) { e.model.reset(new Model()); e.model->name = "(не найдена)"; }
        models.push_back(std::move(e));
    }
    undo_.clear(); redo_.clear();
    nextId_ = 1;
    return applyScene(j["scene"], err);
}

void Scene::pushUndo() {
    undo_.push_back(toJson());
    if (undo_.size() > 40) undo_.erase(undo_.begin());
    redo_.clear();
}

bool Scene::undo() {
    if (undo_.empty()) return false;
    redo_.push_back(toJson());
    std::string s = undo_.back();
    undo_.pop_back();
    float f = frame_;
    bool ok = fromJson(s);
    evaluate(f);
    return ok;
}

bool Scene::redo() {
    if (redo_.empty()) return false;
    undo_.push_back(toJson());
    std::string s = redo_.back();
    redo_.pop_back();
    float f = frame_;
    bool ok = fromJson(s);
    evaluate(f);
    return ok;
}

}  // namespace eng
