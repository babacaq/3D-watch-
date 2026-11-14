// Единая точка импорта моделей + OBJ (с MTL) + STL.
#include "importer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <unordered_map>

#include "gltf.h"
#include "image.h"
#include "log.h"

namespace eng {

// ------------------------- помощники -------------------------
static std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
static std::string extOf(const std::string& n) {
    size_t d = n.find_last_of('.');
    return d == std::string::npos ? "" : lower(n.substr(d + 1));
}
static std::string baseName(const std::string& n) {
    std::string s = n;
    for (auto& c : s) if (c == '\\') c = '/';
    size_t sl = s.find_last_of('/');
    return sl == std::string::npos ? s : s.substr(sl + 1);
}

bool isSupportedModelFile(const std::string& fileName) {
    std::string e = extOf(fileName);
    return e == "glb" || e == "gltf" || e == "obj" || e == "stl";
}

int addTextureFromMemory(Model& model, const uint8_t* data, size_t size, std::string* err) {
    Image img;
    if (!decodeImage(data, size, img, err)) return -1;
    downscaleToMax(img, kMaxTextureDim);
    Texture t;
    t.w = img.w; t.h = img.h; t.rgba = std::move(img.rgba);
    model.textures.push_back(std::move(t));
    return (int)model.textures.size() - 1;
}

void computeSmoothNormals(MeshData& m, bool onlyZero) {
    struct Key { int x, y, z; bool operator==(const Key& o) const { return x == o.x && y == o.y && z == o.z; } };
    struct KH { size_t operator()(const Key& k) const { return ((size_t)k.x * 73856093u) ^ ((size_t)k.y * 19349663u) ^ ((size_t)k.z * 83492791u); } };
    std::unordered_map<Key, Vec3, KH> acc;
    auto key = [](const Vertex& v) { return Key{(int)std::lround(v.p[0] * 10000.f), (int)std::lround(v.p[1] * 10000.f), (int)std::lround(v.p[2] * 10000.f)}; };
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const Vertex &a = m.verts[m.indices[i]], &b = m.verts[m.indices[i + 1]], &c = m.verts[m.indices[i + 2]];
        Vec3 pa(a.p[0], a.p[1], a.p[2]), pb(b.p[0], b.p[1], b.p[2]), pc(c.p[0], c.p[1], c.p[2]);
        Vec3 g = cross(pb - pa, pc - pa);  // длина = 2*площадь -> вес по площади
        for (const Vertex* v : {&a, &b, &c}) acc[key(*v)] += g;
    }
    for (auto& v : m.verts) {
        if (onlyZero && (v.n[0] != 0 || v.n[1] != 0 || v.n[2] != 0)) continue;
        Vec3 n = normalize(acc[key(v)]);
        v.n[0] = n.x; v.n[1] = n.y; v.n[2] = n.z;
    }
}

void breakNodeCycles(Model& model) {
    int n = (int)model.nodes.size();
    for (int i = 0; i < n; i++) {
        int p = model.nodes[i].parent, steps = 0;
        while (p >= 0 && p < n && steps <= n) { if (p == i) { model.nodes[i].parent = -1; break; } p = model.nodes[p].parent; steps++; }
        if (steps > n) model.nodes[i].parent = -1;
    }
}

bool looksLikeBinaryStl(const uint8_t* data, size_t size) {
    if (size < 84) return false;
    uint32_t tri;
    std::memcpy(&tri, data + 80, 4);
    return 84 + (size_t)tri * 50 == size;
}

// ------------------------- STL -------------------------
std::unique_ptr<Model> importStl(const std::string& name, const uint8_t* data, size_t size, std::string* err) {
    MeshData md;
    auto addTri = [&](Vec3 a, Vec3 b, Vec3 c, Vec3 n) {
        Vec3 g = cross(b - a, c - a);
        if (length(g) > 1e-20f) n = normalize(g);  // всегда по вершинам: нормаль файла часто неверна
        uint32_t base = (uint32_t)md.verts.size();
        for (Vec3 p : {a, b, c}) {
            Vertex v{};
            v.p[0] = p.x; v.p[1] = p.y; v.p[2] = p.z;
            v.n[0] = n.x; v.n[1] = n.y; v.n[2] = n.z;
            v.c[0] = v.c[1] = v.c[2] = 200; v.c[3] = 255;
            v.w[0] = 1;
            md.verts.push_back(v);
        }
        md.indices.insert(md.indices.end(), {base, base + 1, base + 2});
    };
    if (looksLikeBinaryStl(data, size)) {
        uint32_t tri;
        std::memcpy(&tri, data + 80, 4);
        md.verts.reserve((size_t)tri * 3);
        for (uint32_t i = 0; i < tri; i++) {
            float f[12];
            std::memcpy(f, data + 84 + (size_t)i * 50, 48);
            addTri({f[3], f[4], f[5]}, {f[6], f[7], f[8]}, {f[9], f[10], f[11]}, {f[0], f[1], f[2]});
        }
    } else {
        std::string text((const char*)data, size);
        const char* p = text.c_str();
        std::vector<Vec3> pts;
        Vec3 fn;
        while (*p) {
            while (*p && std::isspace((unsigned char)*p)) p++;
            if (!std::strncmp(p, "facet normal", 12)) {
                p += 12; char* e;
                fn.x = std::strtof(p, &e); p = e; fn.y = std::strtof(p, &e); p = e; fn.z = std::strtof(p, &e); p = e;
                pts.clear();
            } else if (!std::strncmp(p, "vertex", 6)) {
                p += 6; char* e; Vec3 v;
                v.x = std::strtof(p, &e); p = e; v.y = std::strtof(p, &e); p = e; v.z = std::strtof(p, &e); p = e;
                pts.push_back(v);
                if (pts.size() == 3) { addTri(pts[0], pts[1], pts[2], fn); pts.clear(); }
            } else {
                while (*p && *p != '\n') p++;
            }
        }
    }
    if (md.indices.empty()) { if (err) *err = "STL: no triangles"; return nullptr; }
    auto model = std::make_unique<Model>();
    model->name = name;
    Node root; root.name = "root"; root.meshes.push_back(0);
    model->nodes.push_back(root);
    md.doubleSided = true;
    model->meshes.push_back(std::move(md));
    model->finalize();
    return model;
}

// ------------------------- OBJ -------------------------
namespace {

struct Mtl {
    float kd[3] = {0.8f, 0.8f, 0.8f};
    float alpha = 1.f, ns = 32.f;
    std::string mapKd;
};

struct Cursor {
    const char* p; const char* end;
    bool eof() const { return p >= end; }
    void skipWs() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) p++; }
    void skipLine() { while (p < end && *p != '\n') p++; if (p < end) p++; }
    bool atEol() { skipWs(); return p >= end || *p == '\n'; }
    float num(float def = 0) {
        skipWs();
        if (p >= end || *p == '\n') return def;
        char buf[48]; int n = 0;
        while (p < end && n < 47 && !std::isspace((unsigned char)*p)) buf[n++] = *p++;
        buf[n] = 0;
        return std::strtof(buf, nullptr);
    }
    std::string word() {
        skipWs();
        std::string s;
        while (p < end && !std::isspace((unsigned char)*p)) s += *p++;
        return s;
    }
    std::string restOfLine() {
        skipWs();
        std::string s;
        while (p < end && *p != '\n' && *p != '\r') s += *p++;
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
        return s;
    }
};

void parseMtl(const std::vector<uint8_t>& data, std::map<std::string, Mtl>& out) {
    Cursor c{(const char*)data.data(), (const char*)data.data() + data.size()};
    Mtl* cur = nullptr;
    while (!c.eof()) {
        std::string k = c.word();
        if (k == "newmtl") { cur = &out[c.restOfLine()]; continue; }
        if (cur) {
            if (k == "Kd") { cur->kd[0] = c.num(); cur->kd[1] = c.num(); cur->kd[2] = c.num(); }
            else if (k == "d") cur->alpha = c.num(1);
            else if (k == "Tr") cur->alpha = 1.f - c.num(0);
            else if (k == "Ns") cur->ns = c.num(32);
            else if (k == "map_Kd" || k == "map_d") {
                std::string rest = c.restOfLine();
                // отбросить опции вида "-s 1 1 1": берём последний путь с расширением изображения
                std::string path = rest;
                size_t i = 0;
                while (i < rest.size() && rest[i] == '-') {
                    size_t sp = rest.find(' ', i);
                    if (sp == std::string::npos) break;
                    i = sp + 1;
                    while (i < rest.size() && (std::isdigit((unsigned char)rest[i]) || rest[i] == '.' || rest[i] == '-' || rest[i] == ' ')) {
                        if (rest[i] == '-' && i + 1 < rest.size() && std::isalpha((unsigned char)rest[i + 1])) break;
                        i++;
                    }
                }
                if (i < rest.size()) path = rest.substr(i);
                if (k == "map_Kd") cur->mapKd = path;
            }
        }
        c.skipLine();
    }
}

}  // namespace

std::unique_ptr<Model> importObj(const std::string& name, const uint8_t* data, size_t size, const FileReader& reader,
                                 std::string* err) {
    Cursor c{(const char*)data, (const char*)data + size};
    std::vector<Vec3> P, N;
    std::vector<float> T;               // пары u,v
    std::vector<uint8_t> VC;            // цвета вершин (3 байта на v), если заданы
    std::map<std::string, Mtl> mtls;
    struct Group {
        std::string mtl;
        MeshData md;
        std::unordered_map<uint64_t, uint32_t> remap;
        bool missingNormals = false;
    };
    std::vector<Group> groups;
    std::map<std::string, int> groupOf;
    int cur = -1;
    auto setMtl = [&](const std::string& m) {
        auto it = groupOf.find(m);
        if (it == groupOf.end()) { groups.emplace_back(); groups.back().mtl = m; it = groupOf.emplace(m, (int)groups.size() - 1).first; }
        cur = it->second;
    };
    setMtl("");
    std::vector<std::string> loadedMtl;

    auto resolveIdx = [](long i, size_t n) -> long { return i > 0 ? i - 1 : (i < 0 ? (long)n + i : -1); };
    std::vector<std::array<long, 3>> face;
    while (!c.eof()) {
        c.skipWs();
        if (c.p >= c.end) break;
        char ch = *c.p;
        if (ch == '\n') { c.p++; continue; }
        if (ch == '#') { c.skipLine(); continue; }
        std::string k = c.word();
        if (k == "v") {
            Vec3 v; v.x = c.num(); v.y = c.num(); v.z = c.num();
            P.push_back(v);
            if (!c.atEol()) {  // расширение: v x y z r g b
                float r = c.num(1), g = c.num(1), b = c.num(1);
                VC.resize(P.size() * 3, 255);
                VC[(P.size() - 1) * 3] = (uint8_t)(clampf(r, 0, 1) * 255); VC[(P.size() - 1) * 3 + 1] = (uint8_t)(clampf(g, 0, 1) * 255); VC[(P.size() - 1) * 3 + 2] = (uint8_t)(clampf(b, 0, 1) * 255);
            } else if (!VC.empty()) VC.resize(P.size() * 3, 255);
            c.skipLine();
        } else if (k == "vn") {
            Vec3 v; v.x = c.num(); v.y = c.num(); v.z = c.num(); N.push_back(v); c.skipLine();
        } else if (k == "vt") {
            float u = c.num(), v = c.num(); T.push_back(u); T.push_back(v); c.skipLine();
        } else if (k == "mtllib") {
            std::string f = c.restOfLine();
            if (reader && std::find(loadedMtl.begin(), loadedMtl.end(), f) == loadedMtl.end()) {
                loadedMtl.push_back(f);
                std::vector<uint8_t> bytes;
                std::string tryName = f;
                if (reader(tryName, bytes) || reader(baseName(f), bytes)) parseMtl(bytes, mtls);
                else ENG_LOG("OBJ: mtl '%s' not found", f.c_str());
            }
        } else if (k == "usemtl") {
            setMtl(c.restOfLine());
        } else if (k == "f") {
            face.clear();
            while (!c.atEol()) {
                c.skipWs();
                long vi = 0, ti = 0, ni = 0;
                char* e;
                const char* before = c.p;
                vi = std::strtol(c.p, &e, 10); c.p = e;
                if (c.p == before) { c.p++; continue; }
                if (c.p < c.end && *c.p == '/') {
                    c.p++;
                    if (c.p < c.end && *c.p != '/') { ti = std::strtol(c.p, &e, 10); c.p = e; }
                    if (c.p < c.end && *c.p == '/') { c.p++; ni = std::strtol(c.p, &e, 10); c.p = e; }
                }
                face.push_back({resolveIdx(vi, P.size()), ti ? resolveIdx(ti, T.size() / 2) : -1, ni ? resolveIdx(ni, N.size()) : -1});
            }
            c.skipLine();
            if (face.size() < 3) continue;
            Group& g = groups[cur];
            std::vector<uint32_t> ids;
            for (auto& f : face) {
                if (f[0] < 0 || f[0] >= (long)P.size()) { ids.clear(); break; }
                uint64_t key = (uint64_t)(f[0] + 1) * 1000003ull * 1000003ull + (uint64_t)(f[1] + 1) * 1000003ull + (uint64_t)(f[2] + 1);
                auto it = g.remap.find(key);
                if (it != g.remap.end()) { ids.push_back(it->second); continue; }
                Vertex v{};
                v.p[0] = P[f[0]].x; v.p[1] = P[f[0]].y; v.p[2] = P[f[0]].z;
                if (f[2] >= 0 && f[2] < (long)N.size()) { v.n[0] = N[f[2]].x; v.n[1] = N[f[2]].y; v.n[2] = N[f[2]].z; } else g.missingNormals = true;
                if (f[1] >= 0 && f[1] < (long)T.size() / 2) { v.uv[0] = T[f[1] * 2]; v.uv[1] = 1.f - T[f[1] * 2 + 1]; }
                v.c[0] = v.c[1] = v.c[2] = v.c[3] = 255;
                if (!VC.empty() && (size_t)f[0] * 3 + 2 < VC.size()) { v.c[0] = VC[f[0] * 3]; v.c[1] = VC[f[0] * 3 + 1]; v.c[2] = VC[f[0] * 3 + 2]; }
                v.w[0] = 1;
                uint32_t id = (uint32_t)g.md.verts.size();
                g.md.verts.push_back(v);
                g.remap.emplace(key, id);
                ids.push_back(id);
            }
            if (ids.size() < 3) continue;
            if (ids.size() == 3) { g.md.indices.insert(g.md.indices.end(), {ids[0], ids[1], ids[2]}); continue; }
            // многоугольник: нормаль Ньюэлла; выпуклый - веер, вогнутый - ear clipping
            std::vector<Vec3> pts;
            for (uint32_t id : ids) pts.push_back({g.md.verts[id].p[0], g.md.verts[id].p[1], g.md.verts[id].p[2]});
            Vec3 nrm;
            for (size_t i = 0; i < pts.size(); i++) {
                Vec3 a = pts[i], b = pts[(i + 1) % pts.size()];
                nrm += Vec3((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y));
            }
            nrm = normalize(nrm);
            std::vector<int> rem(ids.size());
            for (size_t i = 0; i < rem.size(); i++) rem[i] = (int)i;
            int guard = 0;
            while (rem.size() > 3 && guard++ < 10000) {
                bool clipped = false;
                for (size_t i = 0; i < rem.size(); i++) {
                    int ia = rem[(i + rem.size() - 1) % rem.size()], ib = rem[i], ic = rem[(i + 1) % rem.size()];
                    Vec3 a = pts[ia], b = pts[ib], cc = pts[ic];
                    if (dot(cross(b - a, cc - b), nrm) <= 1e-12f) continue;  // вогнутая вершина
                    bool inside = false;
                    for (int j : rem) {
                        if (j == ia || j == ib || j == ic) continue;
                        Vec3 p = pts[j];
                        if (dot(cross(b - a, p - a), nrm) >= 0 && dot(cross(cc - b, p - b), nrm) >= 0 && dot(cross(a - cc, p - cc), nrm) >= 0) { inside = true; break; }
                    }
                    if (inside) continue;
                    g.md.indices.insert(g.md.indices.end(), {ids[ia], ids[ib], ids[ic]});
                    rem.erase(rem.begin() + i);
                    clipped = true;
                    break;
                }
                if (!clipped) break;
            }
            if (rem.size() == 3) g.md.indices.insert(g.md.indices.end(), {ids[rem[0]], ids[rem[1]], ids[rem[2]]});
            else for (size_t i = 1; i + 1 < rem.size(); i++) g.md.indices.insert(g.md.indices.end(), {ids[rem[0]], ids[rem[i]], ids[rem[i + 1]]});
        } else {
            c.skipLine();
        }
    }

    auto model = std::make_unique<Model>();
    model->name = name;
    Node root; root.name = "root";
    std::map<std::string, int> texCache;
    for (auto& g : groups) {
        if (g.md.indices.empty()) continue;
        MeshData md = std::move(g.md);
        auto it = mtls.find(g.mtl);
        float alpha = 1.f;
        if (it != mtls.end()) {
            const Mtl& m = it->second;
            for (auto& v : md.verts) {
                v.c[0] = (uint8_t)(v.c[0] * clampf(m.kd[0], 0, 1)); v.c[1] = (uint8_t)(v.c[1] * clampf(m.kd[1], 0, 1)); v.c[2] = (uint8_t)(v.c[2] * clampf(m.kd[2], 0, 1));
            }
            md.shininess = clampf(m.ns, 4.f, 128.f);
            alpha = m.alpha;
            if (!m.mapKd.empty() && reader) {
                auto tc = texCache.find(m.mapKd);
                if (tc == texCache.end()) {
                    int ti = -1;
                    std::vector<uint8_t> bytes;
                    std::string pth = m.mapKd;
                    for (auto& ch : pth) if (ch == '\\') ch = '/';
                    if (reader(pth, bytes) || reader(baseName(pth), bytes)) ti = addTextureFromMemory(*model, bytes.data(), bytes.size(), nullptr);
                    if (ti < 0) ENG_LOG("OBJ: texture '%s' not loaded", m.mapKd.c_str());
                    tc = texCache.emplace(m.mapKd, ti).first;
                }
                md.texture = tc->second;
            }
        } else for (auto& v : md.verts) v.c[0] = v.c[1] = v.c[2] = v.c[0] == 255 ? 204 : v.c[0];
        if (alpha < 0.999f) { md.alphaMode = 2; for (auto& v : md.verts) v.c[3] = (uint8_t)(alpha * 255); }
        if (g.missingNormals) computeSmoothNormals(md, true);
        root.meshes.push_back((int)model->meshes.size());
        model->meshes.push_back(std::move(md));
    }
    if (model->meshes.empty()) { if (err) *err = "OBJ: no faces"; return nullptr; }
    model->nodes.push_back(root);
    model->finalize();
    return model;
}

// ------------------------- диспетчер -------------------------
std::unique_ptr<Model> importModel(const std::string& fileName, const uint8_t* data, size_t size, const FileReader& reader,
                                   std::string* err) {
    std::string base = baseName(fileName);
    std::string name = base;
    size_t d = name.find_last_of('.');
    if (d != std::string::npos) name = name.substr(0, d);
    for (auto& ch : name) ch = (char)std::toupper((unsigned char)ch);
    std::string ext = extOf(fileName);
    if (size >= 4 && !std::memcmp(data, "glTF", 4)) return loadGlb(data, size, name, err, reader);
    if (ext == "gltf") return loadGltf(data, size, name, err, reader);
    if (ext == "stl") return importStl(name, data, size, err);
    if (ext == "obj") return importObj(name, data, size, reader, err);
    if (ext == "glb") return loadGlb(data, size, name, err, reader);
    if (err) *err = "unsupported file type: ." + ext + " (supported: glb, gltf, obj, stl)";
    return nullptr;
}

}  // namespace eng
