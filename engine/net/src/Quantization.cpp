// mye/net/Quantization.cpp — 양자화 구현 (Quantization.h 참조)
#include "mye/net/Quantization.h"

#include <algorithm>
#include <cmath>

namespace mye::net {

uint32_t QuantizeFloat(float value, float min, float max, int bits) {
    if (bits < 1) bits = 1;
    if (bits > 31) bits = 31;
    if (max <= min) return 0;
    const float clamped = std::clamp(value, min, max);
    const uint32_t maxQ = (1u << bits) - 1u;
    const float norm = (clamped - min) / (max - min);          // 0..1
    return static_cast<uint32_t>(norm * static_cast<float>(maxQ) + 0.5f);
}

float DequantizeFloat(uint32_t q, float min, float max, int bits) {
    if (bits < 1) bits = 1;
    if (bits > 31) bits = 31;
    const uint32_t maxQ = (1u << bits) - 1u;
    if (maxQ == 0) return min;
    const float norm = static_cast<float>(std::min(q, maxQ)) / static_cast<float>(maxQ);
    return min + norm * (max - min);
}

float QuantStep(float min, float max, int bits) {
    if (bits < 1) bits = 1;
    if (bits > 31) bits = 31;
    const uint32_t maxQ = (1u << bits) - 1u;
    return maxQ == 0 ? 0.0f : (max - min) / static_cast<float>(maxQ);
}

} // namespace mye::net
