// mye/phys/SpatialHash.cpp — 균일 공간 해시 그리드 브로드페이즈 (docs/03 §8)
#include "mye/phys/SpatialHash.h"

#include <algorithm>

namespace mye::phys {

void SpatialHash::Clear() {
    m_items.clear();
    m_cells.clear();
}

void SpatialHash::Reserve(size_t itemCount) {
    m_items.reserve(itemCount);
    m_cells.reserve(itemCount * 2);
}

void SpatialHash::Insert(const BroadphaseItem& item) {
    const uint32_t idx = static_cast<uint32_t>(m_items.size());
    m_items.push_back(item);

    const int32_t x0 = CellCoord(item.min.x);
    const int32_t y0 = CellCoord(item.min.y);
    const int32_t x1 = CellCoord(item.max.x);
    const int32_t y1 = CellCoord(item.max.y);
    for (int32_t cy = y0; cy <= y1; ++cy) {
        for (int32_t cx = x0; cx <= x1; ++cx) {
            m_cells[CellKey{cx, cy}].push_back(idx);
        }
    }
}

void SpatialHash::QueryPairs(const std::function<void(uint32_t, uint32_t)>& fn) const {
    // 후보 쌍 중복 제거: 셀마다 나오는 (i,j) 쌍을 한 번만 방출.
    // 큰 AABB가 여러 셀을 공유해 같은 쌍이 반복될 수 있으므로 방출 세트로 dedup.
    // 결정성·안정성 위해 정렬된 쌍 벡터로 dedup.
    std::vector<uint64_t> emitted;
    for (const auto& [key, list] : m_cells) {
        const size_t n = list.size();
        for (size_t a = 0; a < n; ++a) {
            for (size_t b = a + 1; b < n; ++b) {
                uint32_t i = list[a];
                uint32_t j = list[b];
                if (i > j) std::swap(i, j);
                // AABB 실제 겹침 사전필터(셀 공유하지만 실제로는 안 겹칠 수 있음).
                const BroadphaseItem& A = m_items[i];
                const BroadphaseItem& B = m_items[j];
                if (A.max.x < B.min.x || B.max.x < A.min.x ||
                    A.max.y < B.min.y || B.max.y < A.min.y) {
                    continue;
                }
                emitted.push_back((static_cast<uint64_t>(i) << 32) | j);
            }
        }
    }
    std::sort(emitted.begin(), emitted.end());
    emitted.erase(std::unique(emitted.begin(), emitted.end()), emitted.end());
    for (uint64_t key : emitted) {
        fn(static_cast<uint32_t>(key >> 32), static_cast<uint32_t>(key & 0xFFFFFFFFu));
    }
}

void SpatialHash::QueryAABB(Vec2 min, Vec2 max,
                            const std::function<void(uint32_t)>& fn) const {
    const int32_t x0 = CellCoord(min.x);
    const int32_t y0 = CellCoord(min.y);
    const int32_t x1 = CellCoord(max.x);
    const int32_t y1 = CellCoord(max.y);

    std::vector<uint32_t> emitted;
    for (int32_t cy = y0; cy <= y1; ++cy) {
        for (int32_t cx = x0; cx <= x1; ++cx) {
            auto it = m_cells.find(CellKey{cx, cy});
            if (it == m_cells.end()) continue;
            for (uint32_t idx : it->second) {
                const BroadphaseItem& item = m_items[idx];
                if (item.max.x < min.x || max.x < item.min.x ||
                    item.max.y < min.y || max.y < item.min.y) {
                    continue;
                }
                emitted.push_back(idx);
            }
        }
    }
    std::sort(emitted.begin(), emitted.end());
    emitted.erase(std::unique(emitted.begin(), emitted.end()), emitted.end());
    for (uint32_t idx : emitted) fn(idx);
}

} // namespace mye::phys
