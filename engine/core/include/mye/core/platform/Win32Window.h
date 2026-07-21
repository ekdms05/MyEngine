// mye/core/platform/Win32Window.h — win32 구현 (docs/01, namespace mye::win32)
//
// 공개 헤더에 <Windows.h>를 노출하지 않는다 — HWND는 void*로 보관.
// WndProc: 훅 체인(우선순위 순) → 미소비 메시지는 이벤트 변환 후 버스 발행(Publish).
#pragma once

#include "mye/core/Window.h"
#include "mye/core/Events.h"

#include <memory>

namespace mye::win32 {

class Win32Window final : public IWindow {
public:
    // 윈도우 클래스 등록 + CreateWindowExW. bus는 이벤트 발행처(수명: 윈도우보다 길어야 함).
    static Expected<std::unique_ptr<Win32Window>, Error> Create(const WindowDesc& desc, EventBus& bus);

    ~Win32Window() override;
    Win32Window(const Win32Window&) = delete;
    Win32Window& operator=(const Win32Window&) = delete;

    // 메인 루프가 매 프레임 호출: PeekMessage 드레인 → 훅 체인 → 이벤트 발행
    void PumpMessages();

    // WM_CLOSE 수신 여부 (GuardedMain 루프 종료 판단 보조 — 이벤트 구독과 병행 제공)
    bool IsCloseRequested() const { return m_closeRequested; }

    // ---- IWindow ----
    void*  GetNativeHandle() const override { return m_hwnd; }
    Vec2i  GetClientSize() const override { return m_clientSize; }
    float  GetDpiScale() const override { return m_dpiScale; }
    void   SetTitle(std::string_view utf8) override;
    WindowMode GetWindowMode() const override { return m_mode; }
    void   SetWindowMode(WindowMode mode, int displayIndex = -1) override;  // Phase 2 — M0 스텁
    void   AddMessageHook(IWindowMessageHook* hook, int priority) override;
    void   RemoveMessageHook(IWindowMessageHook* hook) override;

private:
    explicit Win32Window(EventBus& bus);

    // 실제 WndProc 처리 (자유 함수 WndProc 트램펄린이 호출). 반환: 메시지 처리 여부와 결과.
    int64_t HandleMessage(uint32_t msg, uint64_t wparam, int64_t lparam, bool& handled);
    friend struct WndProcTrampoline;

    EventBus&  m_bus;
    void*      m_hwnd = nullptr;          // HWND
    Vec2i      m_clientSize{};
    float      m_dpiScale = 1.0f;
    WindowMode m_mode = WindowMode::Windowed;
    bool       m_closeRequested = false;

    struct HookEntry { IWindowMessageHook* hook; int priority; };
    std::vector<HookEntry> m_hooks;       // priority 오름차순 유지
};

} // namespace mye::win32
