#pragma once

#include <cmath>

namespace m3 {

constexpr float kPi = 3.14159265358979323846f;

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a) {
    const float l = length(a);
    return l > 1e-8f ? a * (1.0f / l) : Vec3{};
}

// Column-major 4x4 matrix, matching GLSL's mat4 layout.
struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

inline Mat4 identity() { return Mat4{}; }

inline Mat4 mul(const Mat4 &a, const Mat4 &b) {
    Mat4 r;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a.m[k * 4 + row] * b.m[col * 4 + k];
            r.m[col * 4 + row] = s;
        }
    }
    return r;
}

inline Mat4 translation(float x, float y, float z) {
    Mat4 r;
    r.m[12] = x;
    r.m[13] = y;
    r.m[14] = z;
    return r;
}

inline Mat4 scaling(float s) {
    Mat4 r;
    r.m[0] = r.m[5] = r.m[10] = s;
    return r;
}

inline Mat4 rotationX(float a) {
    const float c = std::cos(a), s = std::sin(a);
    Mat4 r;
    r.m[5] = c;  r.m[6] = -s;
    r.m[9] = s;  r.m[10] = c;
    return r;
}

inline Mat4 rotationY(float a) {
    const float c = std::cos(a), s = std::sin(a);
    Mat4 r;
    r.m[0] = c;   r.m[2] = -s;
    r.m[8] = s;   r.m[10] = c;
    return r;
}

inline Mat4 perspective(float fovY, float aspect, float znear, float zfar) {
    const float f = 1.0f / std::tan(fovY * 0.5f);
    Mat4 r;
    for (float &v : r.m) v = 0.0f;
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zfar + znear) / (znear - zfar);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zfar * znear) / (znear - zfar);
    return r;
}

inline Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up) {
    const Vec3 f = normalize(center - eye);
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);
    Mat4 r;
    r.m[0] = s.x;  r.m[4] = s.y;  r.m[8] = s.z;
    r.m[1] = u.x;  r.m[5] = u.y;  r.m[9] = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -dot(s, eye);
    r.m[13] = -dot(u, eye);
    r.m[14] = dot(f, eye);
    return r;
}

inline Vec3 transformPoint(const Mat4 &m, Vec3 p) {
    const float x = m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12];
    const float y = m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13];
    const float z = m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14];
    const float w = m.m[3] * p.x + m.m[7] * p.y + m.m[11] * p.z + m.m[15];
    if (std::fabs(w) > 1e-8f && std::fabs(w - 1.0f) > 1e-8f) {
        return {x / w, y / w, z / w};
    }
    return {x, y, z};
}

inline Vec3 transformDir(const Mat4 &m, Vec3 d) {
    return {m.m[0] * d.x + m.m[4] * d.y + m.m[8] * d.z,
            m.m[1] * d.x + m.m[5] * d.y + m.m[9] * d.z,
            m.m[2] * d.x + m.m[6] * d.y + m.m[10] * d.z};
}

inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline float smoothstep01(float t) {
    t = clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace m3
