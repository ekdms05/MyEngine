// Math.cpp — 자체 경량 수학 구현 (docs/01 수학 섹션).
// 규약: row-major 저장 + row-vector(v * M) 곱. NDC 깊이 0~1(DX 관행).
// 투영·회전은 DirectXMath 동명 함수와 수치 동일(LH/RH 모두 제공, 채택은 docs/02).
#include "mye/core/Math.h"

#include "mye/core/Assert.h"

namespace mye {

Vec2 Vec2::Normalized() const {
    const float len = Length();
    return len > 0.0f ? (*this / len) : Vec2{};
}

Vec3 Vec3::Normalized() const {
    const float len = Length();
    return len > 0.0f ? (*this / len) : Vec3{};
}

// ---------------------------------------------------------------------------
// Quat
// ---------------------------------------------------------------------------

Quat Quat::FromEuler(Vec3 radians) {
    // roll(z) → pitch(x) → yaw(y) 순 적용 — XMQuaternionRotationRollPitchYaw와 동일.
    // operator*는 "왼쪽 먼저" 규약이므로 Rz * Rx * Ry.
    const Quat rz = FromAxisAngle({0.0f, 0.0f, 1.0f}, radians.z);
    const Quat rx = FromAxisAngle({1.0f, 0.0f, 0.0f}, radians.x);
    const Quat ry = FromAxisAngle({0.0f, 1.0f, 0.0f}, radians.y);
    return rz * rx * ry;
}

Quat Quat::FromAxisAngle(Vec3 axis, float radians) {
    const float len = axis.Length();
    if (len <= 0.0f) {
        MYE_ASSERT(false, "Quat::FromAxisAngle: zero-length axis");
        return Identity();
    }
    const float half = radians * 0.5f;
    const float s = std::sin(half) / len;    // 축 정규화 포함
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(half)};
}

Quat Quat::Slerp(const Quat& a, const Quat& b, float t) {
    // 최단 경로: 내적이 음수면 한쪽을 부호 반전 (q와 -q는 같은 회전)
    float cosTheta = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    Quat end = b;
    if (cosTheta < 0.0f) {
        cosTheta = -cosTheta;
        end = {-b.x, -b.y, -b.z, -b.w};
    }

    float s0, s1;
    if (cosTheta > 0.9995f) {
        // 각이 매우 작으면 nlerp로 대체(수치 안정)
        s0 = 1.0f - t;
        s1 = t;
    } else {
        const float theta = std::acos(Clamp(cosTheta, -1.0f, 1.0f));
        const float invSin = 1.0f / std::sin(theta);
        s0 = std::sin((1.0f - t) * theta) * invSin;
        s1 = std::sin(t * theta) * invSin;
    }
    return Quat{
        a.x * s0 + end.x * s1,
        a.y * s0 + end.y * s1,
        a.z * s0 + end.z * s1,
        a.w * s0 + end.w * s1,
    }.Normalized();
}

Quat Quat::Normalized() const {
    const float len = std::sqrt(x * x + y * y + z * z + w * w);
    return len > 0.0f ? Quat{x / len, y / len, z / len, w / len} : Identity();
}

// ---------------------------------------------------------------------------
// Mat4
// ---------------------------------------------------------------------------

Mat4 Mat4::Rotation(const Quat& q) {
    // 단위 쿼터니언 가정. row-vector 규약(열-벡터 표준 회전 행렬의 전치).
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

    Mat4 r{};
    r.m[0][0] = 1.0f - 2.0f * (yy + zz);
    r.m[0][1] = 2.0f * (xy + wz);
    r.m[0][2] = 2.0f * (xz - wy);
    r.m[1][0] = 2.0f * (xy - wz);
    r.m[1][1] = 1.0f - 2.0f * (xx + zz);
    r.m[1][2] = 2.0f * (yz + wx);
    r.m[2][0] = 2.0f * (xz + wy);
    r.m[2][1] = 2.0f * (yz - wx);
    r.m[2][2] = 1.0f - 2.0f * (xx + yy);
    return r;
}

Mat4 Mat4::TRS(Vec3 t, Quat r, Vec3 s) {
    // row-vector: v * S * R * T (Scale 먼저 적용)
    Mat4 m = Rotation(r);
    for (int col = 0; col < 3; ++col) {
        m.m[0][col] *= s.x;
        m.m[1][col] *= s.y;
        m.m[2][col] *= s.z;
    }
    m.m[3][0] = t.x;
    m.m[3][1] = t.y;
    m.m[3][2] = t.z;
    return m;
}

Mat4 Mat4::OrthoLH(float w, float h, float zn, float zf) {
    MYE_ASSERT(w > 0.0f && h > 0.0f && zn != zf, "Mat4::OrthoLH: invalid params");
    const float range = 1.0f / (zf - zn);
    Mat4 r{};
    r.m[0][0] = 2.0f / w;
    r.m[1][1] = 2.0f / h;
    r.m[2][2] = range;
    r.m[3][2] = -range * zn;
    return r;
}

Mat4 Mat4::OrthoRH(float w, float h, float zn, float zf) {
    MYE_ASSERT(w > 0.0f && h > 0.0f && zn != zf, "Mat4::OrthoRH: invalid params");
    const float range = 1.0f / (zn - zf);
    Mat4 r{};
    r.m[0][0] = 2.0f / w;
    r.m[1][1] = 2.0f / h;
    r.m[2][2] = range;
    r.m[3][2] = range * zn;
    return r;
}

Mat4 Mat4::OrthoOffCenterLH(float left, float right, float bottom, float top, float zn, float zf) {
    MYE_ASSERT(left != right && bottom != top && zn != zf,
               "Mat4::OrthoOffCenterLH: invalid params");
    const float rw = 1.0f / (right - left);
    const float rh = 1.0f / (top - bottom);
    const float range = 1.0f / (zf - zn);
    Mat4 r{};
    r.m[0][0] = 2.0f * rw;
    r.m[1][1] = 2.0f * rh;
    r.m[2][2] = range;
    r.m[3][0] = -(left + right) * rw;
    r.m[3][1] = -(top + bottom) * rh;
    r.m[3][2] = -range * zn;
    return r;
}

Mat4 Mat4::PerspectiveLH(float fovY, float aspect, float zn, float zf) {
    MYE_ASSERT(fovY > 0.0f && fovY < kPi && aspect > 0.0f && zn > 0.0f && zn != zf,
               "Mat4::PerspectiveLH: invalid params");
    const float h = 1.0f / std::tan(fovY * 0.5f);
    const float w = h / aspect;
    const float range = zf / (zf - zn);
    Mat4 r{};
    r.m[0][0] = w;
    r.m[1][1] = h;
    r.m[2][2] = range;
    r.m[2][3] = 1.0f;
    r.m[3][2] = -range * zn;
    r.m[3][3] = 0.0f;
    return r;
}

Mat4 Mat4::PerspectiveRH(float fovY, float aspect, float zn, float zf) {
    MYE_ASSERT(fovY > 0.0f && fovY < kPi && aspect > 0.0f && zn > 0.0f && zn != zf,
               "Mat4::PerspectiveRH: invalid params");
    const float h = 1.0f / std::tan(fovY * 0.5f);
    const float w = h / aspect;
    const float range = zf / (zn - zf);
    Mat4 r{};
    r.m[0][0] = w;
    r.m[1][1] = h;
    r.m[2][2] = range;
    r.m[2][3] = -1.0f;
    r.m[3][2] = range * zn;
    r.m[3][3] = 0.0f;
    return r;
}

Mat4 Mat4::LookAtLH(Vec3 eye, Vec3 target, Vec3 up) {
    const Vec3 zaxis = (target - eye).Normalized();
    const Vec3 xaxis = Vec3::Cross(up, zaxis).Normalized();
    const Vec3 yaxis = Vec3::Cross(zaxis, xaxis);
    MYE_ASSERT(xaxis.Length() > 0.0f, "Mat4::LookAtLH: eye==target or up parallel to view");

    Mat4 r{};
    r.m[0][0] = xaxis.x; r.m[0][1] = yaxis.x; r.m[0][2] = zaxis.x;
    r.m[1][0] = xaxis.y; r.m[1][1] = yaxis.y; r.m[1][2] = zaxis.y;
    r.m[2][0] = xaxis.z; r.m[2][1] = yaxis.z; r.m[2][2] = zaxis.z;
    r.m[3][0] = -Vec3::Dot(xaxis, eye);
    r.m[3][1] = -Vec3::Dot(yaxis, eye);
    r.m[3][2] = -Vec3::Dot(zaxis, eye);
    return r;
}

Mat4 Mat4::Inverse() const {
    // 2x2 블록 라플라스 전개(코팩터) — 일반 4x4 역행렬.
    const float a00 = m[0][0], a01 = m[0][1], a02 = m[0][2], a03 = m[0][3];
    const float a10 = m[1][0], a11 = m[1][1], a12 = m[1][2], a13 = m[1][3];
    const float a20 = m[2][0], a21 = m[2][1], a22 = m[2][2], a23 = m[2][3];
    const float a30 = m[3][0], a31 = m[3][1], a32 = m[3][2], a33 = m[3][3];

    const float s0 = a00 * a11 - a01 * a10;
    const float s1 = a00 * a12 - a02 * a10;
    const float s2 = a00 * a13 - a03 * a10;
    const float s3 = a01 * a12 - a02 * a11;
    const float s4 = a01 * a13 - a03 * a11;
    const float s5 = a02 * a13 - a03 * a12;

    const float c5 = a22 * a33 - a23 * a32;
    const float c4 = a21 * a33 - a23 * a31;
    const float c3 = a21 * a32 - a22 * a31;
    const float c2 = a20 * a33 - a23 * a30;
    const float c1 = a20 * a32 - a22 * a30;
    const float c0 = a20 * a31 - a21 * a30;

    const float det = s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;
    if (!(std::abs(det) > 0.0f)) {           // det==0 또는 NaN
        MYE_ASSERT(false, "Mat4::Inverse: singular matrix");
        return Identity();
    }
    const float inv = 1.0f / det;

    Mat4 r{};
    r.m[0][0] = ( a11 * c5 - a12 * c4 + a13 * c3) * inv;
    r.m[0][1] = (-a01 * c5 + a02 * c4 - a03 * c3) * inv;
    r.m[0][2] = ( a31 * s5 - a32 * s4 + a33 * s3) * inv;
    r.m[0][3] = (-a21 * s5 + a22 * s4 - a23 * s3) * inv;
    r.m[1][0] = (-a10 * c5 + a12 * c2 - a13 * c1) * inv;
    r.m[1][1] = ( a00 * c5 - a02 * c2 + a03 * c1) * inv;
    r.m[1][2] = (-a30 * s5 + a32 * s2 - a33 * s1) * inv;
    r.m[1][3] = ( a20 * s5 - a22 * s2 + a23 * s1) * inv;
    r.m[2][0] = ( a10 * c4 - a11 * c2 + a13 * c0) * inv;
    r.m[2][1] = (-a00 * c4 + a01 * c2 - a03 * c0) * inv;
    r.m[2][2] = ( a30 * s4 - a31 * s2 + a33 * s0) * inv;
    r.m[2][3] = (-a20 * s4 + a21 * s2 - a23 * s0) * inv;
    r.m[3][0] = (-a10 * c3 + a11 * c1 - a12 * c0) * inv;
    r.m[3][1] = ( a00 * c3 - a01 * c1 + a02 * c0) * inv;
    r.m[3][2] = (-a30 * s3 + a31 * s1 - a32 * s0) * inv;
    r.m[3][3] = ( a20 * s3 - a21 * s1 + a22 * s0) * inv;
    return r;
}

} // namespace mye
