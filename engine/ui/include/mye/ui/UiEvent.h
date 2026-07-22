// mye/ui/UiEvent.h — 라우티드 UI 이벤트 (docs/06 §1 이벤트 모델)
//
// 소유: 06 (M5-A). 히트 테스트로 최상단 위젯을 찾고 터널(capture)→타깃→버블 순으로 전파,
//   handled 플래그로 소비. 저수준 입력(core InputState/RawKeyEvent/TextInputEvent)을 UI 좌표
//   이벤트로 변환하는 것은 UiSystem 이 담당하고, 위젯은 UiEvent 만 본다.
#pragma once

#include "mye/core/Math.h"
#include "mye/core/Input.h"     // KeyCode, MouseButton

#include <cstdint>
#include <string_view>

namespace mye::ui {

class Widget;

enum class UiEventType : uint8_t {
    PointerEnter, PointerLeave,
    PointerDown, PointerUp, PointerClick,
    DragStart, Drag, Drop,
    FocusGained, FocusLost,
    KeyDown, KeyUp,
    TextInput,          // 확정 문자(UTF-8, IME 경유는 Phase 3)
    Scroll,
    TooltipRequest,
};

// 라우팅 단계(위젯이 자신이 어느 단계에서 호출됐는지 알 수 있게).
enum class UiRoutePhase : uint8_t { Tunnel, Target, Bubble };

// 드래그 페이로드(docs/06 §1). data 는 게임 정의(인벤토리 슬롯 등) — void* + 타입 문자열로 매칭.
//   Variant 는 05/04 확정 전까지 void* + typeName 로 둔다(계약 동결, 확장 여지).
struct DragPayload {
    std::string_view type;    // "item", "quickslot" 등 페이로드 타입
    void*            data = nullptr;
    // ghost 스프라이트 참조는 SpriteRef(asset) 확정 후 추가 — MVP 는 텍스처 핸들 옵션.
};

struct UiEvent {
    UiEventType  type;
    UiRoutePhase phase = UiRoutePhase::Target;
    bool         handled = false;    // true 로 세팅하면 전파 중단

    // 포인터 계열
    Vec2         pointerPos{};       // UI 픽셀 좌표(좌상단 원점)
    Vec2         pointerDelta{};     // Drag 델타
    MouseButton  button = MouseButton::Left;

    // 키/텍스트
    KeyCode      key = KeyCode::Unknown;
    char         utf8[8] = {};       // TextInput: 확정 문자(NUL 종단)

    // 스크롤
    float        scrollDelta = 0.0f;

    // 드래그
    DragPayload  payload{};

    // 이벤트를 최초로 히트한 타깃(터널/버블 중에도 유지).
    Widget*      target = nullptr;
};

} // namespace mye::ui
