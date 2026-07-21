// mye/core/Math.h — 자체 경량 수학 (docs/01 확정: 자체 구현, 내부 교체 전략)
//
// 행렬 규약: 저장 row-major, 곱셈 row-vector (v * M) — DirectXMath·HLSL 기본과 정합.
// 핸디드니스·클립 깊이·Y방향·PPU 규약은 02 소유 — 여기서는 LH/RH 양쪽을 모두 제공(중립).
// 픽셀 스냅 유틸은 PPU를 항상 파라미터로 받는다 (PPU 상수 소유는 02).
#pragma once

#include <cstdint>
#include <cmath>

namespace mye {

// ---------------------------------------------------------------------------
// 상수·스칼라 유틸 (constexpr)
// ---------------------------------------------------------------------------
inline constexpr float kPi     = 3.14159265358979323846f;
inline constexpr float kTwoPi  = 6.28318530717958647692f;
inline constexpr float kHalfPi = 1.57079632679489661923f;

constexpr float DegToRad(float degrees) { return degrees * (kPi / 180.0f); }
constexpr float RadToDeg(float radians) { return radians * (180.0f / kPi); }

constexpr float Lerp(float a, float b, float t) { return a + (b - a) * t; }

template <class T>
constexpr T Clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

constexpr float Saturate(float v) { return Clamp(v, 0.0f, 1.0f); }

// std::abs는 C++20에서 constexpr가 아니므로 자체 제공
constexpr float Abs(float v) { return v < 0.0f ? -v : v; }

constexpr bool ApproxEqual(float a, float b, float epsilon = 1e-5f) {
    return Abs(a - b) <= epsilon;
}

// ---------------------------------------------------------------------------
// 벡터
// ---------------------------------------------------------------------------
struct Vec2 {
    float x = 0.0f, y = 0.0f;

    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }
    constexpr Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    constexpr Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
    constexpr bool operator==(const Vec2&) const = default;

    static constexpr float Dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
    float Length() const { return std::sqrt(x * x + y * y); }
    Vec2  Normalized() const;                                   // 길이 0이면 영벡터
    static constexpr Vec2 Lerp(Vec2 a, Vec2 b, float t) { return a + (b - a) * t; }
};

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    constexpr Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    constexpr Vec3& operator+=(Vec3 o) { x += o.x; y += o.y; z += o.z; return *this; }
    constexpr Vec3& operator-=(Vec3 o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    constexpr bool operator==(const Vec3&) const = default;

    static constexpr float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    static constexpr Vec3 Cross(Vec3 a, Vec3 b) {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }
    float Length() const { return std::sqrt(x * x + y * y + z * z); }
    Vec3  Normalized() const;
    static constexpr Vec3 Lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }
};

struct Vec4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;

    constexpr Vec4 operator+(Vec4 o) const { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
    constexpr Vec4 operator-(Vec4 o) const { return {x - o.x, y - o.y, z - o.z, w - o.w}; }
    constexpr Vec4 operator*(float s) const { return {x * s, y * s, z * s, w * s}; }
    constexpr bool operator==(const Vec4&) const = default;

    static constexpr float Dot(Vec4 a, Vec4 b) {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    }
};

struct Vec2i {
    int32_t x = 0, y = 0;
    constexpr Vec2i operator+(Vec2i o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2i operator-(Vec2i o) const { return {x - o.x, y - o.y}; }
    constexpr bool operator==(const Vec2i&) const = default;
};

struct Vec3i {
    int32_t x = 0, y = 0, z = 0;
    constexpr bool operator==(const Vec3i&) const = default;
};

constexpr bool ApproxEqual(Vec2 a, Vec2 b, float epsilon = 1e-5f) {
    return ApproxEqual(a.x, b.x, epsilon) && ApproxEqual(a.y, b.y, epsilon);
}
constexpr bool ApproxEqual(Vec3 a, Vec3 b, float epsilon = 1e-5f) {
    return ApproxEqual(a.x, b.x, epsilon) && ApproxEqual(a.y, b.y, epsilon) &&
           ApproxEqual(a.z, b.z, epsilon);
}
constexpr bool ApproxEqual(const Vec4& a, const Vec4& b, float epsilon = 1e-5f) {
    return ApproxEqual(a.x, b.x, epsilon) && ApproxEqual(a.y, b.y, epsilon) &&
           ApproxEqual(a.z, b.z, epsilon) && ApproxEqual(a.w, b.w, epsilon);
}

// ---------------------------------------------------------------------------
// 쿼터니언 (단위 쿼터니언, 기본값 = 항등 회전)
//
// 합성 규약: a * b = "a 먼저, b 나중" — row-vector 행렬 연쇄(v * A * B)와 같은
//            왼쪽→오른쪽 읽기 순서. Mat4::Rotation(a * b) == Rotation(a) * Rotation(b).
//            (DirectXMath XMQuaternionMultiply(a, b)와 동일한 의미)
// 회전 방향: DirectXMath 관행 — 회전축 +방향에서 원점을 바라볼 때 시계 방향이 +각.
//            예) LH에서 FromAxisAngle({0,0,1}, +90°)는 +X를 +Y로 보낸다.
// ---------------------------------------------------------------------------
struct Quat {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;

    static constexpr Quat Identity() { return {0.0f, 0.0f, 0.0f, 1.0f}; }
    // 오일러(라디안) → 쿼터니언. 적용 순서: roll(z) → pitch(x) → yaw(y)
    // (DirectXMath XMQuaternionRotationRollPitchYaw({x=pitch, y=yaw, z=roll})와 동일)
    static Quat FromEuler(Vec3 radians);
    static Quat FromAxisAngle(Vec3 axis, float radians); // axis는 정규화 불필요(내부 정규화)
    static Quat Slerp(const Quat& a, const Quat& b, float t); // 최단 경로, 결과 정규화

    // 회전 합성: this 먼저, o 나중 (위 규약 참조)
    constexpr Quat operator*(const Quat& o) const {
        return {
            o.w * x + w * o.x + (o.y * z - o.z * y),
            o.w * y + w * o.y + (o.z * x - o.x * z),
            o.w * z + w * o.z + (o.x * y - o.y * x),
            o.w * w - (o.x * x + o.y * y + o.z * z),
        };
    }
    Quat Normalized() const;                             // 길이 0이면 Identity
    // 벡터 회전 (v' = q v q⁻¹, 단위 쿼터니언 가정)
    constexpr Vec3 Rotate(Vec3 v) const {
        const Vec3 qv{x, y, z};
        const Vec3 t = Vec3::Cross(qv, v) * 2.0f;
        return v + t * w + Vec3::Cross(qv, t);
    }
    constexpr bool operator==(const Quat&) const = default;
};

// ---------------------------------------------------------------------------
// 4x4 행렬 — 저장 row-major, 곱셈 row-vector (v * M)
// m[row][col]. 이동(translation)은 마지막 행(m[3][0..2])에 위치한다.
// ---------------------------------------------------------------------------
struct Mat4 {
    float m[4][4] = {
        {1, 0, 0, 0},
        {0, 1, 0, 0},
        {0, 0, 1, 0},
        {0, 0, 0, 1},
    };

    static constexpr Mat4 Identity() { return {}; }
    static constexpr Mat4 Translation(Vec3 t) {
        Mat4 r{};
        r.m[3][0] = t.x; r.m[3][1] = t.y; r.m[3][2] = t.z;
        return r;
    }
    static constexpr Mat4 Scale(Vec3 s) {
        Mat4 r{};
        r.m[0][0] = s.x; r.m[1][1] = s.y; r.m[2][2] = s.z;
        return r;
    }
    static Mat4 Rotation(const Quat& q);                 // 단위 쿼터니언 가정
    static Mat4 TRS(Vec3 t, Quat r, Vec3 s);             // Scale → Rotation → Translation 순 적용(row-vector)

    // 투영: LH/RH 모두 제공 — 채택은 02가 결정(엔진 규약: LH, NDC 깊이 0~1)
    // 전부 DirectXMath 동명 함수와 수치 동일(row-vector, 클립 깊이 0~1).
    static Mat4 OrthoLH(float w, float h, float zn, float zf);
    static Mat4 OrthoRH(float w, float h, float zn, float zf);
    static Mat4 OrthoOffCenterLH(float left, float right, float bottom, float top, float zn, float zf);
    static Mat4 PerspectiveLH(float fovY, float aspect, float zn, float zf);
    static Mat4 PerspectiveRH(float fovY, float aspect, float zn, float zf);
    static Mat4 LookAtLH(Vec3 eye, Vec3 target, Vec3 up);

    // this * o (row-major 연쇄: v * A * B — A 먼저 적용)
    constexpr Mat4 operator*(const Mat4& o) const {
        Mat4 r{};
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) sum += m[row][k] * o.m[k][col];
                r.m[row][col] = sum;
            }
        return r;
    }
    constexpr Mat4 Transposed() const {
        Mat4 r{};
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col) r.m[col][row] = m[row][col];
        return r;
    }
    Mat4 Inverse() const;                                // 특이 행렬이면 Identity 반환 + 어서트
};

// row-vector 곱: v' = v * M
constexpr Vec4 operator*(const Vec4& v, const Mat4& m) {
    return {
        v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0] + v.w * m.m[3][0],
        v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1] + v.w * m.m[3][1],
        v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2] + v.w * m.m[3][2],
        v.x * m.m[0][3] + v.y * m.m[1][3] + v.z * m.m[2][3] + v.w * m.m[3][3],
    };
}
constexpr Vec3 TransformPoint(Vec3 p, const Mat4& m) {   // w=1 취급, 원근 나눗셈 없음
    const Vec4 r = Vec4{p.x, p.y, p.z, 1.0f} * m;
    return {r.x, r.y, r.z};
}
constexpr Vec3 TransformDirection(Vec3 d, const Mat4& m) { // w=0 취급
    const Vec4 r = Vec4{d.x, d.y, d.z, 0.0f} * m;
    return {r.x, r.y, r.z};
}

// ---------------------------------------------------------------------------
// 사각형
// ---------------------------------------------------------------------------
struct Rect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;

    constexpr bool Contains(Vec2 p) const {
        return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h;
    }
    constexpr bool Overlaps(const Rect& o) const {
        return x < o.x + o.w && o.x < x + w && y < o.y + o.h && o.y < y + h;
    }
    constexpr bool operator==(const Rect&) const = default;
};

struct RectInt {
    int32_t x = 0, y = 0, w = 0, h = 0;
    constexpr bool operator==(const RectInt&) const = default;
};

// ---------------------------------------------------------------------------
// 색 — Color: float RGBA linear / Color32: 8bit RGBA (sRGB 저장·전송용)
// ---------------------------------------------------------------------------
struct Color {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;

    static constexpr Color Black()       { return {0, 0, 0, 1}; }
    static constexpr Color White()       { return {1, 1, 1, 1}; }
    static constexpr Color Transparent() { return {0, 0, 0, 0}; }
    constexpr bool operator==(const Color&) const = default;
};

struct Color32 {
    uint8_t r = 0, g = 0, b = 0, a = 255;
    constexpr bool operator==(const Color32&) const = default;
};

// ---------------------------------------------------------------------------
// 픽셀 스냅 유틸 — PPU는 항상 파라미터 (PPU 규약·정책 소유는 02)
// ---------------------------------------------------------------------------
namespace pixel {

inline float SnapScalar(float v, float pixelsPerUnit) {
    return std::round(v * pixelsPerUnit) / pixelsPerUnit;
}
inline Vec2 Snap(Vec2 world, float pixelsPerUnit) {
    return {SnapScalar(world.x, pixelsPerUnit), SnapScalar(world.y, pixelsPerUnit)};
}
inline Rect SnapRect(const Rect& r, float pixelsPerUnit) {
    return {SnapScalar(r.x, pixelsPerUnit), SnapScalar(r.y, pixelsPerUnit),
            SnapScalar(r.w, pixelsPerUnit), SnapScalar(r.h, pixelsPerUnit)};
}
inline bool IsOnPixelGrid(Vec2 world, float pixelsPerUnit, float epsilon = 1e-5f) {
    const Vec2 s = Snap(world, pixelsPerUnit);
    return std::abs(s.x - world.x) <= epsilon && std::abs(s.y - world.y) <= epsilon;
}

} // namespace pixel
} // namespace mye
