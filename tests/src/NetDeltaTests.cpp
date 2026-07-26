// NetDeltaTests.cpp — 스냅샷 필드 델타 압축 검증 (docs/mmorpg/02, M9)
#include "TestFramework.h"

#include "mye/net/DeltaCodec.h"
#include "mye/net/BitStream.h"

#include <cstdint>

using namespace mye;
using namespace mye::net;

MYE_TEST(DeltaEncodesOnlyChangedFields) {
    const int bits[4] = {16, 16, 8, 8};
    const uint32_t baseline[4] = {1000, 2000, 50, 7};
    const uint32_t current[4]  = {1000, 2050, 50, 9};   // 필드 1,3 만 변경

    BitWriter w;
    WriteDelta(w, baseline, current, 4, bits);
    const auto bytes = w.Finish();

    uint32_t out[4] = {};
    BitReader r(bytes);
    MYE_EXPECT(ReadDelta(r, baseline, out, 4, bits));
    MYE_EXPECT(out[0] == 1000 && out[1] == 2050 && out[2] == 50 && out[3] == 9);

    // 대역폭: 변경비트 4 + 변경값(16+8=24) = 28비트 = 4바이트. 전체(4비트+48비트=52비트=7바이트)보다 작다.
    MYE_EXPECT(bytes.size() == 4);
}

MYE_TEST(DeltaNoChangeIsTiny) {
    const int bits[3] = {32, 32, 32};
    const uint32_t base[3] = {111, 222, 333};
    const uint32_t cur[3]  = {111, 222, 333};   // 무변경

    BitWriter w;
    WriteDelta(w, base, cur, 3, bits);
    const auto bytes = w.Finish();
    MYE_EXPECT(bytes.size() == 1);   // 변경비트 3개만 → 1바이트

    uint32_t out[3] = {};
    BitReader r(bytes);
    MYE_EXPECT(ReadDelta(r, base, out, 3, bits));
    MYE_EXPECT(out[0] == 111 && out[1] == 222 && out[2] == 333);
}

MYE_TEST(DeltaAllChanged) {
    const int bits[2] = {16, 16};
    const uint32_t base[2] = {0, 0};
    const uint32_t cur[2]  = {40000, 12345};

    BitWriter w;
    WriteDelta(w, base, cur, 2, bits);
    uint32_t out[2] = {};
    BitReader r(w.Finish());
    MYE_EXPECT(ReadDelta(r, base, out, 2, bits));
    MYE_EXPECT(out[0] == 40000 && out[1] == 12345);
}
