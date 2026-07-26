// mye/net/DeltaCodec.h — 스냅샷 필드 델타 압축 (docs/mmorpg/02, M9)
//
// 베이스라인 대비 변경된 필드만 전송한다: 필드별 변경비트(1) + 변경 필드 값만 기록. 미변경 필드는
// 수신측이 베이스라인에서 복사. 서버 대역폭 절감의 핵심(복제 델타). 순수 로직·결정론.
#pragma once

#include "mye/net/BitStream.h"

#include <cstdint>

namespace mye::net {

// baseline 대비 current 를 델타 인코딩. fieldBits[i] = 필드 i 값의 비트폭(1..32).
//   출력: fieldCount 개의 변경비트 + 변경 필드 값들.
void WriteDelta(BitWriter& w, const uint32_t* baseline, const uint32_t* current,
                int fieldCount, const int* fieldBits);

// 델타 디코드. 변경 필드는 스트림에서, 미변경 필드는 baseline 에서 out 으로. 성공 시 true.
bool ReadDelta(BitReader& r, const uint32_t* baseline, uint32_t* out,
               int fieldCount, const int* fieldBits);

} // namespace mye::net
