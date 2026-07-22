// mye/ui/UiTypes.h — 인게임 UI 공용 값 타입 (docs/06 §1 인게임 UI 시스템)
//
// 소유: 06 (M5-A). 앵커·RectTransform·스타일·9-slice 등 무의존 데이터 타입. ImGui 아님 —
//   SpriteBatch 기반 자체 리테인드 트리. 좌표: 스크린 스페이스 픽셀(정수 스냅), 좌상단 원점(UI 관례).
//
// 좌표계 주의: 월드 렌더는 +Y 업(왼손, 02)이지만 UI 는 화면 픽셀 좌표로 +Y 아래(좌상단 원점)를
//   쓴다 — UiRenderer 가 UI 픽셀 좌표 → SpriteBatch 스크린 카메라로 변환한다(정본은 UiRenderer).
#pragma once

#include "mye/core/Math.h"

#include <cstdint>
#include <string_view>

namespace mye::ui {

// UI 픽셀 사각형(좌상단 원점, +Y 아래). core::Rect(float) 재사용.
using UiRect = Rect;

// 앵커 기반 RectTransform (Unity 유사 — docs/06 §1 레이아웃).
//   anchorMin/Max: 부모 사각형 대비 정규화(0..1). Min==Max 면 점 앵커(고정 크기), 다르면 스트레치.
//   pivot: 자기 사각형 대비 정규화 피벗(0..1). offsetMin/Max: 앵커로부터 픽셀 오프셋.
//     - 점 앵커: offsetMin = 위치(pivot 기준), sizeDelta 로 크기. (아래 두 표현 중 하나만 유효)
//   MVP 는 "앵커 + 오프셋(=마진)" 모델을 정본으로 한다: 좌하(offsetMin), 우상(offsetMax) 픽셀 마진.
struct AnchorRect {
    Vec2 anchorMin{0.0f, 0.0f};   // 좌하단 앵커(정규화) — UI +Y 아래 규약상 (좌, 상) 로 해석
    Vec2 anchorMax{0.0f, 0.0f};
    Vec2 pivot{0.5f, 0.5f};
    // 앵커 스트레치 시: 부모 앵커 사각형에서 안쪽으로 미는 픽셀 마진.
    Vec2 offsetMin{0.0f, 0.0f};   // 좌·상 마진(또는 점앵커 시 위치)
    Vec2 offsetMax{0.0f, 0.0f};   // 우·하 마진(또는 점앵커 시 -크기 표현)
    // 점 앵커(anchorMin==anchorMax)일 때 고정 크기. 스트레치면 무시.
    Vec2 sizeDelta{0.0f, 0.0f};

    // 자주 쓰는 프리셋.
    static AnchorRect Fill() { return {{0,0},{1,1},{0.5f,0.5f},{0,0},{0,0},{0,0}}; }
    static AnchorRect TopLeft(Vec2 pos, Vec2 size) {
        return {{0,0},{0,0},{0,0}, pos, {0,0}, size};
    }
    static AnchorRect Center(Vec2 size) {
        return {{0.5f,0.5f},{0.5f,0.5f},{0.5f,0.5f},{0,0},{0,0}, size};
    }
};

// 9-slice 마진(픽셀). 스프라이트 에셋 메타에 저장, UI 는 읽기만(docs/06 §1).
struct NineSlice {
    int16_t left = 0, right = 0, top = 0, bottom = 0;
    bool    integerScale = false;   // 픽셀아트: 모서리 정수배 스케일만
    bool Enabled() const { return left || right || top || bottom; }
};

// 위젯 가시성·상호작용 플래그.
enum class Visibility : uint8_t { Visible, Hidden /*레이아웃 유지*/, Collapsed /*레이아웃 제외*/ };

// 스타일 클래스 키(UiSkin 조회) — 문자열의 FNV 해시.
struct StyleClassId {
    uint64_t v = 0;
    constexpr bool IsValid() const { return v != 0; }
    constexpr bool operator==(const StyleClassId&) const = default;
};
StyleClassId MakeStyleClass(std::string_view name);   // FNV-1a 64

} // namespace mye::ui
