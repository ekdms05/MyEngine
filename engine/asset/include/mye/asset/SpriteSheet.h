// mye/asset/SpriteSheet.h — 스프라이트시트·애니클립 데이터 에셋 (docs/04 §임포터, M3-A)
//
// Aseprite/시트 임포터의 산출물. 프레임 rect·pivot(슬라이스)·태그→클립 구간을 담는다.
// 애니메이션 시스템(03)·재생(M3-B)은 이 데이터를 소비만 한다 — 04는 데이터 스키마와 임포트만.
//
// 좌표 규약(02): 픽셀 좌표는 좌상단 원점 텍스처 공간(rect). pivot 은 프레임 로컬 픽셀(왼손·
//   +Y업 월드 배치는 소비자가 변환). UV 는 AtlasPacker 배치 후 채워진다.
#pragma once

#include "mye/asset/AssetGuid.h"
#include "mye/core/Math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mye::asset {

// 시트 내 프레임 하나 — 소스 텍스처(또는 아틀라스 페이지) 내 픽셀 rect + pivot + 아틀라스 UV.
struct SpriteFrame {
    RectInt  rect;                 // 소스/아틀라스 픽셀 rect(좌상단 원점)
    Vec2     pivot{0.5f, 0.5f};    // 프레임 로컬 정규화 피벗(0..1) 또는 픽셀(pivotInPixels)
    bool     pivotInPixels = false;
    Rect     uv;                   // 아틀라스 배치 후 UV(0..1). 미배치 시 {0,0,1,1}
    uint32_t atlasPage = 0;        // 소속 아틀라스 페이지 인덱스(AtlasPacker 결과)
};

// 애니메이션 이벤트 마커 — 프레임에 부착된 이름 있는 이벤트(예: "footstep").
//   M3-B에서 AudioCue 등으로 라우팅(발소리→AudioCue). M3-A는 데이터만 실어 나른다.
struct AnimEventMarker {
    uint32_t    frameIndex = 0;    // 클립 내 프레임 인덱스
    std::string name;              // 이벤트 이름("footstep", "attack_hit" ...)
    std::string stringArg;         // 선택 문자열 인자(큐 이름 등)
    float       floatArg = 0.0f;   // 선택 수치 인자
};

// 태그 → 애니메이션 클립 데이터. 프레임 목록·per-frame duration·루프·방향·이벤트.
struct AnimationClipData {
    MYE_ASSET_TYPE(mye::asset::AnimationClipData);

    std::string          name;             // 태그 이름("walk", "idle")
    std::vector<uint32_t> frameIndices;    // 시트의 SpriteFrame 인덱스 시퀀스
    std::vector<float>   frameDurations;   // 프레임별 지속(초). frameIndices 와 동수
    bool                 loop = true;
    // Aseprite 태그 재생 방향 규약(Forward/Reverse/PingPong) — 03 애니 시스템이 해석.
    enum class Direction : uint8_t { Forward, Reverse, PingPong } direction = Direction::Forward;
    std::vector<AnimEventMarker> events;   // 프레임 이벤트 마커
    uint32_t             version = 1;      // 스키마 버전(직렬화)
};

// 슬라이스 → pivot/9-slice/히트박스 등 이름 있는 영역(Aseprite Slice).
struct SpriteSlice {
    std::string name;              // "pivot", "@hitbox" 등 이름 규약
    RectInt     bounds;            // 슬라이스 영역(픽셀)
    bool        hasPivot = false;
    Vec2i       pivot;             // hasPivot 시 픽셀 pivot
    bool        hasCenter = false; // 9-slice center 존재 여부
    RectInt     center;            // 9-slice 중앙 rect
};

// 스프라이트시트 에셋 — 프레임 테이블 + 슬라이스 + 클립 참조 + 소스 텍스처 참조.
struct SpriteSheet {
    MYE_ASSET_TYPE(mye::asset::SpriteSheet);

    AssetRef                 texture;      // 소스/아틀라스 텍스처(하드 의존성)
    Vec2i                    frameSize;    // 그리드 임포트 시 프레임 크기(0 = 태그/rect 기반)
    std::vector<SpriteFrame> frames;       // 전체 프레임 테이블
    std::vector<SpriteSlice> slices;       // pivot·히트박스 등
    // 태그→클립: 서브 에셋으로 분리 저장될 수도 있어 참조로 둔다(.meta subAssets name→GUID).
    std::vector<AssetRef>    clips;        // AnimationClipData 서브에셋 참조
    uint32_t                 version = 1;
};

} // namespace mye::asset
