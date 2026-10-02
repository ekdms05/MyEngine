// mye/phys/SpatialHash.h — 균일 공간 해시 그리드(브로드페이즈) (docs/03 §8)
//
// 일반 AABB는 셀에 등록하며 큰 영역/셀 정수 범위 밖 항목은 선형 검사한다.
// 후보 결과는 삽입 인덱스로 정렬·중복 제거한다. 큰 항목이 많으면 최악 O(n²)이다.
#pragma once

#include "mye/core/Base.h"
#include "mye/core/Math.h"
#include "mye/ecs/Entity.h"

#include <cstdint>
#include <functional>
#include <optional>
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
    explicit SpatialHash(float cellSize = 1.0f) { SetCellSize(cellSize); }

    // Changing the grid discards its items; the owner rebuilds them at the next step.
    void SetCellSize(float cellSize);
    float CellSize() const { return m_cellSize; }

    void Clear();
    void Reserve(size_t itemCount);

    // 유한·순서가 올바른 AABB만 삽입한다. 실패 시 항목 수는 변하지 않는다.
    Expected<void, Error> Insert(const BroadphaseItem& item);

    // 겹치는 AABB 쌍을 중복 없이 콜백. i<j(삽입 인덱스 기준).
    // 콜백 인자는 Insert 순서로 부여된 항목 인덱스(0-based).
    void QueryPairs(const std::function<void(uint32_t i, uint32_t j)>& fn) const;

    // 한 AABB와 겹치는 항목 인덱스 산출. 잘못된 범위는 콜백 전에 거부한다.
    Expected<void, Error> QueryAABB(Vec2 min, Vec2 max,
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

    struct CellRange { int32_t x0, y0, x1, y1; };
    // Large/out-of-grid bounds use exact linear overlap checks instead of cell insertion.
    std::optional<CellRange> IndexedRange(Vec2 min, Vec2 max) const;

    float m_cellSize = 1.0f;
    std::vector<BroadphaseItem> m_items;
    std::vector<uint32_t> m_unindexed;
    // 셀 → 그 셀에 등록된 항목 인덱스 목록.
    std::unordered_map<CellKey, std::vector<uint32_t>, CellKeyHash> m_cells;
};

} // namespace mye::phys
