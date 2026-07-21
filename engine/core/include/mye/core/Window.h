// mye/core/Window.h — 윈도우 추상·메시지 훅·윈도우 이벤트 (docs/01)
//
// WndProc은 메시지를 직접 처리하지 않고 이벤트로 변환해 버스에 발행한다.
// 선점 소비자(ImGui 07 / IME 06)는 IWindowMessageHook 체인(우선순위 순, handled 시 중단)을 사용.
// M0 구현 범위: 메인 윈도우 1개, Resize/CloseRequested/Focus/Dpi 이벤트. 창 모드 전환은 Phase 2(선언만).
#pragma once

#include "mye/core/Base.h"
#include "mye/core/Math.h"

#include <string>
#include <string_view>
#include <vector>

namespace mye {

struct WindowDesc {
    std::string title = "MyEngine";     // UTF-8
    Vec2i clientSize{1280, 720};
    bool  resizable = true;
    bool  borderless = false;
    bool  startMaximized = false;
};

enum class WindowMode : uint8_t { Windowed, BorderlessFullscreen };  // Exclusive는 비목표

struct DisplayInfo {
    Vec2i resolution;
    float refreshRate = 0.0f;
    Rect  workArea;
    bool  primary = false;
};

// ImGui(07)·IME(06)·플러그인이 win32 메시지를 선점하는 통로
class IWindowMessageHook {
public:
    virtual ~IWindowMessageHook() = default;
    // true 반환 = 메시지 소비 (이후 훅·기본 처리 중단)
    virtual bool OnMessage(void* hwnd, uint32_t msg, uint64_t wparam, int64_t lparam) = 0;
};

class IWindow {
public:
    virtual ~IWindow() = default;

    virtual void*  GetNativeHandle() const = 0;   // HWND — 02의 스왑체인 생성용
    virtual Vec2i  GetClientSize() const = 0;
    virtual float  GetDpiScale() const = 0;
    virtual void   SetTitle(std::string_view utf8) = 0;

    virtual WindowMode GetWindowMode() const = 0;
    virtual void   SetWindowMode(WindowMode mode, int displayIndex = -1) = 0;  // -1 = 현재 디스플레이 (구현 Phase 2)

    virtual void   AddMessageHook(IWindowMessageHook* hook, int priority) = 0; // priority 낮을수록 먼저
    virtual void   RemoveMessageHook(IWindowMessageHook* hook) = 0;
};

// 그래픽 설정 화면(06)이 소비. M0 스텁: 빈 목록.  구현: src/platform/Win32Window.cpp
std::vector<DisplayInfo> EnumerateDisplays();

// ---- 버스로 발행되는 윈도우 이벤트 ----
struct WindowResizedEvent       { MYE_EVENT(WindowResizedEvent);       IWindow* window; Vec2i clientSize; };
struct WindowCloseRequestedEvent{ MYE_EVENT(WindowCloseRequestedEvent);IWindow* window; };
struct WindowFocusEvent         { MYE_EVENT(WindowFocusEvent);         IWindow* window; bool focused; };
struct DpiChangedEvent          { MYE_EVENT(DpiChangedEvent);          IWindow* window; float dpiScale; };
struct WindowModeChangedEvent   { MYE_EVENT(WindowModeChangedEvent);   // 02가 구독: 스왑체인 재구성
                                  IWindow* window; WindowMode mode; Vec2i clientSize; };

// ---- 최소 키 입력 (M0 임시 — 추가 계약) ----
// WM_KEYDOWN/UP·WM_SYSKEYDOWN/UP의 win32 가상 키(VK_*)를 그대로 전달한다.
// M1에서 Raw Input 기반 RawKeyEvent(물리 스캔코드 KeyCode, docs/01)로 대체 예정 —
// 06 입력 매핑은 그쪽을 소비하고, 이 이벤트는 부트스트랩(ESC 종료 등) 용도로만 쓸 것.
struct KeyEvent { MYE_EVENT(KeyEvent); IWindow* window; uint32_t virtualKey; bool pressed; bool repeat; };

} // namespace mye
