// mye/ecs/World.cpp — ECS 저장소 구현 (docs/03 §1)
#include "mye/ecs/World.h"

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
