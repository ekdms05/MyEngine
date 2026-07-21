// AtlasPacker.cpp — 아틀라스 패킹 (docs/04 §임포터 라인업 AtlasPacker, M3-A)
//
// 순수 계산 배치기. 입력 rect 목록 → 페이지·픽셀 배치·정규화 UV 산출(픽셀 데이터 불요).
// 알고리즘: MaxRects 계열의 결정적 변형(Guillotine + Best-Area-Fit). 픽셀아트 규약에 맞춰
//   각 스프라이트에 (padding + extrude) 여백을 부여해 블리딩을 방지한다.
//
// 규약(02 인용):
//   - UV 는 텍스처 좌상단 원점(좌상단 (0,0), 우하단 (1,1)) — SpriteSheet.h rect 규약과 정합.
//   - placement.rect 는 실제 이미지 영역(extrude/padding 제외한 원본 픽셀 크기).
//   - extrude 픽셀은 소비자가 원본 가장자리를 복제해 채운다(여기서는 자리만 확보).
//   - 실패 없음: 하나라도 maxPageSize 에 못 들어가면 overflowed=true 로 리포트하고 스킵.
#include "mye/asset/AtlasPacker.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace mye::asset {

namespace {

// 2의 거듭제곱으로 올림(powerOfTwo 페이지 강제 시).
int32_t NextPow2(int32_t v) {
    if (v <= 1) return 1;
    int32_t p = 1;
    while (p < v) p <<= 1;
    return p;
}

// 하나의 페이지 — MaxRects 의 "free rectangle 목록" 유지.
struct Page {
    int32_t width = 0;   // 현재 사용된 콘텐츠 폭·높이(페이지 실제 크기 산출용)
    int32_t height = 0;
    std::vector<RectInt> freeRects;   // 아직 배치 가능한 빈 영역
};

// r 이 free 안에 완전히 포함되는지.
bool ContainedIn(const RectInt& r, const RectInt& free) {
    return r.x >= free.x && r.y >= free.y &&
           r.x + r.w <= free.x + free.w &&
           r.y + r.h <= free.y + free.h;
}

// used 가 free 를 자르고 남는 조각들을 freeRects 에 추가(MaxRects split).
void SplitFree(std::vector<RectInt>& freeRects, const RectInt& free, const RectInt& used) {
    // used 와 free 가 겹치지 않으면 그대로 유지.
    if (used.x >= free.x + free.w || used.x + used.w <= free.x ||
        used.y >= free.y + free.h || used.y + used.h <= free.y) {
        freeRects.push_back(free);
        return;
    }
    // 위쪽 조각
    if (used.y > free.y) {
        freeRects.push_back({free.x, free.y, free.w, used.y - free.y});
    }
    // 아래쪽 조각
    if (used.y + used.h < free.y + free.h) {
        freeRects.push_back({free.x, used.y + used.h, free.w,
                             (free.y + free.h) - (used.y + used.h)});
    }
    // 왼쪽 조각
    if (used.x > free.x) {
        freeRects.push_back({free.x, free.y, used.x - free.x, free.h});
    }
    // 오른쪽 조각
    if (used.x + used.w < free.x + free.w) {
        freeRects.push_back({used.x + used.w, free.y,
                             (free.x + free.w) - (used.x + used.w), free.h});
    }
}

// a 가 b 에 완전히 포함되면 true(중복 free rect 제거용).
bool IsContained(const RectInt& a, const RectInt& b) {
    return a.x >= b.x && a.y >= b.y &&
           a.x + a.w <= b.x + b.w && a.y + a.h <= b.y + b.h;
}

// 서로 포함되는 free rect 를 제거해 목록을 최소화.
void PruneFreeList(std::vector<RectInt>& freeRects) {
    for (size_t i = 0; i < freeRects.size(); ++i) {
        for (size_t j = i + 1; j < freeRects.size();) {
            if (IsContained(freeRects[i], freeRects[j])) {
                freeRects.erase(freeRects.begin() + static_cast<long>(i));
                --i;
                break;
            }
            if (IsContained(freeRects[j], freeRects[i])) {
                freeRects.erase(freeRects.begin() + static_cast<long>(j));
            } else {
                ++j;
            }
        }
    }
}

// 페이지에서 (w,h) 를 놓을 최적 free rect 를 Best-Short-Side-Fit 로 찾는다.
// 성공 시 outRect 에 배치 위치, true 반환.
bool FindPositionBSSF(const Page& page, int32_t w, int32_t h, RectInt& outRect) {
    int32_t bestShort = std::numeric_limits<int32_t>::max();
    int32_t bestLong = std::numeric_limits<int32_t>::max();
    bool found = false;
    for (const RectInt& free : page.freeRects) {
        if (free.w >= w && free.h >= h) {
            const int32_t leftoverH = free.w - w;
            const int32_t leftoverV = free.h - h;
            const int32_t shortSide = std::min(leftoverH, leftoverV);
            const int32_t longSide = std::max(leftoverH, leftoverV);
            if (shortSide < bestShort || (shortSide == bestShort && longSide < bestLong)) {
                outRect = {free.x, free.y, w, h};
                bestShort = shortSide;
                bestLong = longSide;
                found = true;
            }
        }
    }
    return found;
}

// 배치 후 페이지 free 목록 갱신.
void PlaceInPage(Page& page, const RectInt& placed) {
    std::vector<RectInt> newFree;
    newFree.reserve(page.freeRects.size());
    for (const RectInt& free : page.freeRects) {
        SplitFree(newFree, free, placed);
    }
    page.freeRects.swap(newFree);
    PruneFreeList(page.freeRects);
    page.width = std::max(page.width, placed.x + placed.w);
    page.height = std::max(page.height, placed.y + placed.h);
}

} // namespace

AtlasPackResult AtlasPacker::Pack(const std::vector<AtlasInput>& inputs,
                                  const AtlasPackSettings& settings) {
    AtlasPackResult result;
    result.placements.reserve(inputs.size());

    const int32_t maxW = std::max(1, settings.maxPageSize.x);
    const int32_t maxH = std::max(1, settings.maxPageSize.y);
    // 각 스프라이트에 부여할 총 여백 = extrude(양쪽) + padding(스프라이트 간 간격).
    // margin 은 스프라이트 rect 를 둘러싸는 셀 크기에 더해진다.
    const int32_t margin = static_cast<int32_t>(settings.padding + 2u * settings.extrude);

    // 배치 순서: 큰 면적 우선(결정적) — 동률은 원본 인덱스로 안정 정렬.
    std::vector<size_t> order(inputs.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const int64_t areaA = int64_t(inputs[a].size.x) * inputs[a].size.y;
        const int64_t areaB = int64_t(inputs[b].size.x) * inputs[b].size.y;
        if (areaA != areaB) return areaA > areaB;
        return a < b;
    });

    std::vector<Page> pages;

    // 결과를 원본 인덱스 순서로 채우기 위해 임시 저장 후 재정렬.
    std::vector<AtlasPlacement> byOrder(inputs.size());
    std::vector<bool> placedOk(inputs.size(), false);

    for (size_t oi = 0; oi < order.size(); ++oi) {
        const size_t idx = order[oi];
        const AtlasInput& in = inputs[idx];
        const int32_t imgW = std::max(0, in.size.x);
        const int32_t imgH = std::max(0, in.size.y);
        // 셀 크기 = 이미지 + 여백. 셀 자체가 페이지에 안 들어가면 overflow.
        const int32_t cellW = imgW + margin;
        const int32_t cellH = imgH + margin;

        if (cellW > maxW || cellH > maxH) {
            result.overflowed = true;
            byOrder[idx] = {in.id, 0, {0, 0, imgW, imgH}, {0, 0, 0, 0}};
            continue;
        }

        // 기존 페이지에 배치 시도.
        RectInt cellRect{};
        uint32_t pageIndex = 0;
        bool placed = false;
        for (uint32_t p = 0; p < pages.size(); ++p) {
            if (FindPositionBSSF(pages[p], cellW, cellH, cellRect)) {
                PlaceInPage(pages[p], cellRect);
                pageIndex = p;
                placed = true;
                break;
            }
        }
        // 새 페이지 생성.
        if (!placed) {
            Page page;
            page.freeRects.push_back({0, 0, maxW, maxH});
            if (FindPositionBSSF(page, cellW, cellH, cellRect)) {
                PlaceInPage(page, cellRect);
                pageIndex = static_cast<uint32_t>(pages.size());
                pages.push_back(std::move(page));
                placed = true;
            }
        }
        if (!placed) {
            result.overflowed = true;
            byOrder[idx] = {in.id, 0, {0, 0, imgW, imgH}, {0, 0, 0, 0}};
            continue;
        }

        // 이미지 실제 영역 = 셀 rect 안쪽으로 extrude 만큼 오프셋(padding 은 셀 외곽에 흡수).
        const int32_t imgX = cellRect.x + static_cast<int32_t>(settings.extrude);
        const int32_t imgY = cellRect.y + static_cast<int32_t>(settings.extrude);
        AtlasPlacement pl;
        pl.id = in.id;
        pl.page = pageIndex;
        pl.rect = {imgX, imgY, imgW, imgH};
        byOrder[idx] = pl;   // UV 는 페이지 최종 크기 확정 후 채운다.
        placedOk[idx] = true;
    }

    // 페이지 최종 크기 산출(powerOfTwo 강제 시 올림).
    result.pageSizes.resize(pages.size());
    for (size_t p = 0; p < pages.size(); ++p) {
        int32_t w = pages[p].width;
        int32_t h = pages[p].height;
        if (settings.powerOfTwo) {
            w = NextPow2(w);
            h = NextPow2(h);
        }
        w = std::min(std::max(1, w), maxW);
        h = std::min(std::max(1, h), maxH);
        result.pageSizes[p] = {w, h};
    }

    // UV 채우기(페이지 크기 확정 후) + 원본 순서로 결과 배열 구성.
    result.placements.resize(inputs.size());
    for (size_t i = 0; i < inputs.size(); ++i) {
        AtlasPlacement pl = byOrder[i];
        if (placedOk[i] && pl.page < result.pageSizes.size()) {
            const Vec2i ps = result.pageSizes[pl.page];
            const float pw = static_cast<float>(ps.x);
            const float ph = static_cast<float>(ps.y);
            pl.uv = {static_cast<float>(pl.rect.x) / pw,
                     static_cast<float>(pl.rect.y) / ph,
                     static_cast<float>(pl.rect.w) / pw,
                     static_cast<float>(pl.rect.h) / ph};
        } else {
            pl.uv = {0.0f, 0.0f, 0.0f, 0.0f};
        }
        result.placements[i] = pl;
    }

    return result;
}

} // namespace mye::asset
