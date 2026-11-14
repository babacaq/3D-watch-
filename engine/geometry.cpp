#include "geometry.h"

#include <algorithm>
#include <cmath>

namespace eng {

static uint8_t toByte(float v) { return (uint8_t)(clampf(v, 0.f, 1.f) * 255.f + 0.5f); }

static uint32_t pushVertex(MeshData& m, Vec3 p, Vec3 n, Color c, const int* bones, const float* weights, int nb) {
    Vertex v{};
    v.p[0] = p.x; v.p[1] = p.y; v.p[2] = p.z;
    Vec3 nn = normalize(n);
    v.n[0] = nn.x; v.n[1] = nn.y; v.n[2] = nn.z;
    v.c[0] = toByte(c.r); v.c[1] = toByte(c.g); v.c[2] = toByte(c.b); v.c[3] = toByte(c.a);
    for (int i = 0; i < 4; i++) { v.j[i] = 0; v.w[i] = 0; }
    float sum = 0;
    for (int i = 0; i < nb && i < 4; i++) {
        v.j[i] = (uint8_t)std::max(0, bones[i]);
        v.w[i] = weights[i];
        sum += weights[i];
    }
    if (sum <= 0) { v.w[0] = 1; sum = 1; }
    for (int i = 0; i < 4; i++) v.w[i] /= sum;
    m.verts.push_back(v);
    return (uint32_t)m.verts.size() - 1;
}

static uint32_t pushRigid(MeshData& m, Vec3 p, Vec3 n, Color c, int bone) {
    float w = 1.f;
    int b = std::max(0, bone);
    return pushVertex(m, p, n, c, &b, &w, 1);
}

void addBox(MeshData& m, Vec3 c, Vec3 h, Color col, int bone) {
    struct Face { Vec3 n, u, v; };
    static const Face faces[6] = {
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},  {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},  {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},   {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
    };
    auto ext = [&](Vec3 a) { return std::fabs(a.x) * h.x + std::fabs(a.y) * h.y + std::fabs(a.z) * h.z; };
    for (const Face& f : faces) {
        Vec3 base = c + f.n * ext(f.n);
        float eu = ext(f.u), ev = ext(f.v);
        uint32_t i0 = pushRigid(m, base - f.u * eu - f.v * ev, f.n, col, bone);
        uint32_t i1 = pushRigid(m, base + f.u * eu - f.v * ev, f.n, col, bone);
        uint32_t i2 = pushRigid(m, base + f.u * eu + f.v * ev, f.n, col, bone);
        uint32_t i3 = pushRigid(m, base - f.u * eu + f.v * ev, f.n, col, bone);
        m.indices.insert(m.indices.end(), {i0, i1, i2, i0, i2, i3});
    }
}

void addSphere(MeshData& m, Vec3 c, Vec3 r, Color col, int bone, int segs, int rings) {
    uint32_t base = (uint32_t)m.verts.size();
    for (int i = 0; i <= rings; i++) {
        float th = kPi * i / rings;
        for (int j = 0; j <= segs; j++) {
            float ph = 2.f * kPi * j / segs;
            Vec3 u{std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)};
            Vec3 p = c + Vec3(u.x * r.x, u.y * r.y, u.z * r.z);
            Vec3 n{u.x / std::max(r.x, 1e-6f), u.y / std::max(r.y, 1e-6f), u.z / std::max(r.z, 1e-6f)};
            pushRigid(m, p, n, col, bone);
        }
    }
    for (int i = 0; i < rings; i++)
        for (int j = 0; j < segs; j++) {
            uint32_t a = base + i * (segs + 1) + j, b = a + segs + 1;
            m.indices.insert(m.indices.end(), {a, b + 1, b, a, a + 1, b + 1});
        }
}

void addCylinder(MeshData& m, Vec3 p0, Vec3 p1, float r0, float r1, Color col, int bone, int sides, bool caps) {
    TubeDesc d;
    d.pts = {p0, p1};
    d.bones = {bone, bone};
    d.radii = {r0, r1};
    d.sides = sides;
    d.step = length(p1 - p0) + 1.f;  // только два кольца
    d.capRings = 0;
    d.capStart = d.capEnd = false;
    d.color = col;
    addTube(m, d);
    if (!caps) return;
    Vec3 axis = normalize(p1 - p0);
    Vec3 ref = std::fabs(axis.y) > 0.98f ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
    Vec3 u = normalize(cross(ref, axis)), v = cross(axis, u);
    for (int end = 0; end < 2; end++) {
        Vec3 c = end ? p1 : p0;
        float r = end ? r1 : r0;
        Vec3 n = end ? axis : -axis;
        uint32_t ci = pushRigid(m, c, n, col, bone);
        uint32_t first = (uint32_t)m.verts.size();
        for (int a = 0; a < sides; a++) {
            float ang = 2.f * kPi * a / sides;
            pushRigid(m, c + (u * std::cos(ang) + v * std::sin(ang)) * r, n, col, bone);
        }
        for (int a = 0; a < sides; a++) {
            uint32_t A = first + a, B = first + (a + 1) % sides;
            if (end) m.indices.insert(m.indices.end(), {ci, A, B});
            else m.indices.insert(m.indices.end(), {ci, B, A});
        }
    }
}

namespace {
struct Ring {
    Vec3 c, u, v, axis;
    float radius, axialN;  // axialN: компонента нормали вдоль оси
    Color col;
    int b[4];
    float w[4];
    int nb;
};
}  // namespace

void addTube(MeshData& m, const TubeDesc& d) {
    const size_t K = d.pts.size();
    if (K < 2) return;
    std::vector<float> Lj(K, 0.f);
    for (size_t i = 1; i < K; i++) Lj[i] = Lj[i - 1] + length(d.pts[i] - d.pts[i - 1]);
    const float total = Lj[K - 1];
    if (total < 1e-6f) return;

    auto frame = [](Vec3 axis, Vec3& u, Vec3& v) {
        Vec3 ref = std::fabs(axis.y) > 0.98f ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
        u = normalize(cross(ref, axis));
        v = cross(axis, u);
    };
    auto weightsAt = [&](float L, Ring& r) {
        // последовательное смешивание: bones[k-1] -> bones[k] вокруг сустава k
        float acc[8] = {0};
        int n = (int)std::min<size_t>(K, 8);
        acc[0] = 1.f;
        for (int k = 1; k < n; k++) {
            float bw = std::max(d.blend, 1e-4f);
            float t = smoothstep(Lj[k] - bw, Lj[k] + bw, L);
            for (int q = 0; q < k; q++) acc[q] *= (1.f - t);
            acc[k] = t;
        }
        // свернуть одинаковые кости, взять 4 самых тяжёлых
        std::vector<std::pair<float, int>> list;
        for (int k = 0; k < n; k++) {
            if (acc[k] < 1e-4f) continue;
            bool found = false;
            for (auto& e : list) if (e.second == d.bones[k]) { e.first += acc[k]; found = true; }
            if (!found) list.push_back({acc[k], d.bones[k]});
        }
        std::sort(list.begin(), list.end(), [](auto& a, auto& b) { return a.first > b.first; });
        r.nb = (int)std::min<size_t>(list.size(), 4);
        for (int i = 0; i < r.nb; i++) { r.w[i] = list[i].first; r.b[i] = list[i].second; }
    };
    auto sampleAt = [&](float L, Vec3& pos, Vec3& axis, float& rad) {
        size_t s = 0;
        while (s + 2 < K && L > Lj[s + 1]) s++;
        float seg = Lj[s + 1] - Lj[s];
        float f = seg > 1e-9f ? clampf((L - Lj[s]) / seg, 0.f, 1.f) : 0.f;
        pos = lerp(d.pts[s], d.pts[s + 1], f);
        axis = normalize(d.pts[s + 1] - d.pts[s]);
        float r0 = d.radii[std::min(s, d.radii.size() - 1)];
        float r1 = d.radii[std::min(s + 1, d.radii.size() - 1)];
        rad = r0 + (r1 - r0) * f;
    };

    // Значения L для основных колец: равномерно + сами суставы + края зон сглаживания.
    std::vector<float> Ls;
    int n = std::max(1, (int)std::ceil(total / std::max(d.step, 1e-4f)));
    for (int i = 0; i <= n; i++) Ls.push_back(total * i / n);
    for (size_t k = 1; k + 1 < K; k++) {
        Ls.push_back(Lj[k]);
        Ls.push_back(std::max(0.f, Lj[k] - d.blend));
        Ls.push_back(std::min(total, Lj[k] + d.blend));
    }
    std::sort(Ls.begin(), Ls.end());
    Ls.erase(std::unique(Ls.begin(), Ls.end(), [](float a, float b) { return std::fabs(a - b) < 1e-5f; }), Ls.end());

    std::vector<Ring> rings;
    // начальная шапка
    {
        Vec3 pos, axis; float rad;
        sampleAt(0, pos, axis, rad);
        Ring base{}; weightsAt(0, base);
        base.col = d.colorFn ? d.colorFn(0.f) : d.color;
        if (d.capStart && d.capRings > 0)
            for (int k = 0; k < d.capRings; k++) {
                float ang = 0.5f * kPi * k / d.capRings;
                Ring r = base;
                r.axis = axis; frame(axis, r.u, r.v);
                r.c = pos + axis * (-rad * std::cos(ang));
                r.radius = rad * std::sin(ang);
                r.axialN = -std::cos(ang);
                rings.push_back(r);
            }
    }
    for (float L : Ls) {
        Ring r{};
        Vec3 pos, axis; float rad;
        sampleAt(L, pos, axis, rad);
        r.c = pos; r.axis = axis; r.radius = rad; r.axialN = 0;
        r.col = d.colorFn ? d.colorFn(L) : d.color;
        frame(axis, r.u, r.v);
        weightsAt(L, r);
        rings.push_back(r);
    }
    {
        Vec3 pos, axis; float rad;
        sampleAt(total, pos, axis, rad);
        Ring base{}; weightsAt(total, base);
        base.col = d.colorFn ? d.colorFn(total) : d.color;
        if (d.capEnd && d.capRings > 0)
            for (int k = d.capRings - 1; k >= 0; k--) {
                float ang = 0.5f * kPi * k / d.capRings;
                if (k == 0) ang = 0.f;
                Ring r = base;
                r.axis = axis; frame(axis, r.u, r.v);
                r.c = pos + axis * (rad * std::cos(ang));
                r.radius = rad * std::sin(ang);
                r.axialN = std::cos(ang);
                rings.push_back(r);
            }
    }

    const int S = d.sides;
    uint32_t base = (uint32_t)m.verts.size();
    for (const Ring& r : rings) {
        for (int a = 0; a < S; a++) {
            float ang = 2.f * kPi * a / S;
            Vec3 radial = r.u * std::cos(ang) + r.v * std::sin(ang);
            Vec3 p = r.c + radial * r.radius;
            float radN = std::sqrt(std::max(0.f, 1.f - r.axialN * r.axialN));
            Vec3 n = radial * radN + r.axis * r.axialN;
            pushVertex(m, p, n, r.col, r.b, r.w, r.nb);
        }
    }
    for (size_t i = 0; i + 1 < rings.size(); i++)
        for (int a = 0; a < S; a++) {
            int a1 = (a + 1) % S;
            uint32_t A = base + (uint32_t)(i * S + a), B = base + (uint32_t)(i * S + a1);
            uint32_t C = base + (uint32_t)((i + 1) * S + a), D = base + (uint32_t)((i + 1) * S + a1);
            m.indices.insert(m.indices.end(), {A, B, C, B, D, C});
        }
}

float windingScore(const MeshData& m) {
    size_t good = 0, total = 0;
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const Vertex& a = m.verts[m.indices[i]];
        const Vertex& b = m.verts[m.indices[i + 1]];
        const Vertex& c = m.verts[m.indices[i + 2]];
        Vec3 pa(a.p[0], a.p[1], a.p[2]), pb(b.p[0], b.p[1], b.p[2]), pc(c.p[0], c.p[1], c.p[2]);
        Vec3 g = cross(pb - pa, pc - pa);
        if (length(g) < 1e-9f) continue;
        Vec3 n(a.n[0] + b.n[0] + c.n[0], a.n[1] + b.n[1] + c.n[1], a.n[2] + b.n[2] + c.n[2]);
        total++;
        if (dot(g, n) > 0) good++;
    }
    return total ? (float)good / (float)total : 1.f;
}

}  // namespace eng
