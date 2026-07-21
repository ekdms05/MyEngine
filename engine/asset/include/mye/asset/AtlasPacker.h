// mye/asset/AtlasPacker.h — 스프라이트→아틀라스 배치·UV (docs/04 §임포터 라인업 AtlasPacker)
//
// 임포터가 아닌 **후처리 단계**. `.meta` atlas 그룹 단위로 MaxRects 패킹, 픽셀아트용 1px
// extrude 패딩. 멤버 하나 변경 시 해당 페이지만 재패킹(부분 재패킹 — 리로드 전파와 연동).
//
// M3-A 범위: 순수 데이터 패킹 인터페이스(입력 rect 목록 → 페이지·배치·UV). 실제 픽셀 복사·
//   extrude·GPU 페이지 텍스처 생성은 구현 에이전트. 순수 함수라 aseprite.exe 불요·단위 테스트 용이.
#pragma once

#include "mye/core/Base.h"
#include "mye/core/Math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mye::asset {

// 패킹 입력 — 하나의 배치 대상(스프라이트/프레임). id 로 결과와 매칭.
struct AtlasInput {
    uint64_t id = 0;        // 호출자 정의(예: SpriteFrame 인덱스)
    Vec2i    size;          // 픽셀 크기(w,h)
};

// 패킹 결과 항목 — 어느 페이지의 어디에 놓였는지 + 정규화 UV.
struct AtlasPlacement {
    uint64_t id = 0;
    uint32_t page = 0;      // 페이지 인덱스
    RectInt  rect;          // 페이지 내 픽셀 rect(패딩 제외한 실제 이미지 영역)
    Rect     uv;            // 정규화 UV(0..1) — 페이지 크기 기준
};

struct AtlasPackSettings {
    Vec2i    maxPageSize{2048, 2048};  // 페이지 최대 크기(초과 시 새 페이지)
    uint32_t padding = 1;              // 스프라이트 간 간격(픽셀)
    uint32_t extrude = 1;              // 픽셀아트 블리딩 방지 extrude(px)
    bool     powerOfTwo = false;       // 페이지 크기 2의 거듭제곱 강제
};

struct AtlasPackResult {
    std::vector<AtlasPlacement> placements;   // 입력과 동수(id 매칭)
    std::vector<Vec2i>          pageSizes;     // 페이지별 실제 크기
    bool                        overflowed = false;  // 일부가 maxPageSize에 안 들어감
};

// MaxRects 계열 패킹. 순수 계산(픽셀 데이터 불요) — 배치·UV만 산출. 실패 없음(overflow 플래그).
class AtlasPacker {
public:
    static AtlasPackResult Pack(const std::vector<AtlasInput>& inputs,
                                const AtlasPackSettings& settings);
};

} // namespace mye::asset
