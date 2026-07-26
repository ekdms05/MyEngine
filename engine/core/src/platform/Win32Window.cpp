// Win32Window.cpp — win32 창 구현 (docs/01 윈도우·메시지 펌프·저수준 입력)
// 규약: W 계열 API만 사용, 문자열은 경계에서 Widen/Narrow. 공개 헤더에 Windows.h 비노출(void* HWND).
// WndProc은 메시지를 직접 처리하지 않고 훅 체인(우선순위 순) → 이벤트 변환 → 버스 발행(Publish)한다.
#include "mye/core/platform/Win32Window.h"
#include "mye/core/Log.h"

#include <Windows.h>

#include <algorithm>
#include <format>

namespace mye {

std::vector<DisplayInfo> EnumerateDisplays() {
    // TODO(Phase 2): EnumDisplayMonitors — 그래픽 설정 화면(06)용
    return {};
}

} // namespace mye

namespace mye::win32 {

namespace {

constexpr wchar_t kWindowClassName[] = L"MyEngineWindowClass";

HWND ToHwnd(void* handle) { return static_cast<HWND>(handle); }

DWORD MakeWindowStyle(const WindowDesc& desc) {
    if (desc.borderless) return WS_POPUP;
    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!desc.resizable) style &= ~static_cast<DWORD>(WS_THICKFRAME | WS_MAXIMIZEBOX);
    return style;
}

} // namespace

// 자유 함수 WndProc 트램펄린 — GWLP_USERDATA에 연결된 Win32Window*로 위임 (헤더의 friend)
struct WndProcTrampoline {
    static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        if (msg == WM_NCCREATE) {
            const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
            auto* self = static_cast<Win32Window*>(cs->lpCreateParams);
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            if (self) self->m_hwnd = hwnd;   // CreateWindowExW 완료 전 도착하는 WM_SIZE 등을 위해 선기록
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        }

        auto* self = reinterpret_cast<Win32Window*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self == nullptr) return ::DefWindowProcW(hwnd, msg, wparam, lparam);

        bool handled = false;
        const int64_t result = self->HandleMessage(static_cast<uint32_t>(msg),
                                                   static_cast<uint64_t>(wparam),
                                                   static_cast<int64_t>(lparam), handled);
        return handled ? static_cast<LRESULT>(result) : ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }
};

namespace {

// 프로세스 1회 윈도우 클래스 등록. 실패 시 false + outError 세팅.
bool RegisterWindowClassOnce(Error& outError) {
    static bool s_registered = false;
    if (s_registered) return true;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &WndProcTrampoline::Proc;
    wc.hInstance     = ::GetModuleHandleW(nullptr);
    wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;   // 렌더러(02)가 전체를 그림 — 배경 지우기 플리커 방지
    wc.lpszClassName = kWindowClassName;

    if (::RegisterClassExW(&wc) == 0) {
        outError = Error{std::format("RegisterClassExW failed (GetLastError={})", ::GetLastError()),
                         static_cast<int32_t>(::GetLastError())};
        return false;
    }
    s_registered = true;
    return true;
}

} // namespace

Win32Window::Win32Window(EventBus& bus) : m_bus(bus) {}

Expected<std::unique_ptr<Win32Window>, Error> Win32Window::Create(const WindowDesc& desc, EventBus& bus) {
    Error registerError;
    if (!RegisterWindowClassOnce(registerError)) return registerError;

    auto window = std::unique_ptr<Win32Window>(new Win32Window(bus));
    window->m_clientSize = desc.clientSize;

    const DWORD style   = MakeWindowStyle(desc);
    const DWORD exStyle = 0;

    RECT rect{0, 0, static_cast<LONG>(desc.clientSize.x), static_cast<LONG>(desc.clientSize.y)};
    ::AdjustWindowRectEx(&rect, style, FALSE, exStyle);

    const std::wstring wideTitle = Widen(desc.title);
    HWND hwnd = ::CreateWindowExW(exStyle, kWindowClassName, wideTitle.c_str(), style,
                                  CW_USEDEFAULT, CW_USEDEFAULT,
                                  rect.right - rect.left, rect.bottom - rect.top,
                                  nullptr, nullptr, ::GetModuleHandleW(nullptr), window.get());
    if (hwnd == nullptr) {
        return Error{std::format("CreateWindowExW failed (GetLastError={})", ::GetLastError()),
                     static_cast<int32_t>(::GetLastError())};
    }
    window->m_hwnd = hwnd;   // WM_NCCREATE에서 선기록되지만 명시적으로 확정

    const UINT dpi = ::GetDpiForWindow(hwnd);
    window->m_dpiScale = dpi != 0 ? static_cast<float>(dpi) / 96.0f : 1.0f;

    RECT client{};
    ::GetClientRect(hwnd, &client);
    window->m_clientSize = Vec2i{static_cast<int32_t>(client.right - client.left),
                                 static_cast<int32_t>(client.bottom - client.top)};

    ::ShowWindow(hwnd, desc.startMaximized ? SW_SHOWMAXIMIZED : SW_SHOW);
    ::UpdateWindow(hwnd);

    MYE_LOG_INFO("Window", "win32 window created: client {}x{}, dpi scale {:.2f}",
                 window->m_clientSize.x, window->m_clientSize.y, window->m_dpiScale);
    return window;
}

Win32Window::~Win32Window() {
    if (m_hwnd != nullptr) {
        HWND hwnd = ToHwnd(m_hwnd);
        // 파괴 중 WM_DESTROY 등의 콜백이 죽어가는 this로 들어오지 않도록 연결 해제 후 파괴
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        ::DestroyWindow(hwnd);
        m_hwnd = nullptr;
    }
}

void Win32Window::PumpMessages() {
    MSG msg;
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) != 0) {
        if (msg.message == WM_QUIT) {   // 외부 PostQuitMessage 방어 — 우리는 WM_QUIT에 의존하지 않는다
            m_closeRequested = true;
            continue;
        }
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
}

int64_t Win32Window::HandleMessage(uint32_t msg, uint64_t wparam, int64_t lparam, bool& handled) {
    // 1) 훅 체인 우선 (priority 오름차순) — ImGui(07)/IME(06) 선점 통로. true = 소비.
    for (const auto& entry : m_hooks) {
        if (entry.hook->OnMessage(m_hwnd, msg, wparam, lparam)) {
            handled = true;
            return 0;
        }
    }

    // 2) 미소비 메시지 → 이벤트 변환 후 버스 발행. handled=false면 DefWindowProcW로 폴스루.
    handled = false;
    switch (msg) {
    case WM_CLOSE:
        m_closeRequested = true;
        m_bus.Publish(WindowCloseRequestedEvent{this});
        handled = true;   // DefWindowProc의 즉시 DestroyWindow 차단 — 종료 시점은 앱(루프)이 결정
        return 0;

    case WM_DESTROY:
        m_closeRequested = true;
        return 0;

    case WM_SIZE: {
        const Vec2i newSize{static_cast<int32_t>(LOWORD(lparam)), static_cast<int32_t>(HIWORD(lparam))};
        if (!(newSize == m_clientSize)) {
            m_clientSize = newSize;
            // 최소화(0x0)는 발행하지 않는다 — 스왑체인 소비자(02)가 0 크기 Resize를 받지 않게.
            // 복원 시 크기가 달라져 있으면 여기서 다시 발행된다 (최소화/복원 크래시 없음이 M0 기준).
            if (newSize.x > 0 && newSize.y > 0) {
                m_bus.Publish(WindowResizedEvent{this, newSize});
            }
        }
        return 0;
    }

    case WM_SETFOCUS:
        m_bus.Publish(WindowFocusEvent{this, true});
        return 0;
    case WM_KILLFOCUS:
        m_bus.Publish(WindowFocusEvent{this, false});
        return 0;

    case WM_DPICHANGED: {
        const UINT newDpi = HIWORD(wparam);
        m_dpiScale = newDpi != 0 ? static_cast<float>(newDpi) / 96.0f : 1.0f;
        m_bus.Publish(DpiChangedEvent{this, m_dpiScale});
        // OS 권장 위치·크기 적용 — 뒤따르는 WM_SIZE가 WindowResizedEvent를 발행한다
        if (const auto* suggested = reinterpret_cast<const RECT*>(lparam)) {
            ::SetWindowPos(ToHwnd(m_hwnd), nullptr, suggested->left, suggested->top,
                           suggested->right - suggested->left, suggested->bottom - suggested->top,
                           SWP_NOZORDER | SWP_NOACTIVATE);
        }
        handled = true;
        return 0;
    }

    // ---- 최소 키 입력 (M0 임시): 가상 키 그대로 발행. Raw Input 전환은 M1 (docs/01 Phase 1) ----
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        m_bus.Publish(KeyEvent{this, static_cast<uint32_t>(wparam), true,
                               (lparam & (int64_t{1} << 30)) != 0});
        return 0;   // handled=false — Alt+F4(WM_SYSKEYDOWN→WM_CLOSE) 등 기본 처리 유지

    case WM_KEYUP:
    case WM_SYSKEYUP:
        m_bus.Publish(KeyEvent{this, static_cast<uint32_t>(wparam), false, false});
        return 0;

    default:
        return 0;
    }
}

void Win32Window::SetTitle(std::string_view utf8) {
    if (m_hwnd != nullptr) {
        ::SetWindowTextW(ToHwnd(m_hwnd), Widen(utf8).c_str());
    }
}

void Win32Window::SetWindowMode(WindowMode mode, int /*displayIndex*/) {
    if (mode == m_mode) return;
    HWND hwnd = ToHwnd(m_hwnd);
    if (hwnd == nullptr) return;

    if (mode == WindowMode::BorderlessFullscreen) {
        // 현재 창 스타일·위치 저장(복귀용) 후, 모니터 전체를 덮는 테두리 없는 팝업으로 전환.
        m_savedStyle = static_cast<long long>(::GetWindowLongPtrW(hwnd, GWL_STYLE));
        RECT wr{};
        ::GetWindowRect(hwnd, &wr);
        m_savedRect[0] = wr.left;  m_savedRect[1] = wr.top;
        m_savedRect[2] = wr.right; m_savedRect[3] = wr.bottom;

        HMONITOR mon = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        ::GetMonitorInfoW(mon, &mi);

        ::SetWindowLongPtrW(hwnd, GWL_STYLE, static_cast<LONG_PTR>(WS_POPUP | WS_VISIBLE));
        ::SetWindowPos(hwnd, HWND_TOP,
                       mi.rcMonitor.left, mi.rcMonitor.top,
                       mi.rcMonitor.right - mi.rcMonitor.left,
                       mi.rcMonitor.bottom - mi.rcMonitor.top,
                       SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        // 저장한 창 스타일·위치로 복귀(저장값이 없으면 표준 창 스타일).
        const LONG_PTR style = m_savedStyle != 0
            ? static_cast<LONG_PTR>(m_savedStyle)
            : static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        ::SetWindowLongPtrW(hwnd, GWL_STYLE, style);
        const int w = m_savedRect[2] - m_savedRect[0];
        const int h = m_savedRect[3] - m_savedRect[1];
        ::SetWindowPos(hwnd, nullptr, m_savedRect[0], m_savedRect[1],
                       w > 0 ? w : 1280, h > 0 ? h : 720,
                       SWP_FRAMECHANGED | SWP_NOZORDER | SWP_SHOWWINDOW);
    }

    m_mode = mode;
    // 클라이언트 크기 갱신(SetWindowPos가 WM_SIZE→WindowResizedEvent를 이미 발행하지만, 즉시성 보장).
    RECT client{};
    ::GetClientRect(hwnd, &client);
    m_clientSize = Vec2i{static_cast<int32_t>(client.right - client.left),
                         static_cast<int32_t>(client.bottom - client.top)};
    m_bus.Publish(WindowModeChangedEvent{this, mode, m_clientSize});
    MYE_LOG_INFO("Window", "window mode -> {} ({}x{})",
                 mode == WindowMode::BorderlessFullscreen ? "borderless-fullscreen" : "windowed",
                 m_clientSize.x, m_clientSize.y);
}

void Win32Window::AddMessageHook(IWindowMessageHook* hook, int priority) {
    m_hooks.push_back({hook, priority});
    std::stable_sort(m_hooks.begin(), m_hooks.end(),
                     [](const auto& a, const auto& b) { return a.priority < b.priority; });
}

void Win32Window::RemoveMessageHook(IWindowMessageHook* hook) {
    std::erase_if(m_hooks, [&](const auto& e) { return e.hook == hook; });
}

} // namespace mye::win32
