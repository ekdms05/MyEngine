// mye/ecs/World.cpp — ECS 저장소 구현 (docs/03 §1)
#include "mye/ecs/World.h"

#include <algorithm>
#include <vector>

namespace mye::ecs {

struct World::Impl {
    // 엔티티 세대 테이블(index → generation). 0 = 죽음/미사용.
    std::vector<uint32_t> generations;
    std::vector<uint32_t> freeIndices;

    // 타입 → 풀.
    std::unordered_map<ComponentTypeId, std::unique_ptr<ComponentPool>> pools;

    uint32_t AllocIndex() {
        if (!freeIndices.empty()) {
            const uint32_t idx = freeIndices.back();
            freeIndices.pop_back();
            return idx;
        }
        const uint32_t idx = static_cast<uint32_t>(generations.size());
        generations.push_back(1);   // 유효 세대는 1부터
        return idx;
    }
};

World::World() : m_impl(std::make_unique<Impl>()) {}
World::~World() = default;

Entity World::Create() {
    const uint32_t idx = m_impl->AllocIndex();
    if (m_impl->generations[idx] == 0) m_impl->generations[idx] = 1;
    return Entity{idx, m_impl->generations[idx]};
}

Entity World::CreateWithId(uint32_t index, uint32_t generation) {
    if (generation == 0) return Entity::Null();   // null 세대는 살아있는 핸들일 수 없다

    auto& gens = m_impl->generations;
    auto& freeList = m_impl->freeIndices;

    // 슬롯 생사 규약: 이 World는 파괴 시에도 generation을 0으로 두지 않고 증가시키며(항상 유효
    // 세대 유지), 대신 freelist 멤버십으로 "빈 슬롯"을 추적한다. 따라서 살아있음의 판정은
    // "범위 안 && freelist에 없음"이다(gens[index] != 0 은 살아있음의 근거가 아니다).
    if (index < gens.size()) {
        auto freeIt = std::find(freeList.begin(), freeList.end(), index);
        const bool inFreeList = (freeIt != freeList.end());
        if (!inFreeList) return Entity::Null();   // 살아있는 슬롯 → 중복 재생성 금지
        // 빈 슬롯 → freelist에서 제거하고 지정 세대로 되살린다(컴포넌트는 파괴 시 이미 제거됨).
        freeList.erase(freeIt);
        gens[index] = generation;
        return Entity{index, generation};
    }

    // 세대 테이블 밖 index → 사이 슬롯을 죽은(0) 세대로 채워 확장하고 freelist에 등록.
    // (확장으로 생긴 슬롯은 아직 한 번도 산 적 없으므로 freelist 멤버 = 빈 슬롯 규약과 정합.)
    const uint32_t oldSize = static_cast<uint32_t>(gens.size());
    gens.resize(static_cast<size_t>(index) + 1, 0);
    for (uint32_t i = oldSize; i < index; ++i) freeList.push_back(i);
    gens[index] = generation;
    return Entity{index, generation};
}

void World::Destroy(Entity e) {
    if (!Valid(e)) return;
    // 모든 풀에서 컴포넌트 제거.
    for (auto& [type, pool] : m_impl->pools)
        pool->Remove(e.index);
    // 세대 증가로 dangling 무효화(0은 건너뜀 → 항상 유효 세대 유지).
    uint32_t& gen = m_impl->generations[e.index];
    gen = gen + 1;
    if (gen == 0) gen = 1;
    m_impl->freeIndices.push_back(e.index);
}

bool World::Valid(Entity e) const {
    return !e.IsNull() && e.index < m_impl->generations.size() &&
           m_impl->generations[e.index] == e.generation;
}

ComponentTypeId World::RegisterComponent(const ComponentTypeDesc& desc) {
    if (m_impl->pools.find(desc.id) == m_impl->pools.end())
        m_impl->pools.emplace(desc.id, std::make_unique<ComponentPool>(desc));
    return desc.id;
}

bool World::IsRegistered(ComponentTypeId type) const {
    return m_impl->pools.find(type) != m_impl->pools.end();
}

void* World::AddDynamic(Entity e, ComponentTypeId type) {
    if (!Valid(e)) return nullptr;
    ComponentPool* pool = Pool(type);
    if (!pool) return nullptr;
    return pool->Emplace(e.index);
}

void* World::TryGetDynamic(Entity e, ComponentTypeId type) {
    if (!Valid(e)) return nullptr;
    ComponentPool* pool = Pool(type);
    return pool ? pool->TryGet(e.index) : nullptr;
}

const void* World::TryGetDynamic(Entity e, ComponentTypeId type) const {
    if (!Valid(e)) return nullptr;
    const ComponentPool* pool = Pool(type);
    return pool ? pool->TryGet(e.index) : nullptr;
}

bool World::HasDynamic(Entity e, ComponentTypeId type) const {
    if (!Valid(e)) return false;
    const ComponentPool* pool = Pool(type);
    return pool && pool->Has(e.index);
}

void World::RemoveDynamic(Entity e, ComponentTypeId type) {
    if (!Valid(e)) return;
    if (ComponentPool* pool = Pool(type)) pool->Remove(e.index);
}

ComponentPool* World::Pool(ComponentTypeId type) {
    auto it = m_impl->pools.find(type);
    return it == m_impl->pools.end() ? nullptr : it->second.get();
}

const ComponentPool* World::Pool(ComponentTypeId type) const {
    auto it = m_impl->pools.find(type);
    return it == m_impl->pools.end() ? nullptr : it->second.get();
}

uint32_t World::EntityGeneration(uint32_t index) const {
    return index < m_impl->generations.size() ? m_impl->generations[index] : 0;
}

} // namespace mye::ecs
