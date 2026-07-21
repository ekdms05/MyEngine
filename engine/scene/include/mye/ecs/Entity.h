// mye/ecs/Entity.h — 64비트 엔티티 핸들 (docs/03 §1 엔티티 핸들)
//
// 64비트 = index:32 | generation:32. 파괴 시 generation 증가로 dangling 핸들 무효화.
// 이 핸들 규약은 05(Lua)·07(에디터 선택 상태)이 그대로 사용한다 — 동결 계약.
#pragma once

#include <cstdint>
#include <compare>

namespace mye::ecs {

struct Entity {
    uint32_t index = 0;
    uint32_t generation = 0;   // 0 = null 세대(무효). 유효 엔티티는 generation >= 1.

    static constexpr Entity Null() { return Entity{0, 0}; }
    constexpr bool IsNull() const { return generation == 0; }

    // 64비트 패킹 표현(index 하위 32 | generation 상위 32) — 직렬화·해시 키·Lua 경계용.
    constexpr uint64_t Packed() const {
        return (static_cast<uint64_t>(generation) << 32) | static_cast<uint64_t>(index);
    }
    static constexpr Entity FromPacked(uint64_t v) {
        return Entity{static_cast<uint32_t>(v & 0xFFFFFFFFu),
                      static_cast<uint32_t>(v >> 32)};
    }

    constexpr auto operator<=>(const Entity&) const = default;
    constexpr bool operator==(const Entity&) const = default;
};

} // namespace mye::ecs

// std::unordered_map 키 등으로 쓸 수 있도록 해시 특수화.
namespace std {
template <> struct hash<mye::ecs::Entity> {
    size_t operator()(const mye::ecs::Entity& e) const noexcept {
        return static_cast<size_t>(e.Packed());
    }
};
} // namespace std
