// mye/net/BitStream.cpp — 비트 직렬화 구현 (BitStream.h 참조)
#include "mye/net/BitStream.h"

#include <cstring>

namespace mye::net {

// ---- BitWriter ----
void BitWriter::WriteBits(uint32_t value, int bits) {
    if (bits <= 0 || bits > 32) return;
    const uint64_t mask = (bits == 32) ? 0xFFFFFFFFull : ((1ull << bits) - 1);
    m_scratch |= (static_cast<uint64_t>(value) & mask) << m_scratchBits;
    m_scratchBits += bits;
    m_bitCount += bits;
    FlushScratch();
}

void BitWriter::FlushScratch() {
    while (m_scratchBits >= 8) {
        m_buffer.push_back(static_cast<uint8_t>(m_scratch & 0xFF));
        m_scratch >>= 8;
        m_scratchBits -= 8;
    }
}

void BitWriter::WriteVarUint(uint64_t v) {
    // 7비트 그룹 + 연속 비트(1=더 있음). 작은 값일수록 짧다.
    do {
        uint32_t group = static_cast<uint32_t>(v & 0x7F);
        v >>= 7;
        WriteBits(group | (v != 0 ? 0x80u : 0u), 8);
    } while (v != 0);
}

void BitWriter::WriteBytes(const void* data, size_t n) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; ++i) WriteBits(p[i], 8);
}

const std::vector<uint8_t>& BitWriter::Finish() {
    if (!m_finished) {
        if (m_scratchBits > 0) {
            m_buffer.push_back(static_cast<uint8_t>(m_scratch & 0xFF));
            m_scratch = 0;
            m_scratchBits = 0;
        }
        m_finished = true;
    }
    return m_buffer;
}

// ---- BitReader ----
uint32_t BitReader::ReadBits(int bits) {
    if (bits <= 0 || bits > 32) { m_ok = false; return 0; }
    while (m_scratchBits < bits) {
        if (m_bytePos >= m_size) { m_ok = false; return 0; }
        m_scratch |= static_cast<uint64_t>(m_data[m_bytePos++]) << m_scratchBits;
        m_scratchBits += 8;
    }
    const uint64_t mask = (bits == 32) ? 0xFFFFFFFFull : ((1ull << bits) - 1);
    const uint32_t result = static_cast<uint32_t>(m_scratch & mask);
    m_scratch >>= bits;
    m_scratchBits -= bits;
    m_bitsRead += bits;
    return result;
}

uint64_t BitReader::ReadVarUint() {
    uint64_t v = 0;
    int shift = 0;
    for (int i = 0; i < 10; ++i) {   // 최대 10그룹(64비트)
        const uint32_t byte = ReadBits(8);
        if (!m_ok) return 0;
        v |= static_cast<uint64_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) break;
        shift += 7;
    }
    return v;
}

void BitReader::ReadBytes(void* out, size_t n) {
    uint8_t* p = static_cast<uint8_t*>(out);
    for (size_t i = 0; i < n; ++i) p[i] = static_cast<uint8_t>(ReadBits(8));
}

} // namespace mye::net
