#include "glb_export.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "png_writer.h"

namespace eng {
namespace {

std::string num(double v) {
    if (!std::isfinite(v)) v = 0;
    char b[40];
    std::snprintf(b, sizeof b, "%.9g", v);
    return b;
}

std::string esc(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o;
}

std::string vec3s(const Vec3& v) { return "[" + num(v.x) + "," + num(v.y) + "," + num(v.z) + "]"; }
std::string quats(const Quat& q) { return "[" + num(q.x) + "," + num(q.y) + "," + num(q.z) + "," + num(q.w) + "]"; }

struct Writer {
    std::vector<uint8_t> bin;
    std::string views, accs;
    int nViews = 0, nAccs = 0;

    int view(const void* d, size_t n, int target = 0) {
        while (bin.size() % 4) bin.push_back(0);
        size_t off = bin.size();
        const uint8_t* p = (const uint8_t*)d;
        bin.insert(bin.end(), p, p + n);
        if (nViews) views += ",";
        views += "{\"buffer\":0,\"byteOffset\":" + std::to_string(off) + ",\"byteLength\":" + std::to_string(n);
        if (target) views += ",\"target\":" + std::to_string(target);
        views += "}";
        return nViews++;
    }
    int acc(int bv, int comp, size_t count, const char* type, bool norm = false, const std::string& mn = "", const std::string& mx = "") {
        if (nAccs) accs += ",";
        accs += "{\"bufferView\":" + std::to_string(bv) + ",\"componentType\":" + std::to_string(comp) + ",\"count\":" + std::to_string(count) +
                ",\"type\":\"" + type + "\"";
        if (norm) accs += ",\"normalized\":true";
        if (!mn.empty()) accs += ",\"min\":" + mn + ",\"max\":" + mx;
        accs += "}";
        return nAccs++;
    }
};

constexpr int kFloat = 5126, kUByte = 5121, kUInt = 5125;

}  // namespace

bool exportSkeletalGlb(Scene& sc, int objectId, const GlbOptions& opt, std::vector<uint8_t>& out, std::string& err) {
    Object* o = sc.find(objectId);
    if (!o || o->type != ObjType::Model || o->model < 0 || o->model >= (int)sc.models.size() || !sc.models[o->model].model) {
        err = "Выберите модель со скелетом";
        return false;
    }
    const Model& m = *sc.models[o->model].model;
    const int N = (int)m.nodes.size();
    if (N == 0) { err = "В модели нет узлов"; return false; }
    int f0 = std::min(opt.f0, opt.f1), f1 = std::max(opt.f0, opt.f1);
    f1 = std::min(f1, f0 + 6000);
    const int F = f1 - f0 + 1;
    const int fps = std::max(1, opt.fps);
    if ((long long)N * F > 3000000LL) { err = "Слишком много данных: сократите диапазон кадров"; return false; }

    // ---- запекание поз ----
    const float keepFrame = sc.frame();
    std::vector<std::vector<Vec3>> T(N, std::vector<Vec3>((size_t)F)), S(N, std::vector<Vec3>((size_t)F));
    std::vector<std::vector<Quat>> R(N, std::vector<Quat>((size_t)F));
    std::vector<Vec3> rT((size_t)F), rS((size_t)F);
    std::vector<Quat> rR((size_t)F);
    for (int k = 0; k < F; k++) {
        sc.evaluate((float)(f0 + k));
        o = sc.find(objectId);
        if (!o || !o->anim || (int)o->anim->world.size() < N) { sc.evaluate(keepFrame); err = "Нет позы скелета"; return false; }
        const auto& W = o->anim->world;
        for (int i = 0; i < N; i++) {
            Mat4 loc = W[(size_t)i];
            int p = m.nodes[(size_t)i].parent;
            if (p >= 0 && p < N) loc = inverse(W[(size_t)p]) * W[(size_t)i];
            Vec3 t, s;
            Quat q;
            decompose(loc, t, q, s);
            if (k > 0 && dot(q, R[i][(size_t)k - 1]) < 0) q = Quat(-q.x, -q.y, -q.z, -q.w);  // без «прокрутки» на стыках
            T[i][(size_t)k] = t; R[i][(size_t)k] = q; S[i][(size_t)k] = s;
        }
        if (opt.rootMotion) {
            Vec3 t, s;
            Quat q;
            decompose(o->world * o->fit, t, q, s);
            if (k > 0 && dot(q, rR[(size_t)k - 1]) < 0) q = Quat(-q.x, -q.y, -q.z, -q.w);
            rT[(size_t)k] = t; rR[(size_t)k] = q; rS[(size_t)k] = s;
        }
    }
    sc.evaluate(keepFrame);

    Writer w;
    std::string jMeshes, jMaterials, jTextures, jImages, jSkins, jNodes, jAnims;
    int nMeshes = 0, nMaterials = 0;

    // ---- текстуры и материалы ----
    std::vector<int> texOk(m.textures.size(), 0);
    int nTex = 0;
    if (opt.mesh) {
        for (size_t i = 0; i < m.textures.size(); i++) {
            const Texture& t = m.textures[i];
            std::vector<uint8_t> png;
            if (t.w > 0 && t.h > 0 && t.rgba.size() >= (size_t)t.w * t.h * 4 && encodePNG(t.rgba.data(), t.w, t.h, png, false)) texOk[i] = 1;
            if (jImages.size()) jImages += ",";
            if (texOk[i]) {
                int bv = w.view(png.data(), png.size());
                jImages += "{\"bufferView\":" + std::to_string(bv) + ",\"mimeType\":\"image/png\"}";
            } else {
                static const uint8_t px[4] = {255, 255, 255, 255};
                std::vector<uint8_t> one;
                encodePNG(px, 1, 1, one, false);
                int bv = w.view(one.data(), one.size());
                jImages += "{\"bufferView\":" + std::to_string(bv) + ",\"mimeType\":\"image/png\"}";
            }
            if (jTextures.size()) jTextures += ",";
            jTextures += "{\"sampler\":0,\"source\":" + std::to_string(i) + "}";
            nTex++;
        }
    }
    auto texRef = [&](int idx) { return idx >= 0 && idx < nTex; };

    // ---- меши ----
    struct Attach { int node, mesh, skin; };
    std::vector<Attach> attach;  // (узел модели, glTF-меш, скин)
    if (opt.mesh) {
        for (int ni = 0; ni < N; ni++)
            for (int mi : m.nodes[(size_t)ni].meshes) {
                if (mi < 0 || mi >= (int)m.meshes.size()) continue;
                const MeshData& md = m.meshes[(size_t)mi];
                if (md.verts.empty() || md.indices.empty()) continue;
                const size_t V = md.verts.size();
                std::vector<float> pos(V * 3), nrm(V * 3), uv(V * 2), wt(V * 4);
                std::vector<uint8_t> col(V * 4), jn(V * 4);
                bool hasCol = false;
                Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
                for (size_t v = 0; v < V; v++) {
                    const Vertex& x = md.verts[v];
                    for (int c = 0; c < 3; c++) { pos[v * 3 + c] = x.p[c]; nrm[v * 3 + c] = x.n[c]; }
                    uv[v * 2] = x.uv[0]; uv[v * 2 + 1] = x.uv[1];
                    for (int c = 0; c < 4; c++) { col[v * 4 + c] = x.c[c]; jn[v * 4 + c] = x.j[c]; wt[v * 4 + c] = x.w[c]; if (x.c[c] != 255) hasCol = true; }
                    lo = {std::min(lo.x, x.p[0]), std::min(lo.y, x.p[1]), std::min(lo.z, x.p[2])};
                    hi = {std::max(hi.x, x.p[0]), std::max(hi.y, x.p[1]), std::max(hi.z, x.p[2])};
                }
                const bool skinned = md.skin >= 0 && md.skin < (int)m.skins.size();
                std::string attrs = "\"POSITION\":" + std::to_string(w.acc(w.view(pos.data(), pos.size() * 4, 34962), kFloat, V, "VEC3", false,
                                                                           vec3s(lo), vec3s(hi)));
                attrs += ",\"NORMAL\":" + std::to_string(w.acc(w.view(nrm.data(), nrm.size() * 4, 34962), kFloat, V, "VEC3"));
                attrs += ",\"TEXCOORD_0\":" + std::to_string(w.acc(w.view(uv.data(), uv.size() * 4, 34962), kFloat, V, "VEC2"));
                if (hasCol) attrs += ",\"COLOR_0\":" + std::to_string(w.acc(w.view(col.data(), col.size(), 34962), kUByte, V, "VEC4", true));
                if (skinned) {
                    attrs += ",\"JOINTS_0\":" + std::to_string(w.acc(w.view(jn.data(), jn.size(), 34962), kUByte, V, "VEC4"));
                    attrs += ",\"WEIGHTS_0\":" + std::to_string(w.acc(w.view(wt.data(), wt.size() * 4, 34962), kFloat, V, "VEC4"));
                }
                int idxAcc = w.acc(w.view(md.indices.data(), md.indices.size() * 4, 34963), kUInt, md.indices.size(), "SCALAR");

                // материал
                std::string mat = "{\"name\":\"" + esc(md.name.empty() ? "mat" + std::to_string(mi) : md.name) + "\",\"pbrMetallicRoughness\":{";
                mat += "\"baseColorFactor\":[1,1,1,1],\"metallicFactor\":" + num(md.metallic) + ",\"roughnessFactor\":" + num(md.roughness);
                if (texRef(md.texture)) mat += ",\"baseColorTexture\":{\"index\":" + std::to_string(md.texture) + "}";
                if (texRef(md.texMR)) mat += ",\"metallicRoughnessTexture\":{\"index\":" + std::to_string(md.texMR) + "}";
                mat += "}";
                if (texRef(md.texNormal)) mat += ",\"normalTexture\":{\"index\":" + std::to_string(md.texNormal) + ",\"scale\":" + num(md.normalScale) + "}";
                if (texRef(md.texOcclusion)) mat += ",\"occlusionTexture\":{\"index\":" + std::to_string(md.texOcclusion) + "}";
                if (texRef(md.texEmissive) || md.emissive[0] + md.emissive[1] + md.emissive[2] > 0)
                    mat += ",\"emissiveFactor\":[" + num(texRef(md.texEmissive) ? 1.0 : md.emissive[0]) + "," + num(texRef(md.texEmissive) ? 1.0 : md.emissive[1]) + "," +
                           num(texRef(md.texEmissive) ? 1.0 : md.emissive[2]) + "]";
                if (texRef(md.texEmissive)) mat += ",\"emissiveTexture\":{\"index\":" + std::to_string(md.texEmissive) + "}";
                mat += std::string(",\"alphaMode\":\"") + (md.alphaMode == 1 ? "MASK" : md.alphaMode == 2 ? "BLEND" : "OPAQUE") + "\"";
                if (md.alphaMode == 1) mat += ",\"alphaCutoff\":" + num(md.alphaCutoff);
                if (md.doubleSided) mat += ",\"doubleSided\":true";
                mat += "}";
                if (jMaterials.size()) jMaterials += ",";
                jMaterials += mat;
                int matIdx = nMaterials++;

                if (jMeshes.size()) jMeshes += ",";
                jMeshes += "{\"name\":\"" + esc(md.name.empty() ? "mesh" + std::to_string(mi) : md.name) + "\",\"primitives\":[{\"attributes\":{" + attrs + "},\"indices\":" +
                           std::to_string(idxAcc) + ",\"material\":" + std::to_string(matIdx) + ",\"mode\":4}]}";
                attach.push_back({ni, nMeshes++, skinned ? md.skin : -1});
            }
        // скины
        for (size_t si = 0; si < m.skins.size(); si++) {
            const Skin& sk = m.skins[si];
            std::vector<float> ib(sk.joints.size() * 16);
            for (size_t j = 0; j < sk.joints.size(); j++) {
                Mat4 b = j < sk.invBind.size() ? sk.invBind[j] : Mat4();
                std::memcpy(&ib[j * 16], b.m, sizeof b.m);
            }
            std::string js;
            for (size_t j = 0; j < sk.joints.size(); j++) js += (j ? "," : "") + std::to_string(std::max(0, std::min(N - 1, sk.joints[j])));
            if (jSkins.size()) jSkins += ",";
            jSkins += "{\"inverseBindMatrices\":" + std::to_string(w.acc(w.view(ib.data(), ib.size() * 4), kFloat, sk.joints.size(), "MAT4")) + ",\"joints\":[" + js + "]}";
        }
    }

    // ---- узлы ----
    const int rootIdx = opt.rootMotion ? N : -1;
    std::vector<std::vector<int>> kids((size_t)N + (opt.rootMotion ? 1 : 0));
    std::vector<int> topNodes;
    for (int i = 0; i < N; i++) {
        int p = m.nodes[(size_t)i].parent;
        if (p >= 0 && p < N) kids[(size_t)p].push_back(i);
        else if (opt.rootMotion) kids[(size_t)N].push_back(i);
        else topNodes.push_back(i);
    }
    // меши: первый меш узла - на самом узле, остальные - дочерними узлами
    struct Extra { int mesh, skin; int parent; };
    std::vector<int> nodeMesh((size_t)N, -1), nodeSkin((size_t)N, -1);
    std::vector<std::string> extraNodes;
    int nextIdx = N + (opt.rootMotion ? 1 : 0);
    for (const Attach& a : attach) {
        if (nodeMesh[(size_t)a.node] < 0 && (nodeSkin[(size_t)a.node] < 0 || nodeSkin[(size_t)a.node] == a.skin)) {
            nodeMesh[(size_t)a.node] = a.mesh;
            nodeSkin[(size_t)a.node] = a.skin;
        } else {
            int idx = nextIdx++;
            std::string e = "{\"name\":\"" + esc(m.nodes[(size_t)a.node].name + "_m" + std::to_string(a.mesh)) + "\",\"mesh\":" + std::to_string(a.mesh);
            if (a.skin >= 0) e += ",\"skin\":" + std::to_string(a.skin);
            e += "}";
            extraNodes.push_back(e);
            kids[(size_t)a.node].push_back(idx);
        }
    }
    auto childList = [&](const std::vector<int>& k) {
        std::string s;
        for (size_t i = 0; i < k.size(); i++) s += (i ? "," : "") + std::to_string(k[i]);
        return s;
    };
    for (int i = 0; i < N; i++) {
        const Node& nd = m.nodes[(size_t)i];
        std::string e = "{\"name\":\"" + esc(nd.name.empty() ? "node" + std::to_string(i) : nd.name) + "\"";
        if (!kids[(size_t)i].empty()) e += ",\"children\":[" + childList(kids[(size_t)i]) + "]";
        if (nodeMesh[(size_t)i] >= 0) e += ",\"mesh\":" + std::to_string(nodeMesh[(size_t)i]);
        if (nodeSkin[(size_t)i] >= 0) e += ",\"skin\":" + std::to_string(nodeSkin[(size_t)i]);
        e += ",\"translation\":" + vec3s(nd.t) + ",\"rotation\":" + quats(normalize(nd.r)) + ",\"scale\":" + vec3s(nd.s) + "}";
        if (jNodes.size()) jNodes += ",";
        jNodes += e;
    }
    if (opt.rootMotion) {
        jNodes += ",{\"name\":\"Root\"";
        if (!kids[(size_t)N].empty()) jNodes += ",\"children\":[" + childList(kids[(size_t)N]) + "]";
        jNodes += ",\"translation\":" + vec3s(rT[0]) + ",\"rotation\":" + quats(rR[0]) + ",\"scale\":" + vec3s(rS[0]) + "}";
    }
    for (auto& e : extraNodes) jNodes += "," + e;

    // ---- анимация ----
    {
        std::vector<float> times((size_t)F);
        for (int k = 0; k < F; k++) times[(size_t)k] = (float)k / (float)fps;
        int timeAcc = w.acc(w.view(times.data(), times.size() * 4), kFloat, (size_t)F, "SCALAR", false, "[" + num(times.front()) + "]", "[" + num(times.back()) + "]");
        std::string samplers, channels;
        int nS = 0;
        auto addChan = [&](int node, const char* path, const std::vector<float>& data, const char* type) {
            int out = w.acc(w.view(data.data(), data.size() * 4), kFloat, (size_t)F, type);
            if (nS) { samplers += ","; channels += ","; }
            samplers += "{\"input\":" + std::to_string(timeAcc) + ",\"output\":" + std::to_string(out) + ",\"interpolation\":\"LINEAR\"}";
            channels += "{\"sampler\":" + std::to_string(nS) + ",\"target\":{\"node\":" + std::to_string(node) + ",\"path\":\"" + path + "\"}}";
            nS++;
        };
        auto genChans = [&](int node, const Vec3& bt, const Quat& bq, const Vec3& bs, const std::vector<Vec3>& t, const std::vector<Quat>& q, const std::vector<Vec3>& s, bool force) {
            bool vt = force, vq = force, vs = force;
            for (int k = 0; k < F; k++) {
                if (length(t[(size_t)k] - bt) > 1e-5f) vt = true;
                if (std::fabs(dot(q[(size_t)k], bq)) < 1.f - 1e-7f) vq = true;
                if (length(s[(size_t)k] - bs) > 1e-5f) vs = true;
            }
            if (vt) { std::vector<float> d; for (auto& x : t) { d.push_back(x.x); d.push_back(x.y); d.push_back(x.z); } addChan(node, "translation", d, "VEC3"); }
            if (vq) { std::vector<float> d; for (auto& x : q) { d.push_back(x.x); d.push_back(x.y); d.push_back(x.z); d.push_back(x.w); } addChan(node, "rotation", d, "VEC4"); }
            if (vs) { std::vector<float> d; for (auto& x : s) { d.push_back(x.x); d.push_back(x.y); d.push_back(x.z); } addChan(node, "scale", d, "VEC3"); }
        };
        for (int i = 0; i < N; i++) {
            const Node& nd = m.nodes[(size_t)i];
            genChans(i, nd.t, normalize(nd.r), nd.s, T[(size_t)i], R[(size_t)i], S[(size_t)i], false);
        }
        if (opt.rootMotion) genChans(rootIdx, rT[0], rR[0], rS[0], rT, rR, rS, false);
        if (nS == 0) {  // нечего анимировать: всё равно пишем один канал, чтобы клип существовал
            std::vector<float> d;
            for (int k = 0; k < F; k++) { d.push_back(T[0][(size_t)k].x); d.push_back(T[0][(size_t)k].y); d.push_back(T[0][(size_t)k].z); }
            addChan(0, "translation", d, "VEC3");
        }
        jAnims = "{\"name\":\"" + esc(opt.animName.empty() ? "PrismLab" : opt.animName) + "\",\"samplers\":[" + samplers + "],\"channels\":[" + channels + "]}";
    }

    // ---- сборка JSON ----
    std::string top;
    if (opt.rootMotion) top = std::to_string(N);
    else top = childList(topNodes);
    std::string j = "{\"asset\":{\"version\":\"2.0\",\"generator\":\"PrismLab\"},\"scene\":0,\"scenes\":[{\"name\":\"" + esc(sc.name) + "\",\"nodes\":[" + top + "]}],\"nodes\":[" + jNodes + "]";
    if (!jMeshes.empty()) j += ",\"meshes\":[" + jMeshes + "]";
    if (!jSkins.empty()) j += ",\"skins\":[" + jSkins + "]";
    if (!jMaterials.empty()) j += ",\"materials\":[" + jMaterials + "]";
    if (!jTextures.empty()) j += ",\"textures\":[" + jTextures + "],\"images\":[" + jImages + "],\"samplers\":[{\"magFilter\":9729,\"minFilter\":9987,\"wrapS\":10497,\"wrapT\":10497}]";
    j += ",\"animations\":[" + jAnims + "],\"accessors\":[" + w.accs + "],\"bufferViews\":[" + w.views + "],\"buffers\":[{\"byteLength\":" + std::to_string(w.bin.size()) + "}]}";

    // ---- контейнер GLB ----
    std::string js = j;
    while (js.size() % 4) js += ' ';
    while (w.bin.size() % 4) w.bin.push_back(0);
    uint32_t total = 12 + 8 + (uint32_t)js.size() + 8 + (uint32_t)w.bin.size();
    out.clear();
    out.reserve(total);
    auto u32 = [&](uint32_t v) { for (int b = 0; b < 4; b++) out.push_back((uint8_t)(v >> (8 * b))); };
    u32(0x46546C67); u32(2); u32(total);
    u32((uint32_t)js.size()); u32(0x4E4F534A);
    out.insert(out.end(), js.begin(), js.end());
    u32((uint32_t)w.bin.size()); u32(0x004E4942);
    out.insert(out.end(), w.bin.begin(), w.bin.end());
    return true;
}

}  // namespace eng
