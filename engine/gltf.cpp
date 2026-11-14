#include "gltf.h"

#include <cctype>
#include <cmath>
#include <cstring>

#include "geometry.h"
#include "json.h"
#include "log.h"

namespace eng {
namespace {

struct Blob { std::vector<uint8_t> own; const uint8_t* p = nullptr; size_t n = 0; };

int b64val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}
bool decodeDataUri(const std::string& uri, std::vector<uint8_t>& out) {
    size_t comma = uri.find(',');
    if (uri.compare(0, 5, "data:") != 0 || comma == std::string::npos) return false;
    if (uri.find(";base64") == std::string::npos || uri.find(";base64") > comma) return false;
    uint32_t acc = 0; int bits = 0;
    for (size_t i = comma + 1; i < uri.size(); i++) {
        int v = b64val(uri[i]);
        if (v < 0) continue;
        acc = (acc << 6) | (uint32_t)v; bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((uint8_t)((acc >> bits) & 0xFF)); }
    }
    return true;
}
std::string percentDecode(const std::string& s) {
    std::string r;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() + 0 && std::isxdigit((unsigned char)s[i + 1]) && std::isxdigit((unsigned char)s[i + 2])) {
            r += (char)std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else r += s[i];
    }
    return r;
}
// Загружает ресурс по uri: data: или внешний файл.
bool loadUri(const std::string& uri, const FileReader& reader, std::vector<uint8_t>& out) {
    if (uri.compare(0, 5, "data:") == 0) return decodeDataUri(uri, out);
    if (!reader) return false;
    return reader(percentDecode(uri), out);
}

struct Reader {
    const Json& root;
    std::vector<Blob>& bufs;

    const uint8_t* viewPtr(int bvIdx, size_t& size, size_t& stride) const {
        const Json& bv = root["bufferViews"][bvIdx];
        if (bv.isNull()) return nullptr;
        int bi = bv["buffer"].integer();
        if (bi < 0 || bi >= (int)bufs.size()) return nullptr;
        size_t off = (size_t)bv["byteOffset"].integer();
        size_t len = (size_t)bv["byteLength"].integer();
        if (off > bufs[bi].n) return nullptr;
        size = std::min(len, bufs[bi].n - off);
        stride = (size_t)bv["byteStride"].integer();
        return bufs[bi].p + off;
    }
    bool read(int accIdx, std::vector<float>& out, int& comps, int& count) const {
        const Json& acc = root["accessors"][accIdx];
        if (acc.isNull()) return false;
        count = acc["count"].integer();
        const std::string& type = acc["type"].string();
        comps = type == "SCALAR" ? 1 : type == "VEC2" ? 2 : type == "VEC3" ? 3 : type == "VEC4" ? 4
              : type == "MAT4" ? 16 : type == "MAT3" ? 9 : type == "MAT2" ? 4 : 0;
        if (!comps || count < 0 || count > 50000000) return false;
        int ct = acc["componentType"].integer();
        int csz = (ct == 5120 || ct == 5121) ? 1 : (ct == 5122 || ct == 5123) ? 2 : 4;
        bool norm = acc["normalized"].type == Json::Bool && acc["normalized"].b;
        out.assign((size_t)count * comps, 0.f);
        if (!acc.has("bufferView")) return true;
        size_t vsize, stride;
        const uint8_t* base = viewPtr(acc["bufferView"].integer(), vsize, stride);
        if (!base) return false;
        size_t off = (size_t)acc["byteOffset"].integer();
        size_t elem = (size_t)csz * comps;
        if (stride == 0) stride = elem;
        for (int i = 0; i < count; i++) {
            size_t b = off + (size_t)i * stride;
            if (b + elem > vsize) return false;
            for (int c = 0; c < comps; c++) {
                const uint8_t* p = base + b + (size_t)c * csz;
                float v = 0;
                switch (ct) {
                    case 5120: { int8_t x; std::memcpy(&x, p, 1); v = norm ? std::fmax(x / 127.f, -1.f) : x; break; }
                    case 5121: { v = norm ? *p / 255.f : *p; break; }
                    case 5122: { int16_t x; std::memcpy(&x, p, 2); v = norm ? std::fmax(x / 32767.f, -1.f) : x; break; }
                    case 5123: { uint16_t x; std::memcpy(&x, p, 2); v = norm ? x / 65535.f : x; break; }
                    case 5125: { uint32_t x; std::memcpy(&x, p, 4); v = (float)x; break; }
                    case 5126: { float x; std::memcpy(&x, p, 4); v = x; break; }
                    default: return false;
                }
                out[(size_t)i * comps + c] = v;
            }
        }
        return true;
    }
    bool readIndices(int accIdx, std::vector<uint32_t>& out) const {
        const Json& acc = root["accessors"][accIdx];
        if (acc.isNull() || !acc.has("bufferView")) return false;
        int count = acc["count"].integer(), ct = acc["componentType"].integer();
        size_t vsize, stride;
        const uint8_t* base = viewPtr(acc["bufferView"].integer(), vsize, stride);
        if (!base || count < 0) return false;
        size_t off = (size_t)acc["byteOffset"].integer();
        size_t csz = ct == 5121 ? 1 : ct == 5123 ? 2 : 4;
        if (stride == 0) stride = csz;
        out.resize(count);
        for (int i = 0; i < count; i++) {
            size_t b = off + (size_t)i * stride;
            if (b + csz > vsize) return false;
            if (ct == 5121) out[i] = base[b];
            else if (ct == 5123) { uint16_t x; std::memcpy(&x, base + b, 2); out[i] = x; }
            else { uint32_t x; std::memcpy(&x, base + b, 4); out[i] = x; }
        }
        return true;
    }
};

uint8_t toByte(float v) { return (uint8_t)(clampf(v, 0.f, 1.f) * 255.f + 0.5f); }

std::unique_ptr<Model> build(const Json& root, std::vector<Blob>& bufs, const std::string& name, std::string* err,
                             const FileReader& fileReader) {
    auto fail = [&](const char* m) -> std::unique_ptr<Model> { if (err) *err = m; return nullptr; };
    Reader rd{root, bufs};
    auto model = std::make_unique<Model>();
    model->name = name;

    // --- текстуры (images -> Model::textures, по требованию) ---
    const Json& jimg = root["images"];
    std::vector<int> imageTex(jimg.size(), -2);  // -2 не загружено, -1 ошибка
    auto getImage = [&](int idx) -> int {
        if (idx < 0 || idx >= (int)imageTex.size()) return -1;
        if (imageTex[idx] != -2) return imageTex[idx];
        const Json& im = jimg[idx];
        std::vector<uint8_t> bytes;
        bool ok = false;
        if (im.has("bufferView")) {
            size_t sz, st;
            const uint8_t* p = rd.viewPtr(im["bufferView"].integer(), sz, st);
            if (p) { bytes.assign(p, p + sz); ok = true; }
        } else if (im.has("uri")) ok = loadUri(im["uri"].string(), fileReader, bytes);
        int t = -1;
        if (ok) t = addTextureFromMemory(*model, bytes.data(), bytes.size(), nullptr);
        if (t < 0) ENG_LOG("glTF: texture %d not loaded", idx);
        return imageTex[idx] = t;
    };
    auto texImage = [&](const Json& texInfo) -> int {
        if (texInfo.isNull()) return -1;
        const Json& tx = root["textures"][texInfo["index"].integer()];
        if (tx.isNull() || !tx.has("source")) return -1;
        return getImage(tx["source"].integer());
    };

    // --- узлы ---
    const Json& jn = root["nodes"];
    model->nodes.resize(jn.size());
    for (size_t i = 0; i < jn.size(); i++) {
        const Json& n = jn[i];
        Node& nd = model->nodes[i];
        nd.name = n["name"].string();
        if (n.has("matrix") && n["matrix"].size() == 16) {
            Mat4 m;
            for (int k = 0; k < 16; k++) m.m[k] = (float)n["matrix"][k].number();
            decompose(m, nd.t, nd.r, nd.s);
        } else {
            if (n.has("translation")) nd.t = {(float)n["translation"][0].number(), (float)n["translation"][1].number(), (float)n["translation"][2].number()};
            if (n.has("rotation")) nd.r = normalize(Quat((float)n["rotation"][0].number(), (float)n["rotation"][1].number(), (float)n["rotation"][2].number(), (float)n["rotation"][3].number(1)));
            if (n.has("scale")) nd.s = {(float)n["scale"][0].number(1), (float)n["scale"][1].number(1), (float)n["scale"][2].number(1)};
        }
        nd.skin = n.has("skin") ? n["skin"].integer() : -1;
    }
    for (size_t i = 0; i < jn.size(); i++) {
        const Json& ch = jn[i]["children"];
        for (size_t k = 0; k < ch.size(); k++) {
            int c = ch[k].integer(-1);
            if (c >= 0 && c < (int)model->nodes.size()) model->nodes[c].parent = (int)i;
        }
    }

    // --- скины ---
    const Json& js = root["skins"];
    for (size_t i = 0; i < js.size(); i++) {
        Skin sk;
        const Json& jj = js[i]["joints"];
        for (size_t k = 0; k < jj.size(); k++) sk.joints.push_back(jj[k].integer());
        sk.invBind.assign(sk.joints.size(), Mat4());
        if (js[i].has("inverseBindMatrices")) {
            std::vector<float> f; int comps, count;
            if (rd.read(js[i]["inverseBindMatrices"].integer(), f, comps, count) && comps == 16)
                for (size_t k = 0; k < sk.joints.size() && (int)k < count; k++) std::memcpy(sk.invBind[k].m, &f[k * 16], 64);
        }
        if (sk.joints.size() > (size_t)kMaxJoints) ENG_LOG("glTF: skin has %d joints, limit is %d", (int)sk.joints.size(), kMaxJoints);
        model->skins.push_back(std::move(sk));
    }

    // --- меши ---
    const Json& jm = root["meshes"];
    std::vector<std::vector<int>> meshPrims(jm.size());
    std::vector<char> primHasJoints;
    for (size_t mi = 0; mi < jm.size(); mi++) {
        const Json& prims = jm[mi]["primitives"];
        for (size_t pi = 0; pi < prims.size(); pi++) {
            const Json& pr = prims[pi];
            int mode = pr.has("mode") ? pr["mode"].integer() : 4;
            if (mode != 4 && mode != 5 && mode != 6) continue;
            const Json& at = pr["attributes"];
            if (!at.has("POSITION")) continue;
            std::vector<float> pos, nrm, uv, col, jnt, wgt;
            int pc, pn, c2, n2;
            if (!rd.read(at["POSITION"].integer(), pos, pc, pn) || pc != 3) return fail("bad POSITION accessor");
            bool hasN = at.has("NORMAL") && rd.read(at["NORMAL"].integer(), nrm, c2, n2) && c2 == 3 && n2 == pn;
            bool hasUV = at.has("TEXCOORD_0") && rd.read(at["TEXCOORD_0"].integer(), uv, c2, n2) && c2 == 2 && n2 == pn;
            int colC = 0, cn = 0;
            bool hasC = at.has("COLOR_0") && rd.read(at["COLOR_0"].integer(), col, colC, cn) && cn == pn && (colC == 3 || colC == 4);
            bool hasJ = at.has("JOINTS_0") && at.has("WEIGHTS_0") && rd.read(at["JOINTS_0"].integer(), jnt, c2, n2) && n2 == pn &&
                        c2 == 4 && rd.read(at["WEIGHTS_0"].integer(), wgt, c2, n2) && n2 == pn && c2 == 4;

            MeshData md;
            Color base(1, 1, 1, 1);
            float rough = 0.6f;
            if (pr.has("material")) {
                const Json& mat = root["materials"][pr["material"].integer()];
                const Json& pbr = mat["pbrMetallicRoughness"];
                const Json& sg = mat["extensions"]["KHR_materials_pbrSpecularGlossiness"];
                const Json& bc = pbr.has("baseColorFactor") ? pbr["baseColorFactor"] : sg["diffuseFactor"];
                if (!bc.isNull()) base = Color((float)bc[0].number(1), (float)bc[1].number(1), (float)bc[2].number(1), (float)bc[3].number(1));
                if (pbr.has("roughnessFactor")) rough = (float)pbr["roughnessFactor"].number(0.6);
                md.doubleSided = mat["doubleSided"].type == Json::Bool && mat["doubleSided"].b;
                const std::string& am = mat["alphaMode"].string();
                md.alphaMode = am == "MASK" ? 1 : am == "BLEND" ? 2 : 0;
                md.alphaCutoff = (float)mat["alphaCutoff"].number(0.5);
                md.texture = hasUV ? texImage(pbr.has("baseColorTexture") ? pbr["baseColorTexture"] : sg["diffuseTexture"]) : -1;
            }
            md.shininess = clampf(2.f / std::max(rough * rough, 0.02f), 4.f, 128.f);
            md.verts.resize(pn);
            for (int i = 0; i < pn; i++) {
                Vertex& v = md.verts[i];
                std::memset(&v, 0, sizeof v);
                for (int k = 0; k < 3; k++) v.p[k] = pos[i * 3 + k];
                if (hasN) for (int k = 0; k < 3; k++) v.n[k] = nrm[i * 3 + k];
                if (hasUV) { v.uv[0] = uv[i * 2]; v.uv[1] = uv[i * 2 + 1]; }
                float r = base.r, g = base.g, b = base.b, a = base.a;
                if (hasC) { r *= col[i * colC]; g *= col[i * colC + 1]; b *= col[i * colC + 2]; if (colC == 4) a *= col[i * colC + 3]; }
                v.c[0] = toByte(r); v.c[1] = toByte(g); v.c[2] = toByte(b); v.c[3] = toByte(a);
                if (hasJ) {
                    float sum = 0;
                    for (int k = 0; k < 4; k++) {
                        int j = (int)jnt[i * 4 + k];
                        bool okj = j >= 0 && j < kMaxJoints;
                        v.j[k] = (uint8_t)(okj ? j : 0);
                        v.w[k] = okj ? wgt[i * 4 + k] : 0.f;
                        sum += v.w[k];
                    }
                    if (sum > 1e-6f) for (int k = 0; k < 4; k++) v.w[k] /= sum; else v.w[0] = 1;
                } else v.w[0] = 1;
            }
            std::vector<uint32_t> idx;
            if (pr.has("indices")) {
                if (!rd.readIndices(pr["indices"].integer(), idx)) return fail("bad indices");
            } else { idx.resize(pn); for (int i = 0; i < pn; i++) idx[i] = (uint32_t)i; }
            for (uint32_t ix : idx) if (ix >= (uint32_t)pn) return fail("index out of range");
            if (mode == 4) { md.indices = std::move(idx); md.indices.resize(md.indices.size() / 3 * 3); }
            else if (mode == 5) {
                for (size_t i = 0; i + 2 < idx.size(); i++) {
                    if (i & 1) md.indices.insert(md.indices.end(), {idx[i + 1], idx[i], idx[i + 2]});
                    else md.indices.insert(md.indices.end(), {idx[i], idx[i + 1], idx[i + 2]});
                }
            } else {
                for (size_t i = 1; i + 1 < idx.size(); i++) md.indices.insert(md.indices.end(), {idx[0], idx[i], idx[i + 1]});
            }
            if (!hasN) computeSmoothNormals(md, false);
            primHasJoints.push_back(hasJ ? 1 : 0);
            meshPrims[mi].push_back((int)model->meshes.size());
            model->meshes.push_back(std::move(md));
        }
    }
    for (size_t i = 0; i < jn.size(); i++) {
        int mi = jn[i].has("mesh") ? jn[i]["mesh"].integer(-1) : -1;
        if (mi < 0 || mi >= (int)meshPrims.size()) continue;
        for (int pidx : meshPrims[mi]) {
            model->nodes[i].meshes.push_back(pidx);
            if (primHasJoints[pidx] && model->nodes[i].skin >= 0 && model->nodes[i].skin < (int)model->skins.size())
                model->meshes[pidx].skin = model->nodes[i].skin;
        }
    }

    // --- анимации ---
    const Json& ja = root["animations"];
    for (size_t ai = 0; ai < ja.size(); ai++) {
        const Json& an = ja[ai];
        Clip clip;
        clip.name = an["name"].string();
        if (clip.name.empty()) clip.name = "ANIM" + std::to_string(ai);
        const Json& chans = an["channels"];
        const Json& samplers = an["samplers"];
        for (size_t ci = 0; ci < chans.size(); ci++) {
            const Json& ch = chans[ci];
            const Json& tg = ch["target"];
            if (!tg.has("node")) continue;
            const std::string& path = tg["path"].string();
            Channel c;
            c.node = tg["node"].integer();
            if (c.node < 0 || c.node >= (int)model->nodes.size()) continue;
            if (path == "translation") c.path = Path::Translation;
            else if (path == "rotation") c.path = Path::Rotation;
            else if (path == "scale") c.path = Path::Scale;
            else continue;
            const Json& sm = samplers[ch["sampler"].integer()];
            std::vector<float> tin, vout;
            int c1, n1, c2, n2;
            if (!rd.read(sm["input"].integer(), tin, c1, n1) || !rd.read(sm["output"].integer(), vout, c2, n2)) continue;
            const std::string& interp = sm["interpolation"].string();
            c.step = interp == "STEP";
            int comps = c.path == Path::Rotation ? 4 : 3;
            if (interp == "CUBICSPLINE") {
                if ((int)vout.size() < n1 * 3 * comps) continue;
                std::vector<float> v((size_t)n1 * comps);
                for (int k = 0; k < n1; k++)
                    for (int q = 0; q < comps; q++) v[(size_t)k * comps + q] = vout[((size_t)k * 3 + 1) * comps + q];
                vout.swap(v);
            } else if ((int)vout.size() < n1 * comps) continue;
            c.times = tin;
            c.values = vout;
            if (!tin.empty()) clip.duration = std::max(clip.duration, tin.back());
            clip.channels.push_back(std::move(c));
        }
        if (!clip.channels.empty()) model->clips.push_back(std::move(clip));
    }

    if (model->nodes.empty() || model->meshes.empty()) return fail("model has no geometry");
    breakNodeCycles(*model);
    model->finalize();
    return model;
}

}  // namespace

std::unique_ptr<Model> loadGlb(const uint8_t* data, size_t size, const std::string& name, std::string* err,
                               const FileReader& reader) {
    auto fail = [&](const char* m) -> std::unique_ptr<Model> { if (err) *err = m; return nullptr; };
    if (size < 20 || std::memcmp(data, "glTF", 4) != 0) return fail("not a GLB file");
    uint32_t version, total;
    std::memcpy(&version, data + 4, 4);
    std::memcpy(&total, data + 8, 4);
    if (version != 2) return fail("unsupported glTF version");
    if (total > size) total = (uint32_t)size;
    const uint8_t* jsonData = nullptr; size_t jsonSize = 0;
    Blob glbBin;
    size_t pos = 12;
    while (pos + 8 <= total) {
        uint32_t len, type;
        std::memcpy(&len, data + pos, 4);
        std::memcpy(&type, data + pos + 4, 4);
        pos += 8;
        if (len > total - pos) return fail("truncated chunk");
        if (type == 0x4E4F534A) { jsonData = data + pos; jsonSize = len; }
        else if (type == 0x004E4942 && !glbBin.p) { glbBin.p = data + pos; glbBin.n = len; }
        pos += (len + 3) & ~3u;
    }
    if (!jsonData) return fail("no JSON chunk");
    Json root;
    if (!JsonParser((const char*)jsonData, jsonSize).parse(root)) return fail("bad JSON");
    std::vector<Blob> bufs;
    const Json& jb = root["buffers"];
    for (size_t i = 0; i < jb.size(); i++) {
        Blob b;
        if (jb[i].has("uri")) {
            if (!loadUri(jb[i]["uri"].string(), reader, b.own)) return fail("buffer uri not loaded");
            b.p = b.own.data(); b.n = b.own.size();
        } else if (i == 0 && glbBin.p) b = glbBin;
        bufs.push_back(std::move(b));
    }
    for (auto& b : bufs) if (!b.own.empty()) { b.p = b.own.data(); b.n = b.own.size(); }
    return build(root, bufs, name, err, reader);
}

std::unique_ptr<Model> loadGltf(const uint8_t* json, size_t size, const std::string& name, std::string* err,
                                const FileReader& reader) {
    Json root;
    if (!JsonParser((const char*)json, size).parse(root)) { if (err) *err = "bad JSON"; return nullptr; }
    std::vector<Blob> bufs;
    const Json& jb = root["buffers"];
    for (size_t i = 0; i < jb.size(); i++) {
        Blob b;
        if (!jb[i].has("uri") || !loadUri(jb[i]["uri"].string(), reader, b.own)) {
            if (err) *err = "buffer " + std::to_string(i) + " not available (put the .bin next to the .gltf)";
            return nullptr;
        }
        b.p = b.own.data(); b.n = b.own.size();
        bufs.push_back(std::move(b));
    }
    // Blob с own-вектором нельзя копировать по указателю после push_back: переустановим указатели.
    for (auto& b : bufs) { b.p = b.own.data(); b.n = b.own.size(); }
    return build(root, bufs, name, err, reader);
}

}  // namespace eng
