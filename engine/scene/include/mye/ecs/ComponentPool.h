// mye/ecs/ComponentPool.h — 희소집합 컴포넌트 풀 (docs/03 §1 희소집합 채택)
//
// EnTT식 sparse set: dense 배열(연속 컴포넌트 데이터) + sparse 배열(entity index → dense 슬롯).
// 추가/제거 O(1)(swap-remove), 랜덤 액세스 O(1). 동적(비템플릿) 풀이라 플러그인·Lua가
// 등록한 임의 타입도 ComponentTypeDesc만으로 저장한다.
//
// M2-A: 계약 + 동작 가능한 기준 구현. group 정렬 최적화(docs/03 §1-4)는 후속.
#pragma once

#include "mye/ecs/ComponentType.h"
#include "mye/ecs/Entity.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mye::ecs {

// 타입 소거 희소집합 풀. 원소 크기·생성/파괴/이동은 ComponentTypeDesc가 제공.
class ComponentPool {
public:
    explicit ComponentPool(const ComponentTypeDesc& desc);
    ~ComponentPool();
    ComponentPool(const ComponentPool&) = delete;
    ComponentPool& operator=(const ComponentPool&) = delete;

    ComponentTypeId TypeId() const { return m_desc.id; }
    const ComponentTypeDesc& Desc() const { return m_desc; }
    size_t Size() const { return m_denseToEntity.size(); }

    bool  Has(uint32_t entityIndex) const;
    // 원소 추가(기본 생성) 또는 기존 반환. 반환 = 원소 데이터 포인터(타입 T*로 캐스트).
    void* Emplace(uint32_t entityIndex);
    void* TryGet(uint32_t entityIndex);
    const void* TryGet(uint32_t entityIndex) const;
    void  Remove(uint32_t entityIndex);   // swap-remove(O(1))

    // 순회용: dense 배열 접근(엔티티 index 병렬 배열 포함).
    const std::vector<uint32_t>& DenseEntities() const { return m_denseToEntity; }
    void* DataAt(size_t denseSlot);       // denseSlot < Size()

private:
    static constexpr uint32_t kInvalid = 0xFFFFFFFFu;

    ComponentTypeDesc     m_desc;
    std::vector<uint32_t> m_sparse;        // entityIndex → dense slot (또는 kInvalid)
    std::vector<uint32_t> m_denseToEntity; // dense slot → entityIndex
    std::vector<std::byte> m_storage;      // dense 원소 바이트(m_desc.size 단위)

    std::byte* SlotPtr(size_t denseSlot) { return m_storage.data() + denseSlot * m_desc.size; }
};

} // namespace mye::ecs
