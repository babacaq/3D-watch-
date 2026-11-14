// Физика твёрдых тел: sequential impulses + warm starting, SAT (box-box, box-triangle), статичная сетка (сетка ячеек),
// непрерывные столкновения для пуль, шаровые шарниры, сон по островам. См. physics.h.
#include "physics.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace eng {
namespace {

constexpr float kH = 1.f / 120.f;
constexpr int kMaxSub = 4;
constexpr int kIters = 10;
constexpr float kSlop = 0.004f, kBeta = 0.2f;

inline Vec3 rotate(const Quat& q, const Vec3& v) {
    Vec3 u(q.x, q.y, q.z);
    Vec3 t = cross(u, v) * 2.f;
    return v + t * q.w + cross(u, t);
}
inline Quat conj(const Quat& q) { return {-q.x, -q.y, -q.z, q.w}; }
inline float comp(const Vec3& v, int i) { return i == 0 ? v.x : (i == 1 ? v.y : v.z); }
inline bool finite3(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

struct Mat3 { Vec3 c[3]; };
inline Vec3 mul(const Mat3& m, const Vec3& v) { return m.c[0] * v.x + m.c[1] * v.y + m.c[2] * v.z; }
inline Mat3 inv3(const Mat3& m) {
    const Vec3 &a = m.c[0], &b = m.c[1], &c = m.c[2];
    Vec3 r0 = cross(b, c), r1 = cross(c, a), r2 = cross(a, b);
    float det = dot(a, r0);
    if (std::fabs(det) < 1e-20f) return {{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}};
    float id = 1.f / det;
    return {{{r0.x * id, r1.x * id, r2.x * id}, {r0.y * id, r1.y * id, r2.y * id}, {r0.z * id, r1.z * id, r2.z * id}}};
}

struct OBB { Vec3 c; Vec3 ax[3]; float h[3]; };
inline OBB makeOBB(Vec3 c, Quat q, Vec3 half) {
    OBB o; o.c = c;
    o.ax[0] = rotate(q, {1, 0, 0}); o.ax[1] = rotate(q, {0, 1, 0}); o.ax[2] = rotate(q, {0, 0, 1});
    o.h[0] = half.x; o.h[1] = half.y; o.h[2] = half.z;
    return o;
}

struct CP { Vec3 p; float depth; };

// Сэзерленд-Ходжман: оставить точки с dot(p,n) <= d.
void clipPoly(std::vector<Vec3>& poly, Vec3 n, float d) {
    if (poly.empty()) return;
    std::vector<Vec3> out;
    size_t N = poly.size();
    for (size_t i = 0; i < N; i++) {
        Vec3 a = poly[i], b = poly[(i + 1) % N];
        float da = dot(a, n) - d, db = dot(b, n) - d;
        if (da <= 0) out.push_back(a);
        if ((da < 0 && db > 0) || (da > 0 && db < 0)) out.push_back(lerp(a, b, da / (da - db)));
    }
    poly.swap(out);
}

void reduce4(std::vector<CP>& pts, Vec3 n) {
    if (pts.size() <= 4) return;
    std::vector<CP> r;
    size_t i0 = 0;
    for (size_t i = 1; i < pts.size(); i++) if (pts[i].depth > pts[i0].depth) i0 = i;
    r.push_back(pts[i0]);
    size_t i1 = 0; float bd = -1;
    for (size_t i = 0; i < pts.size(); i++) { float d = length(pts[i].p - pts[i0].p); if (d > bd) { bd = d; i1 = i; } }
    r.push_back(pts[i1]);
    Vec3 e = pts[i1].p - pts[i0].p;
    size_t i2 = i0, i3 = i0; float mx = 0, mn = 0;
    for (size_t i = 0; i < pts.size(); i++) {
        float s = dot(cross(e, pts[i].p - pts[i0].p), n);
        if (s > mx) { mx = s; i2 = i; }
        if (s < mn) { mn = s; i3 = i; }
    }
    if (i2 != i0) r.push_back(pts[i2]);
    if (i3 != i0 && i3 != i2) r.push_back(pts[i3]);
    pts.swap(r);
}

// Ближайшие точки двух бесконечных прямых.
void lineClosest(Vec3 p1, Vec3 d1, Vec3 p2, Vec3 d2, Vec3& c1, Vec3& c2) {
    Vec3 r = p1 - p2;
    float a = dot(d1, d1), e = dot(d2, d2), b = dot(d1, d2), c = dot(d1, r), f = dot(d2, r);
    float den = a * e - b * b, s = 0, t;
    if (den > 1e-9f) s = (b * f - c * e) / den;
    t = (b * s + f) / std::max(e, 1e-12f);
    c1 = p1 + d1 * s; c2 = p2 + d2 * t;
}

// Box (A) против Box (B). n: от B к A. Возвращает true при пересечении.
bool boxBox(const OBB& A, const OBB& B, std::vector<CP>& out, Vec3& nOut) {
    Vec3 d = A.c - B.c;
    float best = 1e30f, bestPen = 0;
    int bi = -1;
    Vec3 bn;
    auto test = [&](Vec3 L, int idx) -> bool {
        float len = length(L);
        if (len < 1e-5f) return true;
        L = L * (1.f / len);
        float ra = 0, rb = 0;
        for (int k = 0; k < 3; k++) { ra += std::fabs(dot(A.ax[k], L)) * A.h[k]; rb += std::fabs(dot(B.ax[k], L)) * B.h[k]; }
        float dist = dot(d, L);
        float pen = ra + rb - std::fabs(dist);
        if (pen <= 0) return false;
        float score = idx < 6 ? pen : pen * 1.05f + 0.002f;
        if (score < best) { best = score; bestPen = pen; bi = idx; bn = dist >= 0 ? L : -L; }
        return true;
    };
    for (int i = 0; i < 3; i++) if (!test(A.ax[i], i)) return false;
    for (int i = 0; i < 3; i++) if (!test(B.ax[i], 3 + i)) return false;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) if (!test(cross(A.ax[i], B.ax[j]), 6 + i * 3 + j)) return false;
    if (bi < 0) return false;
    nOut = bn;
    out.clear();
    if (bi < 6) {
        const OBB& R = bi < 3 ? A : B;
        const OBB& I = bi < 3 ? B : A;
        int ra = bi < 3 ? bi : bi - 3;
        Vec3 want = bi < 3 ? -bn : bn;
        Vec3 refN = R.ax[ra] * (dot(R.ax[ra], want) >= 0 ? 1.f : -1.f);
        Vec3 fc = R.c + refN * R.h[ra];
        int k = 0; float mx = -1;
        for (int i = 0; i < 3; i++) { float v = std::fabs(dot(I.ax[i], refN)); if (v > mx) { mx = v; k = i; } }
        float sg = dot(I.ax[k], refN) > 0 ? -1.f : 1.f;
        Vec3 ic = I.c + I.ax[k] * (sg * I.h[k]);
        int u = (k + 1) % 3, v = (k + 2) % 3;
        std::vector<Vec3> poly = {ic + I.ax[u] * I.h[u] + I.ax[v] * I.h[v], ic - I.ax[u] * I.h[u] + I.ax[v] * I.h[v],
                                  ic - I.ax[u] * I.h[u] - I.ax[v] * I.h[v], ic + I.ax[u] * I.h[u] - I.ax[v] * I.h[v]};
        for (int m = 1; m <= 2; m++) {
            int mi = (ra + m) % 3;
            clipPoly(poly, R.ax[mi], dot(R.ax[mi], R.c) + R.h[mi]);
            clipPoly(poly, -R.ax[mi], dot(-R.ax[mi], R.c) + R.h[mi]);
        }
        for (auto& p : poly) {
            float sep = dot(p - fc, refN);
            if (sep <= 0.01f) out.push_back({p, -sep});
        }
        if (out.empty()) out.push_back({ic, bestPen});
        reduce4(out, bn);
    } else {
        int i = (bi - 6) / 3, j = (bi - 6) % 3;
        Vec3 pa = A.c, pb = B.c;
        for (int k = 0; k < 3; k++) {
            if (k != i) pa += A.ax[k] * (dot(A.ax[k], bn) > 0 ? -A.h[k] : A.h[k]);
            if (k != j) pb += B.ax[k] * (dot(B.ax[k], bn) > 0 ? B.h[k] : -B.h[k]);
        }
        Vec3 c1, c2;
        lineClosest(pa, A.ax[i], pb, B.ax[j], c1, c2);
        out.push_back({(c1 + c2) * 0.5f, bestPen});
    }
    return true;
}

struct Tri { Vec3 a, b, c; };

Vec3 closestOnTri(const Tri& t, Vec3 p) {
    Vec3 ab = t.b - t.a, ac = t.c - t.a, ap = p - t.a;
    float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return t.a;
    Vec3 bp = p - t.b;
    float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return t.b;
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return t.a + ab * (d1 / (d1 - d3));
    Vec3 cp = p - t.c;
    float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return t.c;
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return t.a + ac * (d2 / (d2 - d6));
    float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return t.b + (t.c - t.b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    float den = 1.f / (va + vb + vc);
    return t.a + ab * (vb * den) + ac * (vc * den);
}

// Box (dynamic) против треугольника. n: от треугольника к боксу.
bool boxTri(const OBB& B, const Tri& T, std::vector<CP>& out, Vec3& nOut) {
    Vec3 e[3] = {T.b - T.a, T.c - T.b, T.a - T.c};
    Vec3 triN = normalize(cross(e[0], e[1]));
    if (length(cross(e[0], e[1])) < 1e-12f) return false;
    Vec3 v[3] = {T.a, T.b, T.c};
    float best = 1e30f, bestPen = 0;
    int bi = -1;
    Vec3 bn;
    auto test = [&](Vec3 L, int idx) -> bool {
        float len = length(L);
        if (len < 1e-5f) return true;
        L = L * (1.f / len);
        float r = 0;
        for (int k = 0; k < 3; k++) r += std::fabs(dot(B.ax[k], L)) * B.h[k];
        float c = dot(B.c, L);
        float tmin = 1e30f, tmax = -1e30f;
        for (int k = 0; k < 3; k++) { float t = dot(v[k], L); tmin = std::min(tmin, t); tmax = std::max(tmax, t); }
        float o1 = (c + r) - tmin, o2 = tmax - (c - r);
        if (o1 <= 0 || o2 <= 0) return false;
        float pen = std::min(o1, o2);
        float score = idx >= 4 ? pen * 1.05f + 0.002f : pen;
        if (score < best) { best = score; bestPen = pen; bi = idx; bn = o2 < o1 ? L : -L; }
        return true;
    };
    for (int i = 0; i < 3; i++) if (!test(B.ax[i], i)) return false;
    if (!test(triN, 3)) return false;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) if (!test(cross(B.ax[i], e[j]), 4 + i * 3 + j)) return false;
    if (bi < 0) return false;
    nOut = bn;
    out.clear();
    if (bi < 3) {  // опорная грань - грань бокса, обращённая к треугольнику
        Vec3 refN = B.ax[bi] * (dot(B.ax[bi], bn) > 0 ? -1.f : 1.f);  // наружу от бокса к треугольнику
        Vec3 fc = B.c + refN * B.h[bi];
        std::vector<Vec3> poly = {T.a, T.b, T.c};
        for (int m = 1; m <= 2; m++) {
            int mi = (bi + m) % 3;
            clipPoly(poly, B.ax[mi], dot(B.ax[mi], B.c) + B.h[mi]);
            clipPoly(poly, -B.ax[mi], dot(-B.ax[mi], B.c) + B.h[mi]);
        }
        for (auto& p : poly) {
            float sep = dot(p - fc, refN);
            if (sep <= 0.01f) out.push_back({p, -sep});
        }
        if (out.empty()) out.push_back({closestOnTri(T, B.c), bestPen});
    } else if (bi == 3) {  // опорная - треугольник; инцидентная грань бокса
        Vec3 N = dot(bn, triN) >= 0 ? triN : -triN;  // от треугольника к боксу
        int k = 0; float mx = -1;
        for (int i = 0; i < 3; i++) { float s = std::fabs(dot(B.ax[i], N)); if (s > mx) { mx = s; k = i; } }
        float sg = dot(B.ax[k], N) > 0 ? -1.f : 1.f;
        Vec3 ic = B.c + B.ax[k] * (sg * B.h[k]);
        int u = (k + 1) % 3, w = (k + 2) % 3;
        std::vector<Vec3> poly = {ic + B.ax[u] * B.h[u] + B.ax[w] * B.h[w], ic - B.ax[u] * B.h[u] + B.ax[w] * B.h[w],
                                  ic - B.ax[u] * B.h[u] - B.ax[w] * B.h[w], ic + B.ax[u] * B.h[u] - B.ax[w] * B.h[w]};
        for (int s = 0; s < 3; s++) {
            // нормаль внутрь призмы для ребра s (обход CCW вокруг triN)
            Vec3 inward = cross(triN, e[s]);
            inward = normalize(inward);
            clipPoly(poly, -inward, dot(-inward, v[s]));
        }
        for (auto& p : poly) {
            float sep = dot(p - T.a, N);
            if (sep <= 0.01f) out.push_back({p, -sep});
        }
        if (out.empty()) out.push_back({closestOnTri(T, B.c), bestPen});
    } else {
        int i = (bi - 4) / 3, j = (bi - 4) % 3;
        Vec3 pb = B.c;
        for (int k = 0; k < 3; k++) if (k != i) pb += B.ax[k] * (dot(B.ax[k], bn) > 0 ? -B.h[k] : B.h[k]);
        Vec3 c1, c2;
        lineClosest(pb, B.ax[i], v[j], e[j], c1, c2);
        out.push_back({(c1 + c2) * 0.5f, bestPen});
    }
    reduce4(out, bn);
    return true;
}

struct Ext {
    Vec3 invI;            // локальная диагональ обратного тензора инерции
    float mu = 0.6f, e = 0.25f;
    bool bullet = false;
    float sleepT = 0;
    Vec3 ax[3];           // ось вращения (мировые), обновляется каждый подшаг
    Vec3 half2;           // bounding extents
};

struct Contact {
    int a, b;
    Vec3 p, n;
    float depth;
    Vec3 ra, rb, t1, t2;
    float mn = 0, mt1 = 0, mt2 = 0, bias = 0, jn = 0, j1 = 0, j2 = 0, approach = 0, mu = 0.5f;
};

struct JointX { int a, b; Vec3 la, lb, anchorW; Vec3 impulse; };

}  // namespace

Mat4 Body::transform() const { return trs(pos, rot, {1, 1, 1}); }

struct PhysicsWorld::Impl {
    std::vector<Body> bodies;
    std::vector<Ext> ext;
    std::vector<int> freeIds;
    std::vector<OBB> sboxes;
    std::vector<Tri> tris;
    std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
    float cell = 1.f;
    Vec3 mbmin, mbmax;
    mutable std::vector<uint32_t> mark;
    mutable uint32_t stamp = 0;
    float groundY = 0;
    bool ground = true;
    Vec3 g{0, -9.81f, 0};
    float acc = 0;
    std::vector<Contact> contacts, prev;
    std::vector<ContactEvent> events;
    std::vector<JointX> joints;
    int lastContactCount = 0;

    static uint64_t key(int x, int y, int z) {
        return ((uint64_t)(uint32_t)(x + 100000) << 42) ^ ((uint64_t)(uint32_t)(y + 100000) << 21) ^ (uint64_t)(uint32_t)(z + 100000);
    }
    void cellRange(Vec3 lo, Vec3 hi, int* a, int* b) const {
        a[0] = (int)std::floor((lo.x - mbmin.x) / cell); a[1] = (int)std::floor((lo.y - mbmin.y) / cell); a[2] = (int)std::floor((lo.z - mbmin.z) / cell);
        b[0] = (int)std::floor((hi.x - mbmin.x) / cell); b[1] = (int)std::floor((hi.y - mbmin.y) / cell); b[2] = (int)std::floor((hi.z - mbmin.z) / cell);
    }
    // Кандидаты-треугольники в AABB.
    void queryTris(Vec3 lo, Vec3 hi, std::vector<uint32_t>& out) const {
        out.clear();
        if (tris.empty()) return;
        if (hi.x < mbmin.x || hi.y < mbmin.y || hi.z < mbmin.z || lo.x > mbmax.x || lo.y > mbmax.y || lo.z > mbmax.z) return;
        int a[3], b[3];
        cellRange(lo, hi, a, b);
        int nc[3] = {(int)std::ceil((mbmax.x - mbmin.x) / cell), (int)std::ceil((mbmax.y - mbmin.y) / cell), (int)std::ceil((mbmax.z - mbmin.z) / cell)};
        for (int i = 0; i < 3; i++) { a[i] = std::max(a[i], 0); b[i] = std::min(b[i], std::max(nc[i] - 1, 0)); }
        long cnt = (long)(b[0] - a[0] + 1) * (b[1] - a[1] + 1) * (b[2] - a[2] + 1);
        if (cnt > 20000 || cnt <= 0) return;
        if (++stamp == 0) { std::fill(mark.begin(), mark.end(), 0); stamp = 1; }
        for (int x = a[0]; x <= b[0]; x++)
            for (int y = a[1]; y <= b[1]; y++)
                for (int z = a[2]; z <= b[2]; z++) {
                    auto it = grid.find(key(x, y, z));
                    if (it == grid.end()) continue;
                    for (uint32_t t : it->second) if (mark[t] != stamp) { mark[t] = stamp; out.push_back(t); }
                }
    }

    int add(const BodyDesc& d) {
        Body b;
        b.shape = d.shape; b.radius = std::max(d.radius, 0.005f);
        b.half = {std::max(d.half.x, 0.005f), std::max(d.half.y, 0.005f), std::max(d.half.z, 0.005f)};
        b.pos = d.pos; b.rot = normalize(d.rot); b.vel = d.vel; b.angVel = d.angVel;
        b.tag = d.tag; b.lifetime = d.lifetime; b.alive = true; b.age = 0;
        Ext x;
        x.mu = d.friction; x.e = d.restitution; x.bullet = d.bullet;
        if (d.mass > 0) {
            b.invMass = 1.f / d.mass;
            if (d.shape == ShapeType::Sphere) { float i = 0.4f * d.mass * b.radius * b.radius; x.invI = {1 / i, 1 / i, 1 / i}; }
            else {
                float m = d.mass; Vec3 h = b.half;
                x.invI = {3.f / (m * (h.y * h.y + h.z * h.z)), 3.f / (m * (h.x * h.x + h.z * h.z)), 3.f / (m * (h.x * h.x + h.y * h.y))};
            }
        } else { b.invMass = 0; x.invI = {0, 0, 0}; }
        if (!finite3(b.pos) || !finite3(b.vel)) return -1;
        int id;
        if (!freeIds.empty()) { id = freeIds.back(); freeIds.pop_back(); bodies[id] = b; ext[id] = x; }
        else { id = (int)bodies.size(); bodies.push_back(b); ext.push_back(x); }
        return id;
    }

    bool dyn(int i) const { return i >= 0 && bodies[i].alive && bodies[i].invMass > 0; }
    bool frozen(int i) const { return bodies[i].sleeping || bodies[i].invMass <= 0; }
    float invM(int i) const { return frozen(i) ? 0.f : bodies[i].invMass; }

    void axes(int i) {
        const Quat& q = bodies[i].rot;
        ext[i].ax[0] = rotate(q, {1, 0, 0}); ext[i].ax[1] = rotate(q, {0, 1, 0}); ext[i].ax[2] = rotate(q, {0, 0, 1});
    }
    Vec3 applyInvI(int i, Vec3 v) const {
        if (frozen(i)) return Vec3();
        const Ext& e = ext[i];
        return e.ax[0] * (dot(e.ax[0], v) * e.invI.x) + e.ax[1] * (dot(e.ax[1], v) * e.invI.y) + e.ax[2] * (dot(e.ax[2], v) * e.invI.z);
    }
    OBB obb(int i) const {
        OBB o; o.c = bodies[i].pos;
        for (int k = 0; k < 3; k++) o.ax[k] = ext[i].ax[k];
        o.h[0] = bodies[i].half.x; o.h[1] = bodies[i].half.y; o.h[2] = bodies[i].half.z;
        return o;
    }
    void aabb(int i, Vec3& lo, Vec3& hi) const {
        const Body& b = bodies[i];
        Vec3 e;
        if (b.shape == ShapeType::Sphere) e = {b.radius, b.radius, b.radius};
        else {
            const Ext& x = ext[i];
            e = {std::fabs(x.ax[0].x) * b.half.x + std::fabs(x.ax[1].x) * b.half.y + std::fabs(x.ax[2].x) * b.half.z,
                 std::fabs(x.ax[0].y) * b.half.x + std::fabs(x.ax[1].y) * b.half.y + std::fabs(x.ax[2].y) * b.half.z,
                 std::fabs(x.ax[0].z) * b.half.x + std::fabs(x.ax[1].z) * b.half.y + std::fabs(x.ax[2].z) * b.half.z};
        }
        lo = b.pos - e; hi = b.pos + e;
    }

    void emit(int a, int b, Vec3 p, Vec3 n, float depth) {
        if (!(depth > -0.012f) || !finite3(p) || !finite3(n)) return;
        Contact c; c.a = a; c.b = b; c.p = p; c.n = n; c.depth = depth;
        c.mu = ext[a].mu;
        contacts.push_back(c);
    }

    // ---- сферы ----
    void sphereVsPlane(int i) {
        const Body& b = bodies[i];
        float d = b.pos.y - b.radius - groundY;
        if (d < 0) emit(i, -1, {b.pos.x, groundY, b.pos.z}, {0, 1, 0}, -d);
    }
    void sphereVsOBB(int i, int j, const OBB& B) {  // j = индекс бокса или -1
        const Body& s = bodies[i];
        Vec3 rel = s.pos - B.c;
        Vec3 loc(dot(rel, B.ax[0]), dot(rel, B.ax[1]), dot(rel, B.ax[2]));
        Vec3 cl(clampf(loc.x, -B.h[0], B.h[0]), clampf(loc.y, -B.h[1], B.h[1]), clampf(loc.z, -B.h[2], B.h[2]));
        Vec3 dl = loc - cl;
        float d2 = dot(dl, dl);
        if (d2 > s.radius * s.radius) return;
        Vec3 n, p;
        float depth;
        if (d2 > 1e-10f) {
            float d = std::sqrt(d2);
            Vec3 nl = dl * (1.f / d);
            n = B.ax[0] * nl.x + B.ax[1] * nl.y + B.ax[2] * nl.z;
            p = B.c + B.ax[0] * cl.x + B.ax[1] * cl.y + B.ax[2] * cl.z;
            depth = s.radius - d;
        } else {  // центр внутри бокса
            float bestD = 1e30f; int k = 0; float sgn = 1;
            for (int a = 0; a < 3; a++) {
                float la = comp(loc, a);
                float dp = B.h[a] - la, dn = B.h[a] + la;
                if (dp < bestD) { bestD = dp; k = a; sgn = 1; }
                if (dn < bestD) { bestD = dn; k = a; sgn = -1; }
            }
            n = B.ax[k] * sgn;
            p = s.pos - n * bestD;
            depth = bestD + s.radius;
        }
        emit(i, j, p, n, depth);
    }
    void sphereVsTri(int i, const Tri& t, size_t firstNew) {
        const Body& s = bodies[i];
        Vec3 q = closestOnTri(t, s.pos);
        Vec3 d = s.pos - q;
        float d2 = dot(d, d);
        if (d2 > s.radius * s.radius) return;
        Vec3 n;
        float dist = std::sqrt(d2);
        if (dist > 1e-6f) n = d * (1.f / dist);
        else { n = normalize(cross(t.b - t.a, t.c - t.a)); if (dot(n, bodies[i].vel) > 0) n = -n; }
        float depth = s.radius - dist;
        // слияние дубликатов на общих рёбрах соседних треугольников
        for (size_t k = firstNew; k < contacts.size(); k++) {
            Contact& c = contacts[k];
            if (c.a == i && c.b == -1 && dot(c.n, n) > 0.97f && length(c.p - q) < 0.02f + 0.1f * s.radius) {
                if (depth > c.depth) { c.depth = depth; c.p = q; c.n = n; }
                return;
            }
        }
        emit(i, -1, q, n, depth);
    }

    // ---- боксы против статики ----
    void boxVsPlane(int i) {
        const Body& b = bodies[i];
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sy = -1; sy <= 1; sy += 2)
                for (int sz = -1; sz <= 1; sz += 2) {
                    Vec3 p = b.pos + ext[i].ax[0] * (sx * b.half.x) + ext[i].ax[1] * (sy * b.half.y) + ext[i].ax[2] * (sz * b.half.z);
                    if (p.y < groundY + 0.01f) emit(i, -1, p, {0, 1, 0}, groundY - p.y);
                }
    }
    void boxVsBox(int i, int j, const OBB& A, const OBB& B) {  // j<0: статический
        std::vector<CP> pts;
        Vec3 n;
        if (!boxBox(A, B, pts, n)) return;
        for (auto& p : pts) emit(i, j, p.p, n, p.depth);
    }
    void boxVsTri(int i, const Tri& t) {
        std::vector<CP> pts;
        Vec3 n;
        if (!boxTri(obb(i), t, pts, n)) return;
        for (auto& p : pts) emit(i, -1, p.p, n, p.depth);
    }

    void collideStatic(int i) {
        const Body& b = bodies[i];
        Vec3 lo, hi;
        aabb(i, lo, hi);
        if (ground) { if (b.shape == ShapeType::Sphere) sphereVsPlane(i); else boxVsPlane(i); }
        for (const OBB& sb : sboxes) {
            float r = std::fabs(sb.ax[0].x) * sb.h[0] + std::fabs(sb.ax[1].x) * sb.h[1] + std::fabs(sb.ax[2].x) * sb.h[2];
            float ry = std::fabs(sb.ax[0].y) * sb.h[0] + std::fabs(sb.ax[1].y) * sb.h[1] + std::fabs(sb.ax[2].y) * sb.h[2];
            float rz = std::fabs(sb.ax[0].z) * sb.h[0] + std::fabs(sb.ax[1].z) * sb.h[1] + std::fabs(sb.ax[2].z) * sb.h[2];
            if (hi.x < sb.c.x - r || lo.x > sb.c.x + r || hi.y < sb.c.y - ry || lo.y > sb.c.y + ry || hi.z < sb.c.z - rz || lo.z > sb.c.z + rz) continue;
            if (b.shape == ShapeType::Sphere) sphereVsOBB(i, -1, sb);
            else boxVsBox(i, -1, obb(i), sb);
        }
        if (!tris.empty()) {
            static thread_local std::vector<uint32_t> cand;
            queryTris(lo, hi, cand);
            size_t first = contacts.size();
            for (uint32_t t : cand) {
                const Tri& tr = tris[t];
                if (std::max({tr.a.x, tr.b.x, tr.c.x}) < lo.x || std::min({tr.a.x, tr.b.x, tr.c.x}) > hi.x ||
                    std::max({tr.a.y, tr.b.y, tr.c.y}) < lo.y || std::min({tr.a.y, tr.b.y, tr.c.y}) > hi.y ||
                    std::max({tr.a.z, tr.b.z, tr.c.z}) < lo.z || std::min({tr.a.z, tr.b.z, tr.c.z}) > hi.z) continue;
                if (b.shape == ShapeType::Sphere) sphereVsTri(i, tr, first);
                else boxVsTri(i, tr);
            }
        }
    }

    void collidePair(int i, int j) {
        Body &A = bodies[i], &B = bodies[j];
        if (A.shape == ShapeType::Sphere && B.shape == ShapeType::Sphere) {
            Vec3 d = A.pos - B.pos;
            float r = A.radius + B.radius, d2 = dot(d, d);
            if (d2 >= r * r) return;
            float dist = std::sqrt(d2);
            Vec3 n = dist > 1e-6f ? d * (1.f / dist) : Vec3(0, 1, 0);
            emit(i, j, B.pos + n * B.radius, n, r - dist);
        } else if (A.shape == ShapeType::Sphere) {
            sphereBodyBox(i, j);
        } else if (B.shape == ShapeType::Sphere) {
            sphereBodyBox(j, i);
        } else {
            boxVsBox(i, j, obb(i), obb(j));
        }
    }
    void sphereBodyBox(int s, int bx) {  // контакт: a = сфера, b = бокс; n от бокса к сфере
        size_t before = contacts.size();
        sphereVsOBB(s, bx, obb(bx));
        (void)before;
    }

    // ---- пары тел (sweep and prune по X) ----
    void broadphase() {
        static thread_local std::vector<int> idx;
        static thread_local std::vector<Vec3> los, his;
        idx.clear();
        los.resize(bodies.size()); his.resize(bodies.size());
        for (int i = 0; i < (int)bodies.size(); i++) {
            if (!bodies[i].alive) continue;
            aabb(i, los[i], his[i]);
            idx.push_back(i);
        }
        std::sort(idx.begin(), idx.end(), [&](int a, int b) { return los[a].x < los[b].x; });
        for (size_t u = 0; u < idx.size(); u++) {
            int i = idx[u];
            for (size_t v = u + 1; v < idx.size(); v++) {
                int j = idx[v];
                if (los[j].x > his[i].x) break;
                if (los[j].y > his[i].y || his[j].y < los[i].y || los[j].z > his[i].z || his[j].z < los[i].z) continue;
                bool di = dyn(i), dj = dyn(j);
                if (!di && !dj) continue;
                bool ai = di && !bodies[i].sleeping, aj = dj && !bodies[j].sleeping;
                if (!ai && !aj) continue;
                if (!di) collidePairOrdered(j, i); else collidePairOrdered(i, j);
            }
        }
    }
    void collidePairOrdered(int i, int j) { collidePair(i, j); }

    void wake(int i) {
        if (i < 0 || !bodies[i].alive) return;
        bodies[i].sleeping = false; ext[i].sleepT = 0;
    }

    // ---- решатель ----
    Vec3 velAt(int i, Vec3 r) const { return bodies[i].vel + cross(bodies[i].angVel, r); }

    void prepare(Contact& c, float h) {
        const Body& A = bodies[c.a];
        c.ra = c.p - A.pos;
        bool bd = c.b >= 0;
        c.rb = bd ? c.p - bodies[c.b].pos : Vec3();
        c.mu = bd ? std::sqrt(std::max(0.f, ext[c.a].mu * ext[c.b].mu)) : ext[c.a].mu;
        float e = bd ? std::max(ext[c.a].e, ext[c.b].e) : ext[c.a].e;
        Vec3 ref = std::fabs(c.n.x) > 0.7f ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
        c.t1 = normalize(cross(c.n, ref)); c.t2 = cross(c.n, c.t1);
        auto kk = [&](Vec3 d) {
            float k = invM(c.a);
            Vec3 ra_d = cross(c.ra, d);
            k += dot(cross(applyInvI(c.a, ra_d), c.ra), d);
            if (bd) { k += invM(c.b); Vec3 rb_d = cross(c.rb, d); k += dot(cross(applyInvI(c.b, rb_d), c.rb), d); }
            return k > 1e-12f ? 1.f / k : 0.f;
        };
        c.mn = kk(c.n); c.mt1 = kk(c.t1); c.mt2 = kk(c.t2);
        Vec3 vr = velAt(c.a, c.ra) - (bd ? velAt(c.b, c.rb) : Vec3());
        float vn = dot(vr, c.n);
        c.approach = -vn;
        if (bd && bodies[c.b].sleeping && -vn > 0.4f) wake(c.b);
        if (bodies[c.a].sleeping && -vn > 0.4f) wake(c.a);
        c.bias = c.depth > 0 ? std::min(3.f, kBeta / h * std::max(0.f, c.depth - kSlop)) : c.depth / h;
        if (-vn > 1.0f && c.depth > 0) c.bias = std::max(c.bias, e * -vn);
        // warm start
        for (const Contact& p : prev)
            if (p.a == c.a && p.b == c.b && length(p.p - c.p) < 0.03f && dot(p.n, c.n) > 0.9f) { c.jn = p.jn * 0.9f; c.j1 = p.j1 * 0.9f; c.j2 = p.j2 * 0.9f; break; }
        applyImp(c, c.n * c.jn + c.t1 * c.j1 + c.t2 * c.j2);
    }
    void applyImp(const Contact& c, Vec3 J) {
        Body& A = bodies[c.a];
        A.vel += J * invM(c.a); A.angVel += applyInvI(c.a, cross(c.ra, J));
        if (c.b >= 0) { Body& B = bodies[c.b]; B.vel += J * -invM(c.b); B.angVel += applyInvI(c.b, cross(c.rb, -J)); }
    }
    void solveContact(Contact& c) {
        bool bd = c.b >= 0;
        auto rel = [&]() { return velAt(c.a, c.ra) - (bd ? velAt(c.b, c.rb) : Vec3()); };
        // трение
        float maxF = c.mu * c.jn;
        float v1 = dot(rel(), c.t1);
        float d1 = -v1 * c.mt1;
        float n1 = clampf(c.j1 + d1, -maxF, maxF);
        d1 = n1 - c.j1; c.j1 = n1;
        applyImp(c, c.t1 * d1);
        float v2 = dot(rel(), c.t2);
        float d2 = -v2 * c.mt2;
        float n2 = clampf(c.j2 + d2, -maxF, maxF);
        d2 = n2 - c.j2; c.j2 = n2;
        applyImp(c, c.t2 * d2);
        // нормаль
        float vn = dot(rel(), c.n);
        float dj = c.mn * (-vn + c.bias);
        float nj = std::max(0.f, c.jn + dj);
        dj = nj - c.jn; c.jn = nj;
        applyImp(c, c.n * dj);
    }
    void solveJoint(JointX& j, float h) {
        Vec3 ra = rotate(bodies[j.a].rot, j.la);
        Vec3 rb = j.b >= 0 ? rotate(bodies[j.b].rot, j.lb) : j.lb;
        Vec3 pa = bodies[j.a].pos + ra, pb = j.b >= 0 ? bodies[j.b].pos + rb : j.anchorW;
        Vec3 va = velAt(j.a, ra), vb = j.b >= 0 ? velAt(j.b, rb) : Vec3();
        Vec3 err = pa - pb;
        Vec3 rhs = -(va - vb) - err * (kBeta * 2.f / h * 0.5f);
        // K = (mA+mB) I - [ra] IA [ra] - [rb] IB [rb]
        Mat3 K{{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}};
        float m = invM(j.a) + (j.b >= 0 ? invM(j.b) : 0.f);
        for (int c = 0; c < 3; c++) {
            Vec3 e(c == 0, c == 1, c == 2);
            Vec3 col = e * m;
            Vec3 t = cross(ra, applyInvI(j.a, cross(e, ra)));  // -[ra]IA[ra] e = ra x (IA (e x ra))... знак учтён ниже
            col += t;
            if (j.b >= 0) col += cross(rb, applyInvI(j.b, cross(e, rb)));
            K.c[c] = col;
        }
        Vec3 imp = mul(inv3(K), rhs);
        if (!finite3(imp)) return;
        Body& A = bodies[j.a];
        A.vel += imp * invM(j.a); A.angVel += applyInvI(j.a, cross(ra, imp));
        if (j.b >= 0) { Body& B = bodies[j.b]; B.vel += imp * -invM(j.b); B.angVel += applyInvI(j.b, cross(rb, -imp)); }
    }

    // ---- непрерывные столкновения: шагаем по траектории, останавливаемся на первом касании ----
    bool overlapsAt(int i, Vec3 pos) {
        Vec3 old = bodies[i].pos;
        bodies[i].pos = pos;
        size_t n0 = contacts.size();
        collideStatic(i);
        for (int j = 0; j < (int)bodies.size(); j++) {
            if (j == i || !bodies[j].alive) continue;
            Vec3 d = bodies[j].pos - pos;
            float r = (bodies[i].shape == ShapeType::Sphere ? bodies[i].radius : length(bodies[i].half)) +
                      (bodies[j].shape == ShapeType::Sphere ? bodies[j].radius : length(bodies[j].half));
            if (dot(d, d) > r * r) continue;
            collidePair(i, j);
        }
        bool hit = false;
        for (size_t k = n0; k < contacts.size(); k++) if (contacts[k].depth > 1e-4f) hit = true;
        contacts.resize(n0);
        bodies[i].pos = old;
        return hit;
    }
    void sweep(int i, float h) {
        Body& b = bodies[i];
        Vec3 d = b.vel * h;
        float len = length(d);
        float r = b.shape == ShapeType::Sphere ? b.radius : std::min({b.half.x, b.half.y, b.half.z});
        if (len < r * 0.9f && !ext[i].bullet) return;
        if (len < 1e-5f) return;
        if (overlapsAt(i, b.pos)) return;  // уже в контакте: обычный решатель
        float step = r * 0.5f;
        int n = std::min(512, (int)std::ceil(len / step));
        for (int s = 1; s <= n; s++) {
            Vec3 p = b.pos + d * ((float)s / n);
            if (overlapsAt(i, p)) { b.pos = p; return; }
        }
    }

    void substep(float h) {
        int N = (int)bodies.size();
        for (int i = 0; i < N; i++) {
            Body& b = bodies[i];
            if (!b.alive) continue;
            axes(i);
            if (b.invMass > 0 && !b.sleeping) {
                b.vel += g * h;
                b.vel = b.vel * (1.f / (1.f + 0.02f * h));
                b.angVel = b.angVel * (1.f / (1.f + 0.08f * h));
                float w = length(b.angVel);
                if (w > 60.f) b.angVel = b.angVel * (60.f / w);
                float v = length(b.vel);
                if (v > 300.f) b.vel = b.vel * (300.f / v);
            }
        }
        for (int i = 0; i < N; i++) if (dyn(i) && !bodies[i].sleeping) sweep(i, h);
        prev.swap(contacts);
        contacts.clear();
        for (int i = 0; i < N; i++) if (dyn(i) && !bodies[i].sleeping) collideStatic(i);
        broadphase();
        for (Contact& c : contacts) prepare(c, h);
        for (int it = 0; it < kIters; it++) {
            for (auto& j : joints) solveJoint(j, h);
            for (Contact& c : contacts) solveContact(c);
        }
        if (joints.size()) for (auto& j : joints) { (void)j; }
        // события
        for (const Contact& c : contacts)
            if (c.approach > 1.0f && c.jn > 0 && events.size() < 64) events.push_back({c.p, c.n, c.jn, c.approach, c.a, c.b});
        // интегрирование
        for (int i = 0; i < N; i++) {
            Body& b = bodies[i];
            if (!b.alive || b.invMass <= 0 || b.sleeping) continue;
            b.pos += b.vel * h;
            Quat w(b.angVel.x, b.angVel.y, b.angVel.z, 0);
            Quat dq = w * b.rot;
            b.rot = normalize(Quat(b.rot.x + 0.5f * h * dq.x, b.rot.y + 0.5f * h * dq.y, b.rot.z + 0.5f * h * dq.z, b.rot.w + 0.5f * h * dq.w));
            if (!finite3(b.pos) || !finite3(b.vel)) { b.alive = false; freeIds.push_back(i); }
        }
        updateSleep(h);
    }

    void updateSleep(float h) {
        int N = (int)bodies.size();
        static thread_local std::vector<int> parent;
        parent.resize(N);
        for (int i = 0; i < N; i++) parent[i] = i;
        auto find = [&](int x) { while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; } return x; };
        for (const Contact& c : contacts) if (c.b >= 0 && dyn(c.a) && dyn(c.b)) parent[find(c.a)] = find(c.b);
        for (const auto& j : joints) if (j.b >= 0) parent[find(j.a)] = find(j.b);
        static thread_local std::vector<float> minT;
        minT.assign(N, 1e30f);
        for (int i = 0; i < N; i++) {
            if (!dyn(i) || bodies[i].sleeping) continue;
            Body& b = bodies[i];
            float lin = dot(b.vel, b.vel), ang = dot(b.angVel, b.angVel);
            if (lin < 0.0036f && ang < 0.0144f) ext[i].sleepT += h; else ext[i].sleepT = 0;
            int r = find(i);
            minT[r] = std::min(minT[r], ext[i].sleepT);
        }
        // тело без контактов в воздухе не должно засыпать
        std::vector<char> touching(N, 0);
        for (const Contact& c : contacts) { touching[c.a] = 1; if (c.b >= 0) touching[c.b] = 1; }
        for (const auto& j : joints) { touching[j.a] = 1; if (j.b >= 0) touching[j.b] = 1; }
        for (int i = 0; i < N; i++) {
            if (!dyn(i) || bodies[i].sleeping) continue;
            if (!touching[i]) { ext[i].sleepT = 0; continue; }
            if (minT[find(i)] > 0.5f) { bodies[i].sleeping = true; bodies[i].vel = {}; bodies[i].angVel = {}; }
        }
    }

    void step(float dt) {
        events.clear();
        acc += std::min(dt, 0.1f);
        int n = 0;
        while (acc >= kH && n < kMaxSub) {
            for (int i = 0; i < (int)bodies.size(); i++) {
                Body& b = bodies[i];
                if (!b.alive) continue;
                b.age += kH;
                if (b.lifetime >= 0 && b.age > b.lifetime) { b.alive = false; freeIds.push_back(i); }
            }
            substep(kH);
            acc -= kH; n++;
        }
        if (acc > kH) acc = 0;
        lastContactCount = (int)contacts.size();
    }
};

// ---------------------------------------------------------------------------------------------
PhysicsWorld::PhysicsWorld() : impl_(new Impl) {}
PhysicsWorld::~PhysicsWorld() = default;
void PhysicsWorld::setGroundY(float y) { impl_->groundY = y; }
void PhysicsWorld::setGroundEnabled(bool on) { impl_->ground = on; }
void PhysicsWorld::setGravity(Vec3 g) { impl_->g = g; }
int PhysicsWorld::addBody(const BodyDesc& d) { return impl_->add(d); }
void PhysicsWorld::removeBody(int id) {
    if (id < 0 || id >= (int)impl_->bodies.size() || !impl_->bodies[id].alive) return;
    impl_->bodies[id].alive = false;
    impl_->freeIds.push_back(id);
    auto& js = impl_->joints;
    js.erase(std::remove_if(js.begin(), js.end(), [&](const JointX& j) { return j.a == id || j.b == id; }), js.end());
}
void PhysicsWorld::clearBodies() {
    impl_->bodies.clear(); impl_->ext.clear(); impl_->freeIds.clear(); impl_->joints.clear();
    impl_->contacts.clear(); impl_->prev.clear();
}
int PhysicsWorld::addStaticBox(Vec3 pos, Quat rot, Vec3 half) {
    impl_->sboxes.push_back(makeOBB(pos, normalize(rot), half));
    return (int)impl_->sboxes.size() - 1;
}
void PhysicsWorld::clearStaticBoxes() { impl_->sboxes.clear(); }

void PhysicsWorld::setStaticMesh(const std::vector<Vec3>& verts, const std::vector<uint32_t>& idx) {
    Impl& m = *impl_;
    clearStaticMesh();
    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    double edgeSum = 0; size_t edgeN = 0;
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        if (idx[i] >= verts.size() || idx[i + 1] >= verts.size() || idx[i + 2] >= verts.size()) continue;
        Tri t{verts[idx[i]], verts[idx[i + 1]], verts[idx[i + 2]]};
        if (!finite3(t.a) || !finite3(t.b) || !finite3(t.c)) continue;
        if (length(cross(t.b - t.a, t.c - t.a)) < 1e-12f) continue;
        m.tris.push_back(t);
        for (Vec3 p : {t.a, t.b, t.c}) {
            lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
            hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
        }
        edgeSum += length(t.b - t.a) + length(t.c - t.b); edgeN += 2;
    }
    if (m.tris.empty()) return;
    m.mbmin = lo - Vec3(0.01f, 0.01f, 0.01f); m.mbmax = hi + Vec3(0.01f, 0.01f, 0.01f);
    Vec3 ext = m.mbmax - m.mbmin;
    float avg = (float)(edgeSum / std::max<size_t>(1, edgeN));
    m.cell = std::max({avg * 2.f, std::max({ext.x, ext.y, ext.z}) / 96.f, 0.05f});
    m.mark.assign(m.tris.size(), 0);
    m.stamp = 0;
    for (size_t t = 0; t < m.tris.size(); t++) {
        const Tri& tr = m.tris[t];
        Vec3 tl(std::min({tr.a.x, tr.b.x, tr.c.x}), std::min({tr.a.y, tr.b.y, tr.c.y}), std::min({tr.a.z, tr.b.z, tr.c.z}));
        Vec3 th(std::max({tr.a.x, tr.b.x, tr.c.x}), std::max({tr.a.y, tr.b.y, tr.c.y}), std::max({tr.a.z, tr.b.z, tr.c.z}));
        int a[3], b[3];
        m.cellRange(tl, th, a, b);
        for (int x = a[0]; x <= b[0]; x++)
            for (int y = a[1]; y <= b[1]; y++)
                for (int z = a[2]; z <= b[2]; z++) m.grid[Impl::key(x, y, z)].push_back((uint32_t)t);
    }
}
void PhysicsWorld::clearStaticMesh() { impl_->tris.clear(); impl_->grid.clear(); impl_->mark.clear(); }
int PhysicsWorld::staticTriangleCount() const { return (int)impl_->tris.size(); }
void PhysicsWorld::step(float dt) { impl_->step(dt); }

void PhysicsWorld::applyImpulse(int id, Vec3 J, Vec3 p) {
    Impl& m = *impl_;
    if (id < 0 || id >= (int)m.bodies.size() || !m.dyn(id) || !finite3(J)) return;
    m.axes(id);
    Body& b = m.bodies[id];
    m.wake(id);
    b.vel += J * b.invMass;
    b.angVel += m.applyInvI(id, cross(p - b.pos, J));
}
void PhysicsWorld::explosion(Vec3 c, float radius, float impulse) {
    Impl& m = *impl_;
    for (int i = 0; i < (int)m.bodies.size(); i++) {
        if (!m.dyn(i)) continue;
        Body& b = m.bodies[i];
        Vec3 d = b.pos - c;
        float dist = length(d);
        if (dist > radius) continue;
        float f = 1.f - dist / radius;
        Vec3 dir = normalize(dist > 1e-4f ? d + Vec3(0, 0.35f * radius * 0.25f, 0) : Vec3(0, 1, 0));
        Vec3 side = normalize(cross(dir, Vec3(0.3f, 1, 0.2f)));
        float size = b.shape == ShapeType::Sphere ? b.radius : length(b.half);
        applyImpulse(i, dir * (impulse * f * f), b.pos + side * (0.4f * size));
    }
}
int PhysicsWorld::addBallJoint(int a, int b, Vec3 anchor) {
    Impl& m = *impl_;
    if (a < 0 || a >= (int)m.bodies.size() || !m.bodies[a].alive) return -1;
    if (b >= (int)m.bodies.size() || (b >= 0 && !m.bodies[b].alive)) return -1;
    JointX j;
    j.a = a; j.b = b; j.anchorW = anchor;
    j.la = rotate(conj(m.bodies[a].rot), anchor - m.bodies[a].pos);
    j.lb = b >= 0 ? rotate(conj(m.bodies[b].rot), anchor - m.bodies[b].pos) : anchor;
    m.joints.push_back(j);
    return (int)m.joints.size() - 1;
}
void PhysicsWorld::clearJoints() { impl_->joints.clear(); }
void PhysicsWorld::wakeAll() { for (int i = 0; i < (int)impl_->bodies.size(); i++) impl_->wake(i); }
void PhysicsWorld::wakeBody(int id) { impl_->wake(id); }

bool PhysicsWorld::raycast(Vec3 o, Vec3 dir, float maxT, RayHit& hit) const {
    const Impl& m = *impl_;
    dir = normalize(dir);
    float best = maxT;
    bool found = false;
    auto upd = [&](float t, Vec3 n, int body) {
        if (t >= 0 && t < best) { best = t; hit.t = t; hit.point = o + dir * t; hit.normal = n; hit.body = body; found = true; }
    };
    if (m.ground && std::fabs(dir.y) > 1e-6f) { float t = (m.groundY - o.y) / dir.y; if (t > 0) upd(t, {0, 1, 0}, -1); }
    auto rayOBB = [&](const OBB& B, int body) {
        Vec3 rel = o - B.c;
        float tmin = 0, tmax = best;
        Vec3 nrm;
        for (int k = 0; k < 3; k++) {
            float po = dot(rel, B.ax[k]), pd = dot(dir, B.ax[k]);
            if (std::fabs(pd) < 1e-8f) { if (std::fabs(po) > B.h[k]) return; continue; }
            float t1 = (-B.h[k] - po) / pd, t2 = (B.h[k] - po) / pd;
            float sgn = -1;
            if (t1 > t2) { std::swap(t1, t2); sgn = 1; }
            if (t1 > tmin) { tmin = t1; nrm = B.ax[k] * (sgn * (pd > 0 ? -1.f : 1.f) * -1.f); nrm = B.ax[k] * (pd > 0 ? -1.f : 1.f); }
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return;
        }
        if (tmin > 0) upd(tmin, nrm, body);
    };
    for (const OBB& b : m.sboxes) rayOBB(b, -1);
    for (int i = 0; i < (int)m.bodies.size(); i++) {
        const Body& b = m.bodies[i];
        if (!b.alive) continue;
        if (b.shape == ShapeType::Sphere) {
            Vec3 oc = o - b.pos;
            float bq = dot(oc, dir), c = dot(oc, oc) - b.radius * b.radius, disc = bq * bq - c;
            if (disc < 0) continue;
            float t = -bq - std::sqrt(disc);
            if (t > 0) upd(t, normalize(o + dir * t - b.pos), i);
        } else {
            OBB B = makeOBB(b.pos, b.rot, b.half);
            rayOBB(B, i);
        }
    }
    if (!m.tris.empty()) {
        auto testTri = [&](uint32_t t) {
            const Tri& tr = m.tris[t];
            Vec3 e1 = tr.b - tr.a, e2 = tr.c - tr.a, pv = cross(dir, e2);
            float det = dot(e1, pv);
            if (std::fabs(det) < 1e-12f) return;
            float id = 1.f / det;
            Vec3 tv = o - tr.a;
            float u = dot(tv, pv) * id;
            if (u < 0 || u > 1) return;
            Vec3 qv = cross(tv, e1);
            float v = dot(dir, qv) * id;
            if (v < 0 || u + v > 1) return;
            float tt = dot(e2, qv) * id;
            Vec3 n = normalize(cross(e1, e2));
            if (dot(n, dir) > 0) n = -n;
            upd(tt, n, -1);
        };
        // обрезаем луч по границам сетки и идём по ячейкам (3D DDA)
        float t0 = 0, t1 = best;
        const float dd[3] = {dir.x, dir.y, dir.z}, oo[3] = {o.x, o.y, o.z}, lo3[3] = {m.mbmin.x, m.mbmin.y, m.mbmin.z}, hi3[3] = {m.mbmax.x, m.mbmax.y, m.mbmax.z};
        bool ok = true;
        for (int k = 0; k < 3 && ok; k++) {
            if (std::fabs(dd[k]) < 1e-9f) { if (oo[k] < lo3[k] || oo[k] > hi3[k]) ok = false; continue; }
            float a1 = (lo3[k] - oo[k]) / dd[k], a2 = (hi3[k] - oo[k]) / dd[k];
            if (a1 > a2) std::swap(a1, a2);
            t0 = std::max(t0, a1); t1 = std::min(t1, a2);
            if (t0 > t1) ok = false;
        }
        if (ok) {
            Vec3 ps = o + dir * (t0 + 1e-4f);
            int ci[3] = {(int)std::floor((ps.x - m.mbmin.x) / m.cell), (int)std::floor((ps.y - m.mbmin.y) / m.cell), (int)std::floor((ps.z - m.mbmin.z) / m.cell)};
            int st[3]; float tMax[3], tDelta[3];
            for (int k = 0; k < 3; k++) {
                st[k] = dd[k] > 0 ? 1 : -1;
                if (std::fabs(dd[k]) < 1e-9f) { tMax[k] = 1e30f; tDelta[k] = 1e30f; continue; }
                float next = lo3[k] + (ci[k] + (dd[k] > 0 ? 1 : 0)) * m.cell;
                tMax[k] = (next - oo[k]) / dd[k];
                tDelta[k] = m.cell / std::fabs(dd[k]);
            }
            for (int iter = 0; iter < 20000; iter++) {
                auto it = m.grid.find(Impl::key(ci[0], ci[1], ci[2]));
                if (it != m.grid.end()) for (uint32_t t : it->second) testTri(t);
                int ax = tMax[0] < tMax[1] ? (tMax[0] < tMax[2] ? 0 : 2) : (tMax[1] < tMax[2] ? 1 : 2);
                if (tMax[ax] > std::min(t1, best)) break;
                ci[ax] += st[ax]; tMax[ax] += tDelta[ax];
            }
        }
    }
    return found;
}

const std::vector<Body>& PhysicsWorld::bodies() const { return impl_->bodies; }
const std::vector<ContactEvent>& PhysicsWorld::events() const { return impl_->events; }
int PhysicsWorld::aliveCount() const { int n = 0; for (auto& b : impl_->bodies) n += b.alive && b.invMass > 0; return n; }
int PhysicsWorld::awakeCount() const { int n = 0; for (auto& b : impl_->bodies) n += b.alive && b.invMass > 0 && !b.sleeping; return n; }
int PhysicsWorld::contactCount() const { return impl_->lastContactCount; }

}  // namespace eng
