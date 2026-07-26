// NetQuantizationTests.cpp — 양자화 왕복·오차·BitStream 결합 검증 (docs/mmorpg/02, M9)
#include "TestFramework.h"

#include "mye/net/Quantization.h"
#include "mye/net/BitStream.h"
#include "mye/core/Math.h"

#include <cmath>

using namespace mye;
using namespace mye::net;

MYE_TEST(QuantizeRoundtripWithinResolution) {
    // 월드 좌표 [-100,100] 를 16비트로. 오차는 스텝의 절반 이하.
    const float min = -100.0f, max = 100.0f;
    const int bits = 16;
    const float step = QuantStep(min, max, bits);

    const float samples[] = {-100.0f, -37.5f, 0.0f, 12.34f, 99.99f, 100.0f, 55.5f};
    for (float v : samples) {
        const uint32_t q = QuantizeFloat(v, min, max, bits);
        const float back = DequantizeFloat(q, min, max, bits);
        MYE_EXPECT(std::fabs(back - v) <= step);   // 오차 ≤ 1스텝
    }
}

MYE_TEST(QuantizeClampsOutOfRange) {
    const float min = 0.0f, max = 1.0f; const int bits = 8;
    MYE_EXPECT(QuantizeFloat(-5.0f, min, max, bits) == 0);
    MYE_EXPECT(QuantizeFloat(5.0f, min, max, bits) == (1u << bits) - 1u);
    MYE_EXPECT(ApproxEqual(DequantizeFloat(0, min, max, bits), 0.0f));
    MYE_EXPECT(ApproxEqual(DequantizeFloat((1u << bits) - 1u, min, max, bits), 1.0f));
}

MYE_TEST(QuantizedPositionOverBitStream) {
    // 엔티티 위치를 양자화해 BitStream에 싣고(각 축 16비트) 복원 — 스냅샷 압축 경로.
    const float min = -512.0f, max = 512.0f; const int bits = 16;
    const float step = QuantStep(min, max, bits);
    const float px = 123.4f, py = -88.8f;

    BitWriter w;
    w.WriteBits(QuantizeFloat(px, min, max, bits), bits);
    w.WriteBits(QuantizeFloat(py, min, max, bits), bits);
    const auto bytes = w.Finish();
    MYE_EXPECT(bytes.size() == 4);   // 32비트 = 4바이트(두 축 16비트)

    BitReader r(bytes);
    const float rx = DequantizeFloat(r.ReadBits(bits), min, max, bits);
    const float ry = DequantizeFloat(r.ReadBits(bits), min, max, bits);
    MYE_EXPECT(std::fabs(rx - px) <= step);
    MYE_EXPECT(std::fabs(ry - py) <= step);
    MYE_EXPECT(r.Ok());
}
