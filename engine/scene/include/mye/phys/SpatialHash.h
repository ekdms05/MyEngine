// mye/phys/SpatialHash.h — 균일 공간 해시 그리드(브로드페이즈) (docs/03 §8)
//
// 타일맵 셀 크기에 정렬 가능한 균일 그리드. 각 콜라이더의 AABB를 덮는 셀들에 엔티티를
// 삽입하고, 질의 시 후보 쌍을 O(엔티티 + 후보쌍)에 산출한다(전수 O(n²) 회피).
// 결정성: 셀 방문·후보 산출은 삽입 순서를 보존하는 안정 순회(테스트가 전수와 대조 가능).
#pragma once

#include "mye/core/Math.h"
#include "mye/ecs/Entity.h"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace mye::phys {

using ecs::Entity;

// 브로드페이즈 대상 항목: 엔티티 + 월드 AABB(min/max).
struct BroadphaseItem {
    Entity entity = Entity::Null();
    Vec2   min{0, 0};
    Vec2   max{0, 0};
    uint32_t userIndex = 0;   // 호출자 배열 인덱스(콜라이더 목록 등 역참조용)
};

class SpatialHash {
public:
    explicit SpatialHash(float cellSize = 1.0f) : m_cellSize(cellSize > 0 ? cellSize : 1.0f) {}

    void SetCellSize(float cellSize) { m_cellSize = cellSize > 0 ? cellSize : 1.0f; }
    float CellSize() const { return m_cellSize; }

    void Clear();
    void Reserve(size_t itemCount);

    // 항목 삽입 — AABB가 덮는 모든 셀에 등록. 삽입 순서(등록 인덱스)를 보존한다.
    void Insert(const BroadphaseItem& item);

    // 후보 쌍 산출: AABB가 셀을 공유하는 (i, j) 쌍을 중복 없이 콜백. i<j(삽입 인덱스 기준).
    // 콜백 인자는 Insert 순서로 부여된 항목 인덱스(0-based).
    void QueryPairs(const std::function<void(uint32_t i, uint32_t j)>& fn) const;

    // 한 AABB와 겹치는 후보 항목 인덱스 산출(질의 자신은 결과에서 제외 옵션).
    void QueryAABB(Vec2 min, Vec2 max,
                   const std::function<void(uint32_t itemIndex)>& fn) const;

    size_t ItemCount() const { return m_items.size(); }
    const BroadphaseItem& Item(uint32_t idx) const { return m_items[idx]; }

private:
    struct CellKey {
        int32_t x = 0, y = 0;
        bool operator==(const CellKey& o) const { return x == o.x && y == o.y; }
    };
    struct CellKeyHash {
        size_t operator()(const CellKey& k) const noexcept {
            // 2D 정수 셀 좌표 해시(스플릿믹스식 혼합).
            uint64_t h = (static_cast<uint64_t>(static_cast<uint32_t>(k.x)) << 32) ^
                         static_cast<uint32_t>(k.y);
            h ^= h >> 33; h *= 0xff51afd7ed558ccdull; h ^= h >> 33;
            return static_cast<size_t>(h);
        }
    };

    int32_t CellCoord(float v) const {
        return static_cast<int32_t>(std::floor(v / m_cellSize));
    }

    float m_cellSize = 1.0f;
    std::vector<BroadphaseItem> m_items;
    // 셀 → 그 셀에 등록된 항목 인덱스 목록.
    std::unordered_map<CellKey, std::vector<uint32_t>, CellKeyHash> m_cells;
};

} // namespace mye::phys
