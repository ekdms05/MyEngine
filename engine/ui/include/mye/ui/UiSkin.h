// mye/ui/UiSkin.h — 스킨/테마 에셋 (docs/06 §1 스킨/테마)
//
// 소유: 06 (M5-A). UiSkin = 스타일 클래스 이름 → { 스프라이트, 9-slice, 폰트, 색, 패딩, 사운드 큐 }.
//   위젯은 스프라이트를 직접 참조하는 대신 styleClass="window.frame" 로 스킨 키를 참조 → 스킨 교체로
//   전체 테마 변경. 04 리플렉션/직렬화로 에셋화(refl 등록은 UiDocument.cpp/UiSkin.cpp).
#pragma once

#include "mye/ui/UiTypes.h"
#include "mye/ui/UiDrawContext.h"     // UiSprite
#include "mye/text/TextTypes.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace mye::ui {

// 위젯 상태별(Normal/Hover/Pressed/Disabled) 스타일. Button 등이 상태로 조회.
enum class StyleState : uint8_t { Normal, Hover, Pressed, Disabled, Count };

// 한 스타일 클래스의 스타일 값 묶음.
struct UiStyle {
    UiSprite        sprite{};                  // 배경 스프라이트(상태별은 stateSprites)
    UiSprite        stateSprites[static_cast<size_t>(StyleState::Count)]{};
    bool            hasStateSprites = false;
    NineSlice       slice{};
    text::TextStyle text{};                    // 라벨 텍스트 스타일
    Color           tint = Color::White();
    Vec2            padding{};
    uint64_t        soundCueHash = 0;          // UI 사운드 큐(클릭 등) — audio 로 라우팅
};

// UiSkin 에셋. 스타일 클래스 키(FNV) → UiStyle.
class UiSkin {
public:
    // 스타일 조회. 미등록 시 nullptr → 위젯은 기본값 사용.
    const UiStyle* Find(StyleClassId cls) const;
    const UiStyle* Find(std::string_view clsName) const;

    void Set(std::string_view clsName, const UiStyle& style);

    // 폰트 등록소 참조(스타일의 text.font FontId 해석). UiSystem 이 주입.
    // (스킨 자체는 FontId 만 보유 — 실제 FontFace 소유는 FontRegistry)

private:
    std::unordered_map<uint64_t, UiStyle> m_styles;   // StyleClassId.v → UiStyle
};

} // namespace mye::ui
