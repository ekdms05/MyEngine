// mye/ddc/DynamicComponentStore.h — 엔티티↔동적 컴포넌트 부착·질의·직렬화 (엔진 확장성)
//
// 데이터드리븐 컴포넌트를 실제 ECS 컴포넌트처럼 엔티티에 붙인다. 엔티티는 uint64 키(ecs::Entity
// ::Packed()) — 엔진의 정적 ECS 에 의존하지 않고 병행 사용 가능(상위가 매핑). 월드 세이브를 위해
// 전체를 JSON 으로 저장/로드한다.
#pragma once

#include "mye/ddc/SchemaRegistry.h"

#include <cstdint>
#include <functional>
#include <string_view>
#include <unordered_map>

namespace mye::json { class Value; }

namespace mye::ddc {

using EntityId = uint64_t;   // 보통 ecs::Entity::Packed()

class DynamicComponentStore {
public:
    // 컴포넌트 부착(스키마 기본값으로 인스턴스화). 이미 있으면 기존 반환. 스키마 미등록이면 nullptr.
    DynamicComponent* Add(EntityId e, std::string_view schemaName, const SchemaRegistry& reg);

    DynamicComponent*       Get(EntityId e, std::string_view schemaName);
    const DynamicComponent* Get(EntityId e, std::string_view schemaName) const;
    bool Has(EntityId e, std::string_view schemaName) const;

    bool Remove(EntityId e, std::string_view schemaName);   // 컴포넌트 1개 제거
    void RemoveEntity(EntityId e);                          // 엔티티의 전 컴포넌트 제거

    size_t EntityCount() const { return m_entities.size(); }
    size_t ComponentCount() const;

    // 특정 스키마를 가진 모든 엔티티 순회(시스템 구현). fn(EntityId, DynamicComponent&).
    void ForEach(std::string_view schemaName, const std::function<void(EntityId, DynamicComponent&)>& fn);

    // ---- 월드 세이브 ----
    // [ { "e": <id>, "schema": "<name>", "data": { field:value... } }, ... ]
    json::Value ToJson() const;
    // 로드(누적 — 기존 유지). 스키마 미등록 항목은 건너뜀. 반환=로드된 컴포넌트 수.
    Expected<int, Error> FromJson(const json::Value& arr, const SchemaRegistry& reg);

private:
    // entity → (schemaId → component). unordered_map 노드 안정성으로 Get 포인터 유효.
    std::unordered_map<EntityId, std::unordered_map<SchemaId, DynamicComponent>> m_entities;
};

} // namespace mye::ddc
