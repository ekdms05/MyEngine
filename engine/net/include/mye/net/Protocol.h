// mye/net/Protocol.h — legacy XY와 인증된 2D/XYZ 메시지 프로토콜
//
// UDP 패킷 위에 얹는 최소 메시지 집합: 접속·수락·입력·스냅샷·해제. BitStream+양자화로 인코딩.
// legacy XY 위치는 16비트 양자화, 인증된 2D/XYZ 상태는 float32. 서버권위 전제.
#pragma once

#include "mye/net/BitStream.h"
#include "mye/net/Quantization.h"
#include "mye/phys/PhysicsWorld3D.h"
#include "mye/phys/Motion2D.h"
#include <bit>

#include <cstdint>
#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace mye::net {

inline constexpr uint32_t kProtocolId = 0x4D594547u;   // 'MYEG'
inline constexpr uint16_t kProtocolVersion = 1;
// ponytail: one complete snapshot fits in 1400 bytes; use AOI before raising this limit.
inline constexpr size_t kMaxSnapshotEntities = 64;
inline constexpr float    kWorldMin = -512.0f;
inline constexpr float    kWorldMax = 512.0f;
inline constexpr int      kPosBits = 16;

enum class MsgType : uint8_t {
    Connect = 1,  // 클라 → 서버: 접속 요청
    Accept = 2,   // 서버 → 클라: 수락(내 clientId 통지)
    Input = 3,    // 클라 → 서버: 이동 입력(seq, moveX, moveY)
    Snapshot = 4, // 서버 → 클라: 엔티티 상태(tick, [netId, x, y])
    Disconnect = 5,
    Connect3D = 6,
    Accept3D = 7,
    Input3D = 8,
    Snapshot3D = 9,
    Disconnect3D = 10,
    Connect2D = 11,
    Accept2D = 12,
    Input2D = 13,
    Snapshot2D = 14,
    Disconnect2D = 15,
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
    w.WriteBits(type >= MsgType::Connect2D ? 3 : type >= MsgType::Connect3D ? 2 : kProtocolVersion, 16);
    w.WriteBits(static_cast<uint32_t>(type), 8);
}
// 헤더 검증 + 타입 반환. 실패 시 false.
inline bool ReadHeader(BitReader& r, MsgType& outType) {
    const uint32_t proto = r.ReadBits(32);
    const uint32_t version = r.ReadBits(16);
    const uint32_t t = r.ReadBits(8);
    if (!r.Ok() || proto != kProtocolId || t < 1 || t > 15 ||
        version != (t >= 11 ? 3u : t >= 6 ? 2u : kProtocolVersion))
        return false;
    outType = static_cast<MsgType>(t);
    return true;
}

// ---- 문자열(길이 접두 + 바이트) ----
inline void WriteString(BitWriter& w, std::string_view s) {
    w.WriteVarUint(s.size());
    if (!s.empty()) w.WriteBytes(s.data(), s.size());
}
// maxLen 초과·언더런 시 false(신뢰 못 할 입력 방어).
inline bool ReadString(BitReader& r, std::string& out, size_t maxLen) {
    const uint64_t n = r.ReadVarUint();
    if (!r.Ok() || n > maxLen) { out.clear(); return false; }
    out.resize(static_cast<size_t>(n));
    if (n) r.ReadBytes(out.data(), static_cast<size_t>(n));
    return r.Ok();
}

// ---- Connect(인증) ----
// 접속 요청에 자격증명(username/password)을 실어 서버가 계정 인증 후 수락하게 한다.
// 익명 접속은 빈 문자열(하위 호환) — 서버에 인증기가 없으면 그대로 수락.
inline void WriteConnect(BitWriter& w, std::string_view username, std::string_view password) {
    WriteHeader(w, MsgType::Connect);
    WriteString(w, username);
    WriteString(w, password);
}
inline bool ReadConnect(BitReader& r, std::string& username, std::string& password) {
    if (!ReadString(r, username, 64)) return false;
    if (!ReadString(r, password, 128)) return false;
    return true;
}

// ---- Input ----
inline void WriteInput(BitWriter& w, uint32_t seq, float moveX, float moveY) {
    WriteHeader(w, MsgType::Input);
    w.WriteVarUint(seq);
    w.WriteBits(QuantizeFloat(moveX, -1.0f, 1.0f, 12), 12);
    w.WriteBits(QuantizeFloat(moveY, -1.0f, 1.0f, 12), 12);
}
inline bool ReadInput(BitReader& r, uint32_t& seq, float& moveX, float& moveY) {
    const uint64_t sequence = r.ReadVarUint();
    if (!r.Ok() || sequence > UINT32_MAX) return false;
    seq = static_cast<uint32_t>(sequence);
    moveX = DequantizeFloat(r.ReadBits(12), -1.0f, 1.0f, 12);
    moveY = DequantizeFloat(r.ReadBits(12), -1.0f, 1.0f, 12);
    return r.Ok();
}

inline bool SequenceNewer(uint32_t sequence, uint32_t previous) {
    return sequence != previous && sequence - previous < 0x80000000u;
}

inline void NormalizeMove(float& x, float& y) {
    if (!std::isfinite(x) || !std::isfinite(y)) { x = y = 0; return; }
    x = std::clamp(x, -1.0f, 1.0f);
    y = std::clamp(y, -1.0f, 1.0f);
    const float length = std::hypot(x, y);
    if (length > 1) { x /= length; y /= length; }
}

inline constexpr size_t kMaxSnapshotEntities3D = 24;
inline constexpr float kFixedDelta3D = 1.0f / 60;
inline constexpr size_t kMaxSnapshotEntities2D = 40; // Complete float snapshots stay below 1400 bytes.
inline constexpr float kFixedDelta2D = kFixedDelta3D;
struct MovementInput {
    uint32_t seq = 0;
    Vec2 movement{};
    bool jump = false;
};
struct EntitySnap3D {
    uint32_t netId = 0, ack = 0;
    phys::MotionState3D state;
};
struct EntitySnap2D {
    uint32_t netId = 0, ack = 0;
    phys::MotionState2D state;
};
inline bool PacketComplete(BitReader& r, size_t bytes) {
    const size_t remaining = bytes * 8 - r.BitsRead();
    return r.Ok() && remaining < 8 && (remaining == 0 || r.ReadBits(static_cast<int>(remaining)) == 0) &&
           r.Ok();
}
inline void WriteFloat(BitWriter& w, float f) {
    w.WriteBits(std::bit_cast<uint32_t>(f), 32);
}
inline void WriteU64(BitWriter& w, uint64_t v) {
    w.WriteBits(static_cast<uint32_t>(v), 32);
    w.WriteBits(static_cast<uint32_t>(v >> 32), 32);
}
inline uint64_t ReadU64(BitReader& r) {
    const uint64_t lo = r.ReadBits(32);
    return lo | (uint64_t(r.ReadBits(32)) << 32);
}
inline float ReadFloat(BitReader& r) {
    return std::bit_cast<float>(r.ReadBits(32));
}
inline bool ValidState2D(const phys::MotionState2D& s) {
    return std::isfinite(s.position.x) && std::isfinite(s.position.y) &&
           std::abs(s.position.x) <= 100000 && std::abs(s.position.y) <= 100000 &&
           std::isfinite(s.lastMove.x) && std::isfinite(s.lastMove.y) && s.lastMove.Length() <= 2 &&
           std::isfinite(s.facingRadians) && std::abs(s.facingRadians) <= kPi &&
           s.floorLevel >= 0 && s.floorLevel < 8;
}
inline void WriteState2D(BitWriter& w, const phys::MotionState2D& s) {
    for (const auto v : {s.position, s.lastMove}) { WriteFloat(w, v.x); WriteFloat(w, v.y); }
    WriteFloat(w, s.facingRadians);
    w.WriteBits(static_cast<uint32_t>(s.floorLevel), 3);
    w.WriteBits(s.onWall, 1);
}
inline bool ReadState2D(BitReader& r, phys::MotionState2D& s) {
    for (auto* v : {&s.position, &s.lastMove}) { v->x = ReadFloat(r); v->y = ReadFloat(r); }
    s.facingRadians = ReadFloat(r);
    s.floorLevel = static_cast<int8_t>(r.ReadBits(3));
    s.onWall = r.ReadBits(1) != 0;
    return r.Ok() && ValidState2D(s);
}
inline void WriteState3D(BitWriter& w, const phys::MotionState3D& s) {
    for (auto v : {s.position, s.velocity, s.floorNormal}) {
        WriteFloat(w, v.x);
        WriteFloat(w, v.y);
        WriteFloat(w, v.z);
    }
    WriteFloat(w, s.facingRadians);
    w.WriteBits(s.grounded, 1);
    w.WriteBits(s.onWall, 1);
    w.WriteBits(s.onCeiling, 1);
}
inline bool ValidState3D(const phys::MotionState3D& s) {
    const auto finite = [](Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    return finite(s.position) && finite(s.velocity) && finite(s.floorNormal) &&
           std::isfinite(s.facingRadians) && std::abs(s.facingRadians) <= kPi &&
           std::abs(s.position.x) <= 100000 && std::abs(s.position.y) <= 100000 &&
           std::abs(s.position.z) <= 100000 && s.velocity.Length() <= 200 &&
           std::abs(s.floorNormal.Length() - 1) < .001f;
}
inline bool ReadState3D(BitReader& r, phys::MotionState3D& s) {
    for (auto* v : {&s.position, &s.velocity, &s.floorNormal}) {
        v->x = ReadFloat(r);
        v->y = ReadFloat(r);
        v->z = ReadFloat(r);
        if (!std::isfinite(v->x) || !std::isfinite(v->y) || !std::isfinite(v->z)) return false;
    }
    s.facingRadians = ReadFloat(r);
    s.grounded = r.ReadBits(1) != 0;
    s.onWall = r.ReadBits(1) != 0;
    s.onCeiling = r.ReadBits(1) != 0;
    return r.Ok() && ValidState3D(s);
}
inline void WriteMovementInput(BitWriter& w, const MovementInput& input) {
    w.WriteBits(input.seq, 32);
    WriteFloat(w, input.movement.x);
    WriteFloat(w, input.movement.y);
    w.WriteBits(input.jump, 1);
}
inline bool ReadMovementInput(BitReader& r, MovementInput& input) {
    input.seq = r.ReadBits(32);
    input.movement.x = ReadFloat(r);
    input.movement.y = ReadFloat(r);
    input.jump = r.ReadBits(1) != 0;
    return r.Ok() && input.seq != 0 && std::isfinite(input.movement.x) && std::isfinite(input.movement.y) &&
           input.movement.Length() <= 1.001f;
}

// ---- Accept ----
inline void WriteAccept(BitWriter& w, uint32_t clientId) {
    WriteHeader(w, MsgType::Accept);
    w.WriteVarUint(clientId);
}
inline uint32_t ReadAccept(BitReader& r) {
    const uint64_t id = r.ReadVarUint();
    return r.Ok() && id <= UINT32_MAX ? static_cast<uint32_t>(id) : 0;
}

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
    const uint64_t tickValue = r.ReadVarUint();
    if (!r.Ok() || tickValue > UINT32_MAX) return false;
    tick = static_cast<uint32_t>(tickValue);
    const uint64_t count = r.ReadVarUint();
    if (!r.Ok() || count > kMaxSnapshotEntities) return false;
    std::vector<EntitySnap> snapshot;
    snapshot.reserve(static_cast<size_t>(count));
    for (uint64_t i = 0; i < count; ++i) {
        EntitySnap e;
        const uint64_t id = r.ReadVarUint();
        if (!r.Ok() || id == 0 || id > UINT32_MAX) return false;
        e.netId = static_cast<uint32_t>(id);
        e.x = DequantizeFloat(r.ReadBits(kPosBits), kWorldMin, kWorldMax, kPosBits);
        e.y = DequantizeFloat(r.ReadBits(kPosBits), kWorldMin, kWorldMax, kPosBits);
        const uint64_t sequence = r.ReadVarUint();
        if (!r.Ok() || sequence > UINT32_MAX) return false;
        e.lastInputSeq = static_cast<uint32_t>(sequence);
        if (std::any_of(snapshot.begin(), snapshot.end(), [&](const auto& existing) { return existing.netId == e.netId; }))
            return false;
        snapshot.push_back(e);
    }
    if (!r.Ok()) return false;
    out = std::move(snapshot);
    return true;
}

} // namespace mye::net
