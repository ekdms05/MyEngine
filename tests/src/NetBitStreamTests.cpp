// NetBitStreamTests.cpp — 비트 직렬화 왕복 검증 (docs/mmorpg/02, M9)
#include "TestFramework.h"

#include "mye/net/BitStream.h"

#include <cstdint>
#include <vector>

using namespace mye;
using namespace mye::net;

MYE_TEST(BitStreamBitsRoundtrip) {
    BitWriter w;
    w.WriteBits(5, 3);        // 101
    w.WriteBool(true);
    w.WriteBits(0x3FF, 10);   // 1023
    w.WriteBits(0xDEADBEEF, 32);
    w.WriteBool(false);
    const std::vector<uint8_t> bytes = w.Finish();

    BitReader r(bytes);
    MYE_EXPECT(r.ReadBits(3) == 5);
    MYE_EXPECT(r.ReadBool() == true);
    MYE_EXPECT(r.ReadBits(10) == 0x3FF);
    MYE_EXPECT(r.ReadBits(32) == 0xDEADBEEF);
    MYE_EXPECT(r.ReadBool() == false);
    MYE_EXPECT(r.Ok());
}

MYE_TEST(BitStreamVarUint) {
    const uint64_t values[] = {0, 1, 127, 128, 300, 16384, 0xFFFFFFFFull, 0x1234567890ABull};
    BitWriter w;
    for (uint64_t v : values) w.WriteVarUint(v);
    const std::vector<uint8_t> bytes = w.Finish();

    BitReader r(bytes);
    for (uint64_t v : values) MYE_EXPECT(r.ReadVarUint() == v);
    MYE_EXPECT(r.Ok());

    // 작은 값은 1바이트로 인코드(공간 효율).
    BitWriter w2; w2.WriteVarUint(100);
    MYE_EXPECT(w2.Finish().size() == 1);
    BitWriter w3; w3.WriteVarUint(200);   // 128~16383 → 2바이트
    MYE_EXPECT(w3.Finish().size() == 2);
}

MYE_TEST(BitStreamBytesAndMixed) {
    const uint8_t payload[] = {0x11, 0x22, 0x33, 0x44};
    BitWriter w;
    w.WriteBits(7, 4);
    w.WriteBytes(payload, sizeof(payload));
    w.WriteVarUint(99999);
    const auto bytes = w.Finish();

    BitReader r(bytes);
    MYE_EXPECT(r.ReadBits(4) == 7);
    uint8_t out[4] = {};
    r.ReadBytes(out, 4);
    MYE_EXPECT(out[0] == 0x11 && out[1] == 0x22 && out[2] == 0x33 && out[3] == 0x44);
    MYE_EXPECT(r.ReadVarUint() == 99999);
    MYE_EXPECT(r.Ok());
}

MYE_TEST(BitStreamReadPastEndFails) {
    BitWriter w; w.WriteBits(1, 8);
    BitReader r(w.Finish());
    (void)r.ReadBits(8);       // ok
    (void)r.ReadBits(8);       // 데이터 초과
    MYE_EXPECT(!r.Ok());
}
