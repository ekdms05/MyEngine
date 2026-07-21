// mye/ecs/View.h — 다중 컴포넌트 순회(희소집합 교집합) (docs/03 §1, §핵심 API)
//
// View<Cs...>는 최소 풀을 기준으로 순회하고 나머지 풀 멤버십을 교집합으로 확인한다.
// range-for로 (Entity, Cs&...) 튜플을 순회하거나 Each(콜백) 스타일을 제공한다.
// 순회 중 구조 변경 금지 — CommandBuffer 경유(World 규칙).
#pragma once

#include "mye/ecs/ComponentPool.h"
#include "mye/ecs/ComponentType.h"
#include "mye/ecs/Entity.h"

#include <array>
#include <cstddef>
#include <functional>
#include <tuple>

namespace mye::ecs {

class World;

template <typename... Cs>
class View {
public:
    View(World& world, std::array<ComponentPool*, sizeof...(Cs)> pools);

    // 콜백 순회: fn(Entity, Cs&...). 가장 단순하고 안전한 순회 표면(권장).
    template <typename Fn>
    void Each(Fn&& fn);

    bool Empty() const;

private:
    // 파라미터 팩 Cs... 에서 타입 T의 위치(컴파일타임).
    template <typename T> static constexpr size_t Index();

    World* m_world = nullptr;
    std::array<ComponentPool*, sizeof...(Cs)> m_pools{};
    ComponentPool* m_smallest = nullptr;   // 순회 기준(최소 크기 풀)
};

} // namespace mye::ecs
