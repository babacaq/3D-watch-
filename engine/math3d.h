// Мини-библиотека математики: Vec3, Quat, Mat4 (column-major, как в OpenGL).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>

namespace eng {

constexpr float kPi = 3.14159265358979323846f;

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
};
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(const Vec3& a) {
    float l = length(a);
    return l > 1e-12f ? a * (1.0f / l) : Vec3(0, 1, 0);
}
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float smoothstep(float a, float b, float x) {
    float t = clampf((x - a) / (b - a), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
    Quat() = default;
    Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
};
inline Quat operator*(const Quat& a, const Quat& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
inline float dot(const Quat& a, const Quat& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
inline Quat normalize(const Quat& q) {
    float l = std::sqrt(dot(q, q));
    if (l < 1e-12f) return Quat();
    float i = 1.f / l;
    return {q.x * i, q.y * i, q.z * i, q.w * i};
}
inline Quat quatAxisAngle(const Vec3& axis, float a) {
    float s = std::sin(a * 0.5f);
    Vec3 n = normalize(axis);
    return {n.x * s, n.y * s, n.z * s, std::cos(a * 0.5f)};
}
// Эйлер: сначала вращение вокруг X, затем Y, затем Z.
inline Quat quatEuler(float rx, float ry, float rz) {
    return quatAxisAngle({0, 0, 1}, rz) * quatAxisAngle({0, 1, 0}, ry) * quatAxisAngle({1, 0, 0}, rx);
}
inline Quat nlerp(const Quat& a, Quat b, float t) {
    if (dot(a, b) < 0) b = {-b.x, -b.y, -b.z, -b.w};
    return normalize(Quat(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                          a.w + (b.w - a.w) * t));
}
inline Quat slerp(const Quat& a, Quat b, float t) {
    float d = dot(a, b);
    if (d < 0) { b = {-b.x, -b.y, -b.z, -b.w}; d = -d; }
    if (d > 0.9995f) return nlerp(a, b, t);
    float th = std::acos(d), s = std::sin(th);
    float wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
    return {a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
}

// m[col*4 + row]
struct Mat4 {
    float m[16];
    Mat4() { std::memset(m, 0, sizeof m); m[0] = m[5] = m[10] = m[15] = 1.f; }
    float& at(int r, int c) { return m[c * 4 + r]; }
    float at(int r, int c) const { return m[c * 4 + r]; }
};
inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; c++)
        for (int rw = 0; rw < 4; rw++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + rw] * b.m[c * 4 + k];
            r.m[c * 4 + rw] = s;
        }
    return r;
}
inline Vec3 transformPoint(const Mat4& a, const Vec3& p) {
    return {a.m[0] * p.x + a.m[4] * p.y + a.m[8] * p.z + a.m[12],
            a.m[1] * p.x + a.m[5] * p.y + a.m[9] * p.z + a.m[13],
            a.m[2] * p.x + a.m[6] * p.y + a.m[10] * p.z + a.m[14]};
}
inline Vec3 transformDir(const Mat4& a, const Vec3& p) {
    return {a.m[0] * p.x + a.m[4] * p.y + a.m[8] * p.z, a.m[1] * p.x + a.m[5] * p.y + a.m[9] * p.z,
            a.m[2] * p.x + a.m[6] * p.y + a.m[10] * p.z};
}
inline Mat4 translate(const Vec3& t) {
    Mat4 r; r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z; return r;
}
inline Mat4 scaleM(const Vec3& s) {
    Mat4 r; r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z; return r;
}
inline Mat4 fromQuat(const Quat& q) {
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    Mat4 r;
    r.at(0, 0) = 1 - 2 * (yy + zz); r.at(0, 1) = 2 * (xy - wz);     r.at(0, 2) = 2 * (xz + wy);
    r.at(1, 0) = 2 * (xy + wz);     r.at(1, 1) = 1 - 2 * (xx + zz); r.at(1, 2) = 2 * (yz - wx);
    r.at(2, 0) = 2 * (xz - wy);     r.at(2, 1) = 2 * (yz + wx);     r.at(2, 2) = 1 - 2 * (xx + yy);
    return r;
}
// T * R * S
inline Mat4 trs(const Vec3& t, const Quat& q, const Vec3& s) {
    Mat4 r = fromQuat(q);
    for (int i = 0; i < 3; i++) { r.m[i] *= s.x; r.m[4 + i] *= s.y; r.m[8 + i] *= s.z; }
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
}
inline Mat4 perspective(float fovy, float aspect, float n, float f) {
    float t = 1.f / std::tan(fovy * 0.5f);
    Mat4 r;
    std::memset(r.m, 0, sizeof r.m);
    r.m[0] = t / aspect; r.m[5] = t;
    r.m[10] = (f + n) / (n - f); r.m[11] = -1.f;
    r.m[14] = 2.f * f * n / (n - f);
    return r;
}
inline Mat4 ortho(float l, float r_, float b, float t, float n, float f) {
    Mat4 r;
    r.m[0] = 2.f / (r_ - l); r.m[5] = 2.f / (t - b); r.m[10] = -2.f / (f - n);
    r.m[12] = -(r_ + l) / (r_ - l); r.m[13] = -(t + b) / (t - b); r.m[14] = -(f + n) / (f - n);
    return r;
}
inline Mat4 lookAt(const Vec3& eye, const Vec3& c, const Vec3& up) {
    Vec3 f = normalize(c - eye);
    Vec3 s = normalize(cross(f, up));
    Vec3 u = cross(s, f);
    Mat4 r;
    r.at(0, 0) = s.x; r.at(0, 1) = s.y; r.at(0, 2) = s.z;
    r.at(1, 0) = u.x; r.at(1, 1) = u.y; r.at(1, 2) = u.z;
    r.at(2, 0) = -f.x; r.at(2, 1) = -f.y; r.at(2, 2) = -f.z;
    r.at(0, 3) = -dot(s, eye); r.at(1, 3) = -dot(u, eye); r.at(2, 3) = dot(f, eye);
    return r;
}
// Общая обратная матрица 4x4 (Гаусс-Жордан с выбором опорного элемента).
inline Mat4 inverse(const Mat4& a) {
    double M[4][8];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            M[r][c] = a.at(r, c);
            M[r][c + 4] = (r == c) ? 1.0 : 0.0;
        }
    for (int i = 0; i < 4; i++) {
        int p = i;
        for (int r = i + 1; r < 4; r++)
            if (std::fabs(M[r][i]) > std::fabs(M[p][i])) p = r;
        if (std::fabs(M[p][i]) < 1e-14) return Mat4();
        if (p != i) for (int c = 0; c < 8; c++) std::swap(M[i][c], M[p][c]);
        double d = 1.0 / M[i][i];
        for (int c = 0; c < 8; c++) M[i][c] *= d;
        for (int r = 0; r < 4; r++) {
            if (r == i) continue;
            double f = M[r][i];
            if (f == 0) continue;
            for (int c = 0; c < 8; c++) M[r][c] -= f * M[i][c];
        }
    }
    Mat4 o;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) o.at(r, c) = (float)M[r][c + 4];
    return o;
}
// Разложение матрицы на T, R, S (для узлов glTF, заданных matrix).
inline void decompose(const Mat4& a, Vec3& t, Quat& q, Vec3& s) {
    t = {a.m[12], a.m[13], a.m[14]};
    Vec3 c0{a.m[0], a.m[1], a.m[2]}, c1{a.m[4], a.m[5], a.m[6]}, c2{a.m[8], a.m[9], a.m[10]};
    s = {length(c0), length(c1), length(c2)};
    if (dot(cross(c0, c1), c2) < 0) s.x = -s.x;
    if (s.x != 0) c0 = c0 * (1.f / s.x);
    if (s.y != 0) c1 = c1 * (1.f / s.y);
    if (s.z != 0) c2 = c2 * (1.f / s.z);
    float m00 = c0.x, m10 = c0.y, m20 = c0.z, m01 = c1.x, m11 = c1.y, m21 = c1.z, m02 = c2.x, m12 = c2.y, m22 = c2.z;
    float tr = m00 + m11 + m22;
    if (tr > 0) {
        float S = std::sqrt(tr + 1.f) * 2.f;
        q = {(m21 - m12) / S, (m02 - m20) / S, (m10 - m01) / S, 0.25f * S};
    } else if (m00 > m11 && m00 > m22) {
        float S = std::sqrt(1.f + m00 - m11 - m22) * 2.f;
        q = {0.25f * S, (m01 + m10) / S, (m02 + m20) / S, (m21 - m12) / S};
    } else if (m11 > m22) {
        float S = std::sqrt(1.f + m11 - m00 - m22) * 2.f;
        q = {(m01 + m10) / S, 0.25f * S, (m12 + m21) / S, (m02 - m20) / S};
    } else {
        float S = std::sqrt(1.f + m22 - m00 - m11) * 2.f;
        q = {(m02 + m20) / S, (m12 + m21) / S, 0.25f * S, (m10 - m01) / S};
    }
    q = normalize(q);
}

}  // namespace eng
