// mye/core/Input.h — 저수준 입력: KeyCode·입력 이벤트·InputState 폴링 (docs/01 §저수준 입력)
//
// 책임 경계(01): "무슨 물리 키가 눌렸다"까지. 입력 매핑(액션·축·리바인딩)·IME 조합은 06.
// 스트림 분리: 물리 키(RawKeyEvent, 스캔코드 KeyCode) vs 문자(TextInputEvent, UTF-8).
// InputState는 즉시성이 필요한 소비자용 폴링 스냅샷 — 매 프레임 PumpMessages 직후 1회 갱신.
#pragma once

#include "mye/core/Base.h"
#include "mye/core/Math.h"

#include <cstdint>

namespace mye {

// 물리 스캔코드 기반 키 코드 — USB HID Usage(Keyboard/Keypad Page 0x07) 순서 준용.
// 가상 키(VK_*)가 아니라 물리 위치이므로 레이아웃(QWERTY/AZERTY)과 무관하다.
// (M0의 KeyEvent는 win32 VK를 그대로 전달하는 임시 이벤트 — M1에서 이 KeyCode 기반
//  RawKeyEvent로 대체. 06 입력 매핑은 RawKeyEvent를 소비한다.)
enum class KeyCode : uint16_t {
    Unknown = 0,
    A = 4, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,          // 4..29 (HID)
    Num1 = 30, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9, Num0,   // 30..39
    Enter = 40, Escape, Backspace, Tab, Space,
    Minus, Equals, LeftBracket, RightBracket, Backslash,
    Semicolon = 51, Apostrophe, Grave, Comma, Period, Slash,
    CapsLock = 57,
    F1 = 58, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Right = 79, Left, Down, Up,                     // 화살표(HID 순서)
    LeftControl = 224, LeftShift, LeftAlt, LeftGui,
    RightControl, RightShift, RightAlt, RightGui,   // 228..231
    Count = 256,
};

enum class MouseButton : uint8_t { Left, Right, Middle, X1, X2, Count };

// ---- 버스로 발행되는 저수준 입력 이벤트 (06·게임 코드 구독) ----
struct RawKeyEvent       { MYE_EVENT(RawKeyEvent);       KeyCode key; bool pressed; bool repeat; };
struct RawMouseMoveEvent { MYE_EVENT(RawMouseMoveEvent); Vec2 delta;      // Raw Input 고해상도 델타
                                                         Vec2i position; }; // 클라이언트 좌표
struct MouseButtonEvent  { MYE_EVENT(MouseButtonEvent);  MouseButton button; bool pressed; Vec2i position; };
struct MouseWheelEvent   { MYE_EVENT(MouseWheelEvent);   float deltaY; float deltaX; };
struct TextInputEvent    { MYE_EVENT(TextInputEvent);    char utf8[8]; };  // 확정 문자(코드포인트 1개, NUL 종단)

// IME는 해석하지 않고 06으로 그대로 전달(통로만).
struct ImeRawMessageEvent { MYE_EVENT(ImeRawMessageEvent);
                            void* hwnd; uint32_t msg; uint64_t wparam; int64_t lparam; };

// 즉시 상태 조회(폴링) — 프레임 경계에서 스냅샷 갱신. WasPressed/WasReleased는
// "이번 프레임에 상태가 바뀌었나"(엣지) 질의. 갱신 지점: 메인 루프가 PumpMessages
// 직후 NewFrame()을 1회 호출해 이전 프레임 상태를 롤오버한다.
class InputState {
public:
    MYE_SERVICE(InputState);

    // ---- 폴링 API ----
    // 엣지 질의 계약: WasPressed/WasReleased는 NewFrame()~다음 NewFrame() 사이(=한 렌더 프레임)에
    // 발생한 상태 전이를 관측한다. 따라서 프레임당 정확히 1회 실행되는 페이즈(PreUpdate/Update/
    // PostUpdate)에서 소비해야 한다. FixedUpdate는 한 프레임에 0회(대기)·1회·2회 이상 실행될 수
    // 있어(고정스텝 누산기) 같은 엣지를 중복 관측하거나(다중 실행) 유실할 수 있다(0회 실행 시 다음
    // NewFrame이 previous로 덮음). 지속 입력(IsDown)은 어느 페이즈에서든 안전하다.
    bool IsDown(KeyCode key) const;         // 현재 눌림
    bool WasPressed(KeyCode key) const;     // 이번 프레임에 down 엣지
    bool WasReleased(KeyCode key) const;    // 이번 프레임에 up 엣지

    bool IsDown(MouseButton btn) const;
    bool WasPressed(MouseButton btn) const;
    bool WasReleased(MouseButton btn) const;

    Vec2i MousePosition() const;            // 클라이언트 좌표
    Vec2  MouseDelta() const;               // 이번 프레임 누적 Raw 델타
    float WheelDelta() const;               // 이번 프레임 누적 휠(수직)

    // ---- 갱신(엔진 루프·입력 백엔드 전용) ----
    // 프레임 경계: 이전 프레임 상태 저장 + 엣지/델타 리셋. PumpMessages 직후 1회.
    void NewFrame();
    // 입력 백엔드(win32 Raw Input)가 이벤트 처리 중 호출해 현재 상태를 밀어넣는다.
    void OnKey(KeyCode key, bool pressed);
    void OnMouseButton(MouseButton btn, bool pressed);
    void OnMouseMove(Vec2i position, Vec2 rawDelta);
    void OnWheel(float deltaY);

    // ---- 입력 선점(UI 캡처) ----
    // ImGui 등 오버레이 UI가 키보드/마우스를 캡처 중일 때 게임 입력 상태 갱신을 억제한다.
    // mye_core는 imgui에 의존하지 않으므로, 캡처 소유자(DebugUi)가 프레임마다 이 플래그를
    // 주입한다. Raw Input 백엔드(WM_INPUT)와 레거시 메시지 양쪽이 이 플래그를 게이팅에 쓴다.
    // 억제가 걸리면 해당 장치의 눌림 상태를 released로 강제해 stuck 키를 방지한다.
    void SetKeyboardSuppressed(bool suppressed);
    void SetMouseSuppressed(bool suppressed);
    bool IsKeyboardSuppressed() const { return m_keyboardSuppressed; }
    bool IsMouseSuppressed() const { return m_mouseSuppressed; }

private:
    struct Snapshot {
        bool  keys[static_cast<size_t>(KeyCode::Count)] = {};
        bool  mouseButtons[static_cast<size_t>(MouseButton::Count)] = {};
    };
    Snapshot m_current{};
    Snapshot m_previous{};
    Vec2i    m_mousePos{};
    Vec2     m_mouseDelta{};
    float    m_wheelDelta = 0.0f;
    bool     m_keyboardSuppressed = false;   // UI 캡처 중 게임 키보드 입력 억제
    bool     m_mouseSuppressed = false;      // UI 캡처 중 게임 마우스 입력 억제
};

} // namespace mye
