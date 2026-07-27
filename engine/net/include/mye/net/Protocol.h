// mye/net/Protocol.h — 클라이언트↔서버 메시지 프로토콜 (docs/mmorpg/02, M9)
//
// UDP 패킷 위에 얹는 최소 메시지 집합: 접속·수락·입력·스냅샷·해제. BitStream+양자화로 인코딩.
// 스냅샷은 엔티티 위치를 유계 좌표계에서 16비트로 압축. 서버권위 전제.
#pragma once

#include "mye/net/BitStream.h"
#include "mye/net/Quantization.h"

#include <cstdint>
#include <vector>

namespace mye::net {

inline constexpr uint32_t kProtocolId = 0x4D594547u;   // 'MYEG'
inline constexpr float    kWorldMin = -512.0f;
inline constexpr float    kWorldMax = 512.0f;
inline constexpr int      kPosBits = 16;

enum class MsgType : uint8_t {
    Connect    = 1,   // 클라 → 서버: 접속 요청
    Accept     = 2,   // 서버 → 클라: 수락(내 clientId 통지)
    Input      = 3,   // 클라 → 서버: 이동 입력(seq, moveX, moveY)
    Snapshot   = 4,   // 서버 → 클라: 엔티티 상태(tick, [netId, x, y])
    Disconnect = 5,
};

// 복제 엔티티 상태(스냅샷 원소). lastInputSeq 는 소유 클라의 마지막 처리 입력(재조정용).
struct EntitySnap {
    uint32_t netId = 0;
    float    x = 0.0f;
    float    y = 0.0f;
    uint32_t lastInputSeq = 0;
};

// ---- 헤더(모든 패킷 공통) ----
inline void WriteHeader(BitWriter& w, MsgType type) {
    w.WriteBits(kProtocolId, 32);
    w.WriteBits(static_cast<uint32_t>(type), 8);
}
// 헤더 검증 + 타입 반환. 실패 시 false.
inline bool ReadHeader(BitReader& r, MsgType& outType) {
    const uint32_t proto = r.ReadBits(32);
    const uint32_t t = r.ReadBits(8);
    if (!r.Ok() || proto != kProtocolId) return false;
    outType = static_cast<MsgType>(t);
    return true;
}

// ---- Input ----
inline void WriteInput(BitWriter& w, uint32_t seq, float moveX, float moveY) {
    WriteHeader(w, MsgType::Input);
    w.WriteVarUint(seq);
    w.WriteBits(QuantizeFloat(moveX, -1.0f, 1.0f, 12), 12);
    w.WriteBits(QuantizeFloat(moveY, -1.0f, 1.0f, 12), 12);
}
inline void ReadInput(BitReader& r, uint32_t& seq, float& moveX, float& moveY) {
    seq = static_cast<uint32_t>(r.ReadVarUint());
    moveX = DequantizeFloat(r.ReadBits(12), -1.0f, 1.0f, 12);
    moveY = DequantizeFloat(r.ReadBits(12), -1.0f, 1.0f, 12);
}

// ---- Accept ----
inline void WriteAccept(BitWriter& w, uint32_t clientId) {
    WriteHeader(w, MsgType::Accept);
    w.WriteVarUint(clientId);
}
inline uint32_t ReadAccept(BitReader& r) { return static_cast<uint32_t>(r.ReadVarUint()); }

// ---- Snapshot ----
inline void WriteSnapshot(BitWriter& w, uint32_t tick, const std::vector<EntitySnap>& ents) {
    WriteHeader(w, MsgType::Snapshot);
    w.WriteVarUint(tick);
    w.WriteVarUint(ents.size());
    for (const EntitySnap& e : ents) {
        w.WriteVarUint(e.netId);
        w.WriteBits(QuantizeFloat(e.x, kWorldMin, kWorldMax, kPosBits), kPosBits);
        w.WriteBits(QuantizeFloat(e.y, kWorldMin, kWorldMax, kPosBits), kPosBits);
        w.WriteVarUint(e.lastInputSeq);
    }
}
inline bool ReadSnapshot(BitReader& r, uint32_t& tick, std::vector<EntitySnap>& out) {
    tick = static_cast<uint32_t>(r.ReadVarUint());
    const uint64_t count = r.ReadVarUint();
    if (!r.Ok() || count > 100000) return false;
    out.clear();
    out.reserve(static_cast<size_t>(count));
    for (uint64_t i = 0; i < count; ++i) {
        EntitySnap e;
        e.netId = static_cast<uint32_t>(r.ReadVarUint());
        e.x = DequantizeFloat(r.ReadBits(kPosBits), kWorldMin, kWorldMax, kPosBits);
        e.y = DequantizeFloat(r.ReadBits(kPosBits), kWorldMin, kWorldMax, kPosBits);
        e.lastInputSeq = static_cast<uint32_t>(r.ReadVarUint());
        out.push_back(e);
    }
    return r.Ok();
}

} // namespace mye::net
