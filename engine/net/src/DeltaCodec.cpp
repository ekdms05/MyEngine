// mye/net/DeltaCodec.cpp — 스냅샷 필드 델타 구현 (DeltaCodec.h 참조)
#include "mye/net/DeltaCodec.h"

namespace mye::net {

void WriteDelta(BitWriter& w, const uint32_t* baseline, const uint32_t* current,
                int fieldCount, const int* fieldBits) {
    if (fieldCount <= 0) return;
    // 1) 변경 비트마스크.
    for (int i = 0; i < fieldCount; ++i)
        w.WriteBool(baseline[i] != current[i]);
    // 2) 변경 필드 값.
    for (int i = 0; i < fieldCount; ++i)
        if (baseline[i] != current[i])
            w.WriteBits(current[i], fieldBits[i]);
}

bool ReadDelta(BitReader& r, const uint32_t* baseline, uint32_t* out,
               int fieldCount, const int* fieldBits) {
    if (fieldCount <= 0) return true;
    // 변경 비트마스크는 스트림 순서상 값보다 먼저 나오므로, 먼저 전부 읽어 보관.
    // (고정 fieldCount 이므로 스택 배열 대신 비트 재조회 없이 두 패스로 처리한다.)
    // 1패스: 변경 여부 수집.
    bool changed[64];
    if (fieldCount > 64) fieldCount = 64;   // 안전 상한
    for (int i = 0; i < fieldCount; ++i) changed[i] = r.ReadBool();
    // 2패스: 값 복원.
    for (int i = 0; i < fieldCount; ++i)
        out[i] = changed[i] ? r.ReadBits(fieldBits[i]) : baseline[i];
    return r.Ok();
}

} // namespace mye::net
