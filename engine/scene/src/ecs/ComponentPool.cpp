// mye/ecs/ComponentPool.cpp — 희소집합 풀 구현 (docs/03 §1)
#include "mye/ecs/ComponentPool.h"

namespace mye::ecs {

ComponentPool::ComponentPool(const ComponentTypeDesc& desc) : m_desc(desc) {}

ComponentPool::~ComponentPool() {
    // 남은 원소 파괴.
    if (m_desc.destruct) {
        for (size_t i = 0; i < m_denseToEntity.size(); ++i)
            m_desc.destruct(SlotPtr(i));
    }
}

bool ComponentPool::Has(uint32_t entityIndex) const {
    return entityIndex < m_sparse.size() && m_sparse[entityIndex] != kInvalid;
}

void* ComponentPool::Emplace(uint32_t entityIndex) {
    if (entityIndex >= m_sparse.size())
        m_sparse.resize(entityIndex + 1, kInvalid);
    if (m_sparse[entityIndex] != kInvalid)
        return SlotPtr(m_sparse[entityIndex]);   // 이미 존재

    const uint32_t denseSlot = static_cast<uint32_t>(m_denseToEntity.size());
    m_sparse[entityIndex] = denseSlot;
    m_denseToEntity.push_back(entityIndex);
    m_storage.resize(m_storage.size() + m_desc.size);
    void* p = SlotPtr(denseSlot);
    if (m_desc.construct) m_desc.construct(p);
    return p;
}

void* ComponentPool::TryGet(uint32_t entityIndex) {
    if (!Has(entityIndex)) return nullptr;
    return SlotPtr(m_sparse[entityIndex]);
}

const void* ComponentPool::TryGet(uint32_t entityIndex) const {
    if (!Has(entityIndex)) return nullptr;
    return const_cast<ComponentPool*>(this)->SlotPtr(m_sparse[entityIndex]);
}

void ComponentPool::Remove(uint32_t entityIndex) {
    if (!Has(entityIndex)) return;
    const uint32_t denseSlot = m_sparse[entityIndex];
    const uint32_t lastSlot = static_cast<uint32_t>(m_denseToEntity.size() - 1);

    void* removed = SlotPtr(denseSlot);
    if (m_desc.destruct) m_desc.destruct(removed);

    if (denseSlot != lastSlot) {
        // 마지막 원소를 빈 슬롯으로 이동(swap-remove).
        void* last = SlotPtr(lastSlot);
        if (m_desc.moveConstruct) m_desc.moveConstruct(removed, last);
        const uint32_t movedEntity = m_denseToEntity[lastSlot];
        m_denseToEntity[denseSlot] = movedEntity;
        m_sparse[movedEntity] = denseSlot;
    }

    m_denseToEntity.pop_back();
    m_storage.resize(m_storage.size() - m_desc.size);
    m_sparse[entityIndex] = kInvalid;
}

void* ComponentPool::DataAt(size_t denseSlot) {
    return SlotPtr(denseSlot);
}

} // namespace mye::ecs
