// mye/phys/SpatialHash.cpp — 균일 공간 해시 그리드 브로드페이즈 (docs/03 §8)
#include "mye/phys/SpatialHash.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mye::phys {
namespace {

bool ValidBounds(Vec2 min, Vec2 max) {
    return std::isfinite(min.x) && std::isfinite(min.y) &&
           std::isfinite(max.x) && std::isfinite(max.y) &&
           min.x <= max.x && min.y <= max.y;
}

bool Intersects(const BroadphaseItem& item, Vec2 min, Vec2 max) {
    return item.max.x >= min.x && max.x >= item.min.x &&
           item.max.y >= min.y && max.y >= item.min.y;
}

} // namespace

void SpatialHash::SetCellSize(float cellSize) {
    m_cellSize = std::isfinite(cellSize) && cellSize > 0 ? cellSize : 1.0f;
    Clear();
}

std::optional<SpatialHash::CellRange> SpatialHash::IndexedRange(Vec2 min, Vec2 max) const {
    const double x0 = std::floor(static_cast<double>(min.x) / m_cellSize);
    const double y0 = std::floor(static_cast<double>(min.y) / m_cellSize);
    const double x1 = std::floor(static_cast<double>(max.x) / m_cellSize);
    const double y1 = std::floor(static_cast<double>(max.y) / m_cellSize);
    constexpr double low = std::numeric_limits<int32_t>::min();
    constexpr double high = std::numeric_limits<int32_t>::max();
    // ponytail: at most 4096 cells per item/query; larger bounds scan items in O(n).
    // Replace the linear fallback with a tree only if measured large-body traffic warrants it.
    if (x0 < low || y0 < low || x1 > high || y1 > high ||
        (x1 - x0 + 1) * (y1 - y0 + 1) > 4096) return std::nullopt;
    return CellRange{static_cast<int32_t>(x0), static_cast<int32_t>(y0),
                     static_cast<int32_t>(x1), static_cast<int32_t>(y1)};
}

void SpatialHash::Clear() {
    m_items.clear();
    m_unindexed.clear();
    m_cells.clear();
}

void SpatialHash::Reserve(size_t itemCount) {
    m_items.reserve(itemCount);
    m_cells.reserve(itemCount * 2);
}

Expected<void, Error> SpatialHash::Insert(const BroadphaseItem& item) {
    if (!ValidBounds(item.min, item.max))
        return Error{"Spatial hash bounds must be finite and ordered", 1};
    if (m_items.size() >= std::numeric_limits<uint32_t>::max())
        return Error{"Spatial hash item index exhausted", 1};
    const uint32_t idx = static_cast<uint32_t>(m_items.size());
    m_items.push_back(item);

    const auto range = IndexedRange(item.min, item.max);
    if (!range) {
        m_unindexed.push_back(idx);
        return {};
    }
    for (int64_t cy = range->y0; cy <= range->y1; ++cy) {
        for (int64_t cx = range->x0; cx <= range->x1; ++cx) {
            m_cells[CellKey{static_cast<int32_t>(cx), static_cast<int32_t>(cy)}].push_back(idx);
        }
    }
    return {};
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
                if (!Intersects(A, B.min, B.max)) continue;
                emitted.push_back((static_cast<uint64_t>(i) << 32) | j);
            }
        }
    }
    for (uint32_t i : m_unindexed) {
        for (uint32_t j = 0; j < m_items.size(); ++j) {
            if (i == j || !Intersects(m_items[i], m_items[j].min, m_items[j].max)) continue;
            emitted.push_back((static_cast<uint64_t>(std::min(i, j)) << 32) | std::max(i, j));
        }
    }
    std::sort(emitted.begin(), emitted.end());
    emitted.erase(std::unique(emitted.begin(), emitted.end()), emitted.end());
    for (uint64_t key : emitted) {
        fn(static_cast<uint32_t>(key >> 32), static_cast<uint32_t>(key & 0xFFFFFFFFu));
    }
}

Expected<void, Error> SpatialHash::QueryAABB(Vec2 min, Vec2 max,
                                          const std::function<void(uint32_t)>& fn) const {
    if (!ValidBounds(min, max)) return Error{"Spatial hash query bounds must be finite and ordered", 1};
    const auto range = IndexedRange(min, max);
    if (!range) {
        for (uint32_t i = 0; i < m_items.size(); ++i)
            if (Intersects(m_items[i], min, max)) fn(i);
        return {};
    }

    std::vector<uint32_t> emitted;
    for (int64_t cy = range->y0; cy <= range->y1; ++cy) {
        for (int64_t cx = range->x0; cx <= range->x1; ++cx) {
            auto it = m_cells.find(CellKey{static_cast<int32_t>(cx), static_cast<int32_t>(cy)});
            if (it == m_cells.end()) continue;
            for (uint32_t idx : it->second) {
                const BroadphaseItem& item = m_items[idx];
                if (!Intersects(item, min, max)) continue;
                emitted.push_back(idx);
            }
        }
    }
    for (uint32_t i : m_unindexed)
        if (Intersects(m_items[i], min, max)) emitted.push_back(i);
    std::sort(emitted.begin(), emitted.end());
    emitted.erase(std::unique(emitted.begin(), emitted.end()), emitted.end());
    for (uint32_t idx : emitted) fn(idx);
    return {};
}

} // namespace mye::phys
