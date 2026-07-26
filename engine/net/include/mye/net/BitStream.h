// mye/net/BitStream.h — 비트 단위 직렬화 (docs/mmorpg/02, M9)
//
// 네트워크 스냅샷/델타를 촘촘히 패킹하기 위한 LSB-first 비트 라이터/리더. 정수 비트폭 지정,
// 불린 1비트, 가변길이 정수(VarUint), 바이트 블록. 순수 로직(소켓 무관) — 결정론·유닛 테스트.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mye::net {

class BitWriter {
public:
    // value의 하위 bits(1..32)비트를 기록.
    void WriteBits(uint32_t value, int bits);
    void WriteBool(bool b) { WriteBits(b ? 1u : 0u, 1); }
    void WriteVarUint(uint64_t v);            // 7비트 그룹 + 연속 비트
    void WriteBytes(const void* data, size_t n);

    // 바이트 경계로 패딩하고 버퍼 반환.
    const std::vector<uint8_t>& Finish();
    size_t BitCount() const { return m_bitCount; }

private:
    std::vector<uint8_t> m_buffer;
    uint64_t m_scratch = 0;
    int      m_scratchBits = 0;
    size_t   m_bitCount = 0;
    bool     m_finished = false;
    void FlushScratch();
};

class BitReader {
public:
    BitReader(const uint8_t* data, size_t size) : m_data(data), m_size(size) {}
    explicit BitReader(const std::vector<uint8_t>& v) : m_data(v.data()), m_size(v.size()) {}

    uint32_t ReadBits(int bits);
    bool     ReadBool() { return ReadBits(1) != 0; }
    uint64_t ReadVarUint();
    void     ReadBytes(void* out, size_t n);

    bool Ok() const { return m_ok; }
    size_t BitsRead() const { return m_bitsRead; }

private:
    const uint8_t* m_data = nullptr;
    size_t   m_size = 0;
    size_t   m_bytePos = 0;
    uint64_t m_scratch = 0;
    int      m_scratchBits = 0;
    size_t   m_bitsRead = 0;
    bool     m_ok = true;
};

} // namespace mye::net
