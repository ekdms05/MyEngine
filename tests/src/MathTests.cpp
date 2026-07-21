// MathTests.cpp — 수학 라이브러리 단위 테스트 (M0-math)
// 핵심 불변식: row-vector(v * M) 곱 순서, LH/RH 투영(NDC 깊이 0~1), 쿼터니언 회전·합성,
//              역행렬, TRS 적용 순서, 픽셀 스냅.
#include "TestFramework.h"

#include "mye/core/Math.h"

using namespace mye;

namespace {

bool MatNear(const Mat4& a, const Mat4& b, float eps = 1e-5f) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if (std::abs(a.m[r][c] - b.m[r][c]) > eps) return false;
    return true;
}

// 클립 공간 곱 + 원근 나눗셈 → NDC
Vec3 ProjectNdc(Vec3 p, const Mat4& m) {
    const Vec4 clip = Vec4{p.x, p.y, p.z, 1.0f} * m;
    return {clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
}

} // namespace

// ---------------------------------------------------------------------------
// constexpr 계약 고정 (컴파일 타임 평가 가능해야 함)
// ---------------------------------------------------------------------------
static_assert(Lerp(0.0f, 10.0f, 0.25f) == 2.5f);
static_assert(Clamp(5, 0, 3) == 3 && Clamp(-1, 0, 3) == 0 && Clamp(2, 0, 3) == 2);
static_assert(Saturate(1.5f) == 1.0f && Saturate(-0.5f) == 0.0f);
static_assert(ApproxEqual(DegToRad(180.0f), kPi));
static_assert(ApproxEqual(RadToDeg(kHalfPi), 90.0f, 1e-3f));
static_assert(Vec3::Dot({1, 2, 3}, {4, 5, 6}) == 32.0f);
static_assert(Vec3::Cross({1, 0, 0}, {0, 1, 0}) == Vec3{0, 0, 1});
static_assert(TransformPoint({1, 1, 1}, Mat4::Translation({10, 0, 0})) == Vec3{11, 1, 1});
static_assert(TransformDirection({1, 1, 1}, Mat4::Translation({10, 0, 0})) == Vec3{1, 1, 1});
static_assert(TransformPoint({1, 2, 3}, Mat4::Scale({2, 3, 4})) == Vec3{2, 6, 12});
static_assert(Quat::Identity() * Quat::Identity() == Quat::Identity());
static_assert(Quat::Identity().Rotate({1, 2, 3}) == Vec3{1, 2, 3});

// ---------------------------------------------------------------------------
// 스칼라·벡터
// ---------------------------------------------------------------------------
MYE_TEST(ScalarUtils) {
    MYE_EXPECT_NEAR(Lerp(-2.0f, 2.0f, 0.5f), 0.0f, 1e-6f);
    MYE_EXPECT(Clamp(0.7f, 0.0f, 1.0f) == 0.7f);
    MYE_EXPECT(ApproxEqual(1.0f, 1.0f + 1e-6f));
    MYE_EXPECT(!ApproxEqual(1.0f, 1.01f));
    MYE_EXPECT_NEAR(DegToRad(60.0f), 1.0471976f, 1e-5f);
}

MYE_TEST(Vec2Basics) {
    constexpr Vec2 a{1.0f, 2.0f};
    constexpr Vec2 b{3.0f, -1.0f};
    MYE_EXPECT((a + b) == Vec2{4.0f, 1.0f});
    MYE_EXPECT_NEAR(Vec2::Dot(a, b), 1.0f, 1e-6f);
    MYE_EXPECT_NEAR((Vec2{3.0f, 4.0f}.Length()), 5.0f, 1e-6f);
}

MYE_TEST(VecNormalize) {
    MYE_EXPECT(ApproxEqual(Vec2{3.0f, 4.0f}.Normalized(), Vec2{0.6f, 0.8f}));
    MYE_EXPECT(ApproxEqual(Vec3{0.0f, 0.0f, 5.0f}.Normalized(), Vec3{0, 0, 1}));
    MYE_EXPECT((Vec2{}.Normalized() == Vec2{}));  // 길이 0 → 영벡터
    MYE_EXPECT((Vec3{}.Normalized() == Vec3{}));
    MYE_EXPECT_NEAR((Vec3{1, 2, 3}.Normalized().Length()), 1.0f, 1e-6f);
}

MYE_TEST(Vec3Cross) {
    constexpr Vec3 x{1, 0, 0}, y{0, 1, 0};
    MYE_EXPECT(Vec3::Cross(x, y) == Vec3{0, 0, 1});
    MYE_EXPECT(Vec3::Cross(y, x) == Vec3{0, 0, -1});
}

// ---------------------------------------------------------------------------
// 행렬 — 곱 순서·전치·역행렬
// ---------------------------------------------------------------------------
MYE_TEST(Mat4IdentityMul) {
    const Mat4 identity = Mat4::Identity();
    const Mat4 t = Mat4::Translation({1, 2, 3});
    const Mat4 r = t * identity;
    MYE_EXPECT_NEAR(r.m[3][0], 1.0f, 1e-6f);
    MYE_EXPECT_NEAR(r.m[3][1], 2.0f, 1e-6f);
    MYE_EXPECT_NEAR(r.m[3][2], 3.0f, 1e-6f);
}

MYE_TEST(RowVectorConvention) {
    // row-vector 규약: v * Translation = v + t
    const Vec3 p = TransformPoint({1, 1, 1}, Mat4::Translation({10, 0, 0}));
    MYE_EXPECT_NEAR(p.x, 11.0f, 1e-6f);
}

MYE_TEST(MatMulOrder) {
    // v * A * B는 A 먼저 적용: Scale(2) 후 Translate(+10x)
    const Mat4 s = Mat4::Scale({2, 2, 2});
    const Mat4 t = Mat4::Translation({10, 0, 0});
    MYE_EXPECT(ApproxEqual(TransformPoint({1, 1, 1}, s * t), Vec3{12, 2, 2}));
    // 반대 순서(Translate 후 Scale)는 결과가 달라야 한다
    MYE_EXPECT(ApproxEqual(TransformPoint({1, 1, 1}, t * s), Vec3{22, 2, 2}));
    // (v * A) * B == v * (A * B)
    const Vec4 v{1, 1, 1, 1};
    MYE_EXPECT(ApproxEqual((v * s) * t, v * (s * t)));
}

MYE_TEST(MatTranspose) {
    const Mat4 a = Mat4::TRS({1, 2, 3}, Quat::FromAxisAngle({0, 1, 0}, DegToRad(30.0f)), {2, 1, 1});
    const Mat4 b = Mat4::Translation({-4, 5, 6});
    MYE_EXPECT(MatNear(a.Transposed().Transposed(), a));
    MYE_EXPECT(MatNear((a * b).Transposed(), b.Transposed() * a.Transposed()));
}

MYE_TEST(MatInverse) {
    MYE_EXPECT(MatNear(Mat4::Identity().Inverse(), Mat4::Identity()));
    MYE_EXPECT(MatNear(Mat4::Translation({1, 2, 3}).Inverse(), Mat4::Translation({-1, -2, -3})));

    const Mat4 m = Mat4::TRS({1, 2, 3}, Quat::FromAxisAngle({0, 1, 0}, DegToRad(37.0f)),
                             {2.0f, 1.0f, 0.5f});
    MYE_EXPECT(MatNear(m * m.Inverse(), Mat4::Identity(), 1e-4f));
    MYE_EXPECT(MatNear(m.Inverse() * m, Mat4::Identity(), 1e-4f));
}

MYE_TEST(MatTrsOrder) {
    // TRS: Scale → Rotation → Translation 순 (row-vector)
    const Mat4 m = Mat4::TRS({5, 0, 0}, Quat::FromAxisAngle({0, 0, 1}, kHalfPi), {2, 2, 2});
    // (1,0,0) → 스케일 (2,0,0) → Z축 90° 회전 (0,2,0) → 이동 (5,2,0)
    MYE_EXPECT(ApproxEqual(TransformPoint({1, 0, 0}, m), Vec3{5, 2, 0}, 1e-5f));
}

// ---------------------------------------------------------------------------
// 쿼터니언
// ---------------------------------------------------------------------------
MYE_TEST(QuatAxisAngle) {
    // DirectX 회전 방향(LH): Z축 +90° → +X를 +Y로
    const Quat qz = Quat::FromAxisAngle({0, 0, 1}, kHalfPi);
    MYE_EXPECT(ApproxEqual(qz.Rotate({1, 0, 0}), Vec3{0, 1, 0}));
    // X축 +90° → +Y를 +Z로
    const Quat qx = Quat::FromAxisAngle({1, 0, 0}, kHalfPi);
    MYE_EXPECT(ApproxEqual(qx.Rotate({0, 1, 0}), Vec3{0, 0, 1}));
    // Y축 +90° → +Z를 +X로
    const Quat qy = Quat::FromAxisAngle({0, 1, 0}, kHalfPi);
    MYE_EXPECT(ApproxEqual(qy.Rotate({0, 0, 1}), Vec3{1, 0, 0}));
    // 축은 내부 정규화: 길이 달라도 동일 회전
    const Quat qz2 = Quat::FromAxisAngle({0, 0, 10}, kHalfPi);
    MYE_EXPECT(ApproxEqual(qz2.Rotate({1, 0, 0}), Vec3{0, 1, 0}));
}

MYE_TEST(QuatMatrixConsistency) {
    // Quat::Rotate와 Mat4::Rotation은 같은 회전이어야 한다
    const Quat q = Quat::FromEuler({0.3f, 0.7f, -0.2f});
    const Vec3 v{1, 2, 3};
    MYE_EXPECT(ApproxEqual(q.Rotate(v), TransformDirection(v, Mat4::Rotation(q)), 1e-5f));
    // 회전 행렬은 직교: R * R^T = I
    const Mat4 r = Mat4::Rotation(q);
    MYE_EXPECT(MatNear(r * r.Transposed(), Mat4::Identity(), 1e-5f));
}

MYE_TEST(QuatComposition) {
    // a * b = a 먼저, b 나중 (row-vector 연쇄와 동일한 읽기 순서)
    const Quat a = Quat::FromAxisAngle({0, 0, 1}, kHalfPi);  // +X → +Y
    const Quat b = Quat::FromAxisAngle({1, 0, 0}, kHalfPi);  // +Y → +Z
    MYE_EXPECT(ApproxEqual((a * b).Rotate({1, 0, 0}), Vec3{0, 0, 1}));
    MYE_EXPECT(ApproxEqual((a * b).Rotate({1, 0, 0}), b.Rotate(a.Rotate({1, 0, 0}))));
    // 행렬 합성과 정합: Rotation(a * b) == Rotation(a) * Rotation(b)
    MYE_EXPECT(MatNear(Mat4::Rotation(a * b), Mat4::Rotation(a) * Mat4::Rotation(b), 1e-5f));
    // 합성 결과도 단위 쿼터니언
    const Quat ab = (a * b).Normalized();
    MYE_EXPECT_NEAR(ab.x * ab.x + ab.y * ab.y + ab.z * ab.z + ab.w * ab.w, 1.0f, 1e-5f);
}

MYE_TEST(QuatFromEuler) {
    // 단일 축 오일러는 축각과 동일
    const float th = DegToRad(50.0f);
    MYE_EXPECT(ApproxEqual(Quat::FromEuler({0, th, 0}).Rotate({0, 0, 1}),
                           Quat::FromAxisAngle({0, 1, 0}, th).Rotate({0, 0, 1})));
    MYE_EXPECT(ApproxEqual(Quat::FromEuler({th, 0, 0}).Rotate({0, 1, 0}),
                           Quat::FromAxisAngle({1, 0, 0}, th).Rotate({0, 1, 0})));
    // 합성 순서: roll(z) → pitch(x) → yaw(y)
    const Vec3 e{0.4f, -0.9f, 1.3f};
    const Quat expected = Quat::FromAxisAngle({0, 0, 1}, e.z) *
                          Quat::FromAxisAngle({1, 0, 0}, e.x) *
                          Quat::FromAxisAngle({0, 1, 0}, e.y);
    MYE_EXPECT(ApproxEqual(Quat::FromEuler(e).Rotate({1, 2, 3}), expected.Rotate({1, 2, 3}), 1e-5f));
}

MYE_TEST(QuatSlerp) {
    const Quat a = Quat::Identity();
    const Quat b = Quat::FromAxisAngle({0, 0, 1}, kHalfPi);
    // 끝점
    MYE_EXPECT(ApproxEqual(Quat::Slerp(a, b, 0.0f).Rotate({1, 0, 0}), Vec3{1, 0, 0}));
    MYE_EXPECT(ApproxEqual(Quat::Slerp(a, b, 1.0f).Rotate({1, 0, 0}), Vec3{0, 1, 0}));
    // 중간점 = Z축 45°
    const Vec3 mid = Quat::Slerp(a, b, 0.5f).Rotate({1, 0, 0});
    MYE_EXPECT(ApproxEqual(mid, Vec3{0.70710678f, 0.70710678f, 0.0f}, 1e-5f));
    // 최단 경로: identity → 270°는 -90° 방향으로 돈다 (중간점 = -45°)
    const Quat c = Quat::FromAxisAngle({0, 0, 1}, DegToRad(270.0f));
    const Vec3 midShort = Quat::Slerp(a, c, 0.5f).Rotate({1, 0, 0});
    MYE_EXPECT(ApproxEqual(midShort, Vec3{0.70710678f, -0.70710678f, 0.0f}, 1e-5f));
}

MYE_TEST(QuatNormalized) {
    const Quat q = Quat{1, 2, 3, 4}.Normalized();
    MYE_EXPECT_NEAR(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w, 1.0f, 1e-6f);
    MYE_EXPECT((Quat{0, 0, 0, 0}.Normalized() == Quat::Identity()));  // 길이 0 → 항등
}

// ---------------------------------------------------------------------------
// 투영 — LH/RH 모두, NDC 깊이 0~1 (DX 규약)
// ---------------------------------------------------------------------------
MYE_TEST(PerspectiveLH) {
    const float zn = 0.1f, zf = 100.0f, fovY = DegToRad(60.0f), aspect = 16.0f / 9.0f;
    const Mat4 p = Mat4::PerspectiveLH(fovY, aspect, zn, zf);
    // 깊이: near → 0, far → 1 (시선 +Z)
    MYE_EXPECT_NEAR(ProjectNdc({0, 0, zn}, p).z, 0.0f, 1e-5f);
    MYE_EXPECT_NEAR(ProjectNdc({0, 0, zf}, p).z, 1.0f, 1e-4f);
    // 클립 w = 뷰 z (LH)
    MYE_EXPECT_NEAR(((Vec4{0, 0, 7, 1} * p).w), 7.0f, 1e-5f);
    // 프러스텀 상단 모서리 → NDC y=+1, 우측 모서리 → NDC x=+1
    const float d = 10.0f;
    const float halfTan = std::tan(fovY * 0.5f);
    MYE_EXPECT_NEAR(ProjectNdc({0, d * halfTan, d}, p).y, 1.0f, 1e-4f);
    MYE_EXPECT_NEAR(ProjectNdc({d * halfTan * aspect, 0, d}, p).x, 1.0f, 1e-4f);
}

MYE_TEST(PerspectiveRH) {
    const float zn = 0.1f, zf = 100.0f, fovY = DegToRad(60.0f), aspect = 16.0f / 9.0f;
    const Mat4 p = Mat4::PerspectiveRH(fovY, aspect, zn, zf);
    // RH는 시선 -Z: near(-zn) → 0, far(-zf) → 1
    MYE_EXPECT_NEAR(ProjectNdc({0, 0, -zn}, p).z, 0.0f, 1e-5f);
    MYE_EXPECT_NEAR(ProjectNdc({0, 0, -zf}, p).z, 1.0f, 1e-4f);
    // 클립 w = -뷰 z
    MYE_EXPECT_NEAR(((Vec4{0, 0, -7, 1} * p).w), 7.0f, 1e-5f);
    const float d = 10.0f;
    const float halfTan = std::tan(fovY * 0.5f);
    MYE_EXPECT_NEAR(ProjectNdc({0, d * halfTan, -d}, p).y, 1.0f, 1e-4f);
}

MYE_TEST(OrthoLHRH) {
    const Mat4 lh = Mat4::OrthoLH(8.0f, 6.0f, 0.0f, 10.0f);
    MYE_EXPECT(ApproxEqual(ProjectNdc({4, 3, 0}, lh), Vec3{1, 1, 0}, 1e-5f));
    MYE_EXPECT(ApproxEqual(ProjectNdc({-4, -3, 5}, lh), Vec3{-1, -1, 0.5f}, 1e-5f));
    MYE_EXPECT_NEAR(ProjectNdc({0, 0, 10}, lh).z, 1.0f, 1e-5f);

    const Mat4 rh = Mat4::OrthoRH(8.0f, 6.0f, 0.0f, 10.0f);
    MYE_EXPECT_NEAR(ProjectNdc({0, 0, 0}, rh).z, 0.0f, 1e-5f);
    MYE_EXPECT_NEAR(ProjectNdc({0, 0, -10}, rh).z, 1.0f, 1e-5f);  // RH: far는 -Z 방향
    MYE_EXPECT_NEAR(ProjectNdc({4, 0, -5}, rh).x, 1.0f, 1e-5f);   // XY 매핑은 LH와 동일
}

MYE_TEST(OrthoOffCenterLH) {
    // 스크린 좌표식 매핑(02 규약: 좌상단 원점, +Y 아래): top=0, bottom=540
    const Mat4 p = Mat4::OrthoOffCenterLH(0.0f, 960.0f, 540.0f, 0.0f, 0.0f, 1.0f);
    MYE_EXPECT(ApproxEqual(ProjectNdc({0, 0, 0}, p), Vec3{-1, 1, 0}, 1e-5f));      // 좌상단
    MYE_EXPECT(ApproxEqual(ProjectNdc({960, 540, 0}, p), Vec3{1, -1, 0}, 1e-5f));  // 우하단
    MYE_EXPECT(ApproxEqual(ProjectNdc({480, 270, 1}, p), Vec3{0, 0, 1}, 1e-5f));   // 중심·far
}

MYE_TEST(LookAtLH) {
    // 원점에서 +Z를 보면 항등
    const Mat4 identity = Mat4::LookAtLH({0, 0, 0}, {0, 0, 1}, {0, 1, 0});
    MYE_EXPECT(MatNear(identity, Mat4::Identity(), 1e-6f));
    // 뒤(-Z)로 물러난 카메라: 원점은 뷰 공간 +Z(전방) 5
    const Mat4 v = Mat4::LookAtLH({0, 0, -5}, {0, 0, 0}, {0, 1, 0});
    MYE_EXPECT(ApproxEqual(TransformPoint({0, 0, 0}, v), Vec3{0, 0, 5}, 1e-5f));
    MYE_EXPECT(ApproxEqual(TransformPoint({1, 2, 0}, v), Vec3{1, 2, 5}, 1e-5f));
    // 동쪽(+X)에서 원점을 보는 카메라
    const Mat4 side = Mat4::LookAtLH({5, 0, 0}, {0, 0, 0}, {0, 1, 0});
    MYE_EXPECT(ApproxEqual(TransformPoint({0, 0, 0}, side), Vec3{0, 0, 5}, 1e-5f));
    MYE_EXPECT(ApproxEqual(TransformPoint({0, 0, 1}, side), Vec3{1, 0, 5}, 1e-5f));
}

// ---------------------------------------------------------------------------
// 픽셀 스냅·사각형·색
// ---------------------------------------------------------------------------
MYE_TEST(PixelSnap) {
    // PPU 48: 1/48 그리드로 스냅
    const float snapped = pixel::SnapScalar(0.011f, 48.0f);
    MYE_EXPECT_NEAR(snapped, 1.0f / 48.0f, 1e-6f);
    MYE_EXPECT(pixel::IsOnPixelGrid({1.0f / 48.0f, 2.0f / 48.0f}, 48.0f));
    MYE_EXPECT(!pixel::IsOnPixelGrid({0.011f, 0.0f}, 48.0f));
    // 음수도 대칭으로 스냅 (round: 절반은 0에서 먼 쪽)
    MYE_EXPECT_NEAR(pixel::SnapScalar(-0.011f, 48.0f), -1.0f / 48.0f, 1e-6f);
    // 그리드 위 값은 불변
    MYE_EXPECT_NEAR(pixel::SnapScalar(5.0f / 48.0f, 48.0f), 5.0f / 48.0f, 1e-6f);
    // Snap/SnapRect 성분별 동작
    const Rect r = pixel::SnapRect({0.011f, -0.011f, 1.0f, 0.49f / 48.0f}, 48.0f);
    MYE_EXPECT_NEAR(r.x, 1.0f / 48.0f, 1e-6f);
    MYE_EXPECT_NEAR(r.y, -1.0f / 48.0f, 1e-6f);
    MYE_EXPECT_NEAR(r.w, 1.0f, 1e-6f);
    MYE_EXPECT_NEAR(r.h, 0.0f, 1e-6f);
}

MYE_TEST(RectContainsOverlaps) {
    constexpr Rect r{0, 0, 10, 10};
    MYE_EXPECT(r.Contains({5, 5}));
    MYE_EXPECT(!r.Contains({10, 10}));   // 반개구간 [x, x+w)
    MYE_EXPECT(r.Overlaps({9, 9, 5, 5}));
    MYE_EXPECT(!r.Overlaps({10, 0, 5, 5}));
}

MYE_TEST(ColorDefaults) {
    constexpr Color c{};
    MYE_EXPECT(c == Color::Black());              // 기본값 = 불투명 검정
    MYE_EXPECT(Color::White() == Color{1, 1, 1, 1});
    MYE_EXPECT(Color::Transparent().a == 0.0f);
    constexpr Color32 c32{};
    MYE_EXPECT(c32.a == 255);
}
