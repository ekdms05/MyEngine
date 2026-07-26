// mye/net/Quantization.h — 유계 값 양자화(네트워크 압축) (docs/mmorpg/02, M9)
//
// 위치·회전·비율 등 유계 float를 정수 비트로 양자화해 BitStream에 촘촘히 싣는다. 역변환 오차는
// 해상도(범위/2^bits) 이하. 결정론(같은 입력→같은 비트) — 서버=클라 재현. 순수 로직.
#pragma once

#include <cstdint>

namespace mye::net {

// value(∈[min,max])를 bits(1..31) 정수로. 범위 밖은 클램프.
uint32_t QuantizeFloat(float value, float min, float max, int bits);
// 양자화 정수 → 근사 float.
float    DequantizeFloat(uint32_t q, float min, float max, int bits);

// 양자화 해상도(1 스텝의 월드 크기) — 오차 상한 판단용.
float    QuantStep(float min, float max, int bits);

} // namespace mye::net
