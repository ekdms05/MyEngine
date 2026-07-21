// mye/core/platform/Win32Input.h — win32 Raw Input 백엔드 (docs/01 §저수준 입력)
//
// 책임(01): "무슨 물리 키가 눌렸다/마우스가 어디에 있다"까지. 입력 매핑·IME 조합은 06.
//   - Raw Input(WM_INPUT)으로 키보드/마우스를 받아 물리 스캔코드 KeyCode·고해상도 델타로 정규화.
//   - 물리 키 스트림(RawKeyEvent)과 문자 스트림(WM_CHAR→TextInputEvent, UTF-8)을 분리 발행.
//   - IME 관련 메시지는 해석하지 않고 ImeRawMessageEvent로 통로만 제공(06 소비).
//   - 동시에 InputState(폴링 스냅샷)를 갱신 → 즉시성이 필요한 소비자용.
//
// 배선: Win32Window의 메시지 훅 체인(IWindowMessageHook)에 우선순위로 등록된다.
//   WndProc이 훅을 먼저 태우므로 별도 WndProc 수정 없이 WM_INPUT/WM_CHAR/마우스 메시지를 가로챈다.
//   ImGui(07)가 입력을 선점하려면 이 백엔드보다 낮은(먼저 처리되는) priority로 훅을 등록하면 된다.
#pragma once

#include "mye/core/Input.h"
#include "mye/core/Window.h"

#include <memory>

namespace mye {
class EventBus;
}

namespace mye::win32 {

// win32 Raw Input → RawKeyEvent/RawMouseMoveEvent/... + InputState 갱신.
//
// 수명: window·bus·state 보다 짧아야 한다(참조 보관). 생성 시 Raw Input 장치를 등록하고
//       메시지 훅을 설치하며, 파괴 시 훅을 해제한다. GuardedMain이 소유한다.
class Win32InputBackend final : public IWindowMessageHook {
public:
    // window: HWND 소유 윈도우. bus: 저수준 입력 이벤트 발행처. state: 폴링 스냅샷(선택, nullptr 허용).
    // hookPriority: 훅 체인 우선순위(낮을수록 먼저). ImGui 훅보다 뒤(큰 값)에 두어 UI 선점을 허용.
    static Expected<std::unique_ptr<Win32InputBackend>, Error>
    Create(IWindow& window, EventBus& bus, InputState* state, int hookPriority = 100);

    ~Win32InputBackend() override;
    Win32InputBackend(const Win32InputBackend&) = delete;
    Win32InputBackend& operator=(const Win32InputBackend&) = delete;

    // IWindowMessageHook — true 반환 시 메시지 소비. 저수준 입력은 관찰만 하므로 대개 false(폴스루).
    bool OnMessage(void* hwnd, uint32_t msg, uint64_t wparam, int64_t lparam) override;

    // 폴링 스냅샷 대상 교체(모듈이 나중에 InputState를 등록하는 경우). nullptr로 분리 가능.
    void SetInputState(InputState* state) { m_state = state; }

private:
    Win32InputBackend(IWindow& window, EventBus& bus, InputState* state, int priority);

    IWindow&    m_window;
    EventBus&   m_bus;
    InputState* m_state = nullptr;
    int         m_priority = 100;
};

// ---- win32 스캔코드/가상키 → 물리 KeyCode 변환 (docs/01: USB HID 순서 준용) ----
// Raw Input의 MakeCode(세트1 스캔코드)와 E0/E1 플래그로 물리 위치를 판정한다.
// makeCode: RAWKEYBOARD::MakeCode, e0/e1: Flags의 RI_KEY_E0/E1, vkey: 폴백용 가상 키.
KeyCode ScanCodeToKeyCode(uint16_t makeCode, bool e0, bool e1, uint16_t vkey);

} // namespace mye::win32
