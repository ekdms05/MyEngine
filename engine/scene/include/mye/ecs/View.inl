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
    // 순회 시작 시점의 크기를 스냅샷해 루프 상한을 고정한다: 순회 중 시스템이 CommandBuffer
    // (또는 즉시 경로)로 기준 풀에 엔티티를 추가해도 이번 순회에서 새 엔티티가 즉시 방문되지 않도록
    // 한다(지연 반영 계약 방어 — 결정성·재현성). dense 벡터가 재할당돼도 인덱스 접근은 매 반복
    // size()를 재평가하지 않고 스냅샷 상한까지만 돈다.
    const auto& entities = m_smallest->DenseEntities();
    const size_t count = entities.size();
    for (size_t i = 0; i < count; ++i) {
        if (i >= entities.size()) break;   // 방어: 순회 중 축소(비계약 즉시 제거) 시 오버리드 차단
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
