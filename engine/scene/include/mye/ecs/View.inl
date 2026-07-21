// mye/ecs/View.inl — View 템플릿 구현 (World.h 말미에서 포함)
#pragma once

#include "mye/ecs/View.h"

#include <initializer_list>
#include <type_traits>

namespace mye::ecs {

template <typename... Cs>
View<Cs...>::View(World& world, std::array<ComponentPool*, sizeof...(Cs)> pools)
    : m_world(&world), m_pools(pools) {
    // 최소 크기 풀을 순회 기준으로 선택(교집합 비용 최소화).
    for (ComponentPool* p : m_pools) {
        if (!p) { m_smallest = nullptr; return; }   // 미등록 풀 → 빈 뷰
        if (!m_smallest || p->Size() < m_smallest->Size()) m_smallest = p;
    }
}

template <typename... Cs>
bool View<Cs...>::Empty() const {
    return m_smallest == nullptr || m_smallest->Size() == 0;
}

template <typename... Cs>
template <typename Fn>
void View<Cs...>::Each(Fn&& fn) {
    if (!m_smallest) return;
    // 기준 풀의 dense 엔티티를 순회하며 나머지 풀 멤버십 확인.
    const auto& entities = m_smallest->DenseEntities();
    for (size_t i = 0; i < entities.size(); ++i) {
        const uint32_t idx = entities[i];
        bool all = true;
        (void)std::initializer_list<int>{
            (all = all && m_pools[Index<Cs>()] && m_pools[Index<Cs>()]->Has(idx), 0)...};
        if (!all) continue;

        Entity e{idx, m_world->EntityGeneration(idx)};
        fn(e, *static_cast<Cs*>(m_pools[Index<Cs>()]->TryGet(idx))...);
    }
}

// 파라미터 팩에서 각 타입의 위치를 찾는 컴파일타임 인덱스.
template <typename... Cs>
template <typename T>
constexpr size_t View<Cs...>::Index() {
    size_t idx = 0, found = 0, cur = 0;
    (void)std::initializer_list<int>{
        ((std::is_same_v<T, Cs> ? (found = cur) : 0), ++cur, 0)...};
    (void)idx;
    return found;
}

// World::Query 정의 — View 완전 정의가 필요하므로 여기(World.h 말미 포함) 배치.
template <typename... Cs>
View<Cs...> World::Query() {
    return View<Cs...>(*this, std::array<ComponentPool*, sizeof...(Cs)>{
        Pool(Cs::kComponentTypeId)...});
}

} // namespace mye::ecs
