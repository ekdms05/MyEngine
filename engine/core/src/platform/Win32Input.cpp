// Win32Input.cpp — win32 Raw Input 백엔드 구현 (docs/01 §저수준 입력)
//
// 규약: W 계열 API만, 문자열 경계에서 Narrow. 공개 헤더에 <Windows.h> 비노출.
// Raw Input(WM_INPUT): 키보드 물리 스캔코드 + 마우스 고해상도 델타/버튼/휠.
// WM_CHAR: 확정 문자 → UTF-8 TextInputEvent. WM_IME_*: 통로만(ImeRawMessageEvent).
// 클라이언트 좌표 마우스 위치는 WM_MOUSEMOVE(또는 raw 시 GetCursorPos+ScreenToClient)로 추적.
#include "mye/core/platform/Win32Input.h"
#include "mye/core/Events.h"
#include "mye/core/Log.h"

#include <Windows.h>
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM
#include <imm.h>

#include <array>
#include <format>
#include <vector>
#include <cmath>

namespace mye::win32 {

namespace {

HWND ToHwnd(void* handle) { return static_cast<HWND>(handle); }

// ---- 세트1(PS/2) 스캔코드 → HID 기반 KeyCode ----
// Raw Input의 MakeCode는 세트1 스캔코드. E0 프리픽스는 확장 키(오른쪽 Ctrl/Alt, 화살표 등) 구분.
// 표는 픽셀게임 입력(WASD·화살표·수정자·F키·수/기호)에 필요한 범위를 덮는다. 누락 키는 vkey 폴백.
KeyCode MapNonExtended(uint16_t make) {
    switch (make) {
    // 문자 행 (QWERTY 물리 위치 — 레이아웃 무관)
    case 0x1E: return KeyCode::A; case 0x30: return KeyCode::B; case 0x2E: return KeyCode::C;
    case 0x20: return KeyCode::D; case 0x12: return KeyCode::E; case 0x21: return KeyCode::F;
    case 0x22: return KeyCode::G; case 0x23: return KeyCode::H; case 0x17: return KeyCode::I;
    case 0x24: return KeyCode::J; case 0x25: return KeyCode::K; case 0x26: return KeyCode::L;
    case 0x32: return KeyCode::M; case 0x31: return KeyCode::N; case 0x18: return KeyCode::O;
    case 0x19: return KeyCode::P; case 0x10: return KeyCode::Q; case 0x13: return KeyCode::R;
    case 0x1F: return KeyCode::S; case 0x14: return KeyCode::T; case 0x16: return KeyCode::U;
    case 0x2F: return KeyCode::V; case 0x11: return KeyCode::W; case 0x2D: return KeyCode::X;
    case 0x15: return KeyCode::Y; case 0x2C: return KeyCode::Z;
    // 숫자 행
    case 0x02: return KeyCode::Num1; case 0x03: return KeyCode::Num2; case 0x04: return KeyCode::Num3;
    case 0x05: return KeyCode::Num4; case 0x06: return KeyCode::Num5; case 0x07: return KeyCode::Num6;
    case 0x08: return KeyCode::Num7; case 0x09: return KeyCode::Num8; case 0x0A: return KeyCode::Num9;
    case 0x0B: return KeyCode::Num0;
    // 편집·공백·기호
    case 0x1C: return KeyCode::Enter;      case 0x01: return KeyCode::Escape;
    case 0x0E: return KeyCode::Backspace;  case 0x0F: return KeyCode::Tab;
    case 0x39: return KeyCode::Space;      case 0x0C: return KeyCode::Minus;
    case 0x0D: return KeyCode::Equals;     case 0x1A: return KeyCode::LeftBracket;
    case 0x1B: return KeyCode::RightBracket; case 0x2B: return KeyCode::Backslash;
    case 0x27: return KeyCode::Semicolon;  case 0x28: return KeyCode::Apostrophe;
    case 0x29: return KeyCode::Grave;      case 0x33: return KeyCode::Comma;
    case 0x34: return KeyCode::Period;     case 0x35: return KeyCode::Slash;
    case 0x3A: return KeyCode::CapsLock;
    // 수정자 (왼쪽)
    case 0x1D: return KeyCode::LeftControl; case 0x2A: return KeyCode::LeftShift;
    case 0x38: return KeyCode::LeftAlt;     case 0x36: return KeyCode::RightShift;
    // 기능 키
    case 0x3B: return KeyCode::F1; case 0x3C: return KeyCode::F2; case 0x3D: return KeyCode::F3;
    case 0x3E: return KeyCode::F4; case 0x3F: return KeyCode::F5; case 0x40: return KeyCode::F6;
    case 0x41: return KeyCode::F7; case 0x42: return KeyCode::F8; case 0x43: return KeyCode::F9;
    case 0x44: return KeyCode::F10; case 0x57: return KeyCode::F11; case 0x58: return KeyCode::F12;
    default: return KeyCode::Unknown;
    }
}

// E0 프리픽스 확장 키(오른쪽 수정자·화살표·NumpadEnter 등)
KeyCode MapExtended(uint16_t make) {
    switch (make) {
    case 0x1D: return KeyCode::RightControl;  // E0 1D
    case 0x38: return KeyCode::RightAlt;       // E0 38 (AltGr)
    case 0x5B: return KeyCode::LeftGui;        // E0 5B (왼쪽 Win)
    case 0x5C: return KeyCode::RightGui;       // E0 5C (오른쪽 Win)
    case 0x48: return KeyCode::Up;             // E0 48
    case 0x50: return KeyCode::Down;           // E0 50
    case 0x4B: return KeyCode::Left;           // E0 4B
    case 0x4D: return KeyCode::Right;          // E0 4D
    case 0x47: return KeyCode::Home;
    case 0x4F: return KeyCode::End;
    case 0x53: return KeyCode::Delete;
    case 0x1C: return KeyCode::Enter;          // E0 1C (Numpad Enter → Enter로 통합)
    default: return KeyCode::Unknown;
    }
}

// 가상 키 폴백 (스캔코드가 0이거나 표에 없을 때 최후 수단 — 레이아웃 의존이라 물리 키만 매핑).
KeyCode MapVirtualKeyFallback(uint16_t vkey) {
    if (vkey >= 'A' && vkey <= 'Z') return static_cast<KeyCode>(
        static_cast<uint16_t>(KeyCode::A) + (vkey - 'A'));
    switch (vkey) {
    case VK_LEFT:  return KeyCode::Left;   case VK_RIGHT: return KeyCode::Right;
    case VK_UP:    return KeyCode::Up;     case VK_DOWN:  return KeyCode::Down;
    case VK_SPACE: return KeyCode::Space;  case VK_RETURN: return KeyCode::Enter;
    case VK_ESCAPE: return KeyCode::Escape;
    case VK_HOME: return KeyCode::Home; case VK_END: return KeyCode::End; case VK_DELETE: return KeyCode::Delete;
    case VK_LSHIFT: return KeyCode::LeftShift; case VK_RSHIFT: return KeyCode::RightShift;
    case VK_LCONTROL: return KeyCode::LeftControl; case VK_RCONTROL: return KeyCode::RightControl;
    default: return KeyCode::Unknown;
    }
}

// UTF-16 코드유닛(WM_CHAR)을 UTF-8로 인코딩. 서로게이트 상위/하위를 조합해 코드포인트 완성.
// 반환: TextInputEvent(NUL 종단 utf8[8]) — 완성된 코드포인트가 없으면(상위 서로게이트 대기) false.
bool EncodeWmChar(wchar_t codeUnit, wchar_t& pendingHighSurrogate, TextInputEvent& out) {
    wchar_t buffer[2];
    int count = 0;
    if (codeUnit >= 0xD800 && codeUnit <= 0xDBFF) {   // 상위 서로게이트 — 다음 유닛 대기
        pendingHighSurrogate = codeUnit;
        return false;
    }
    if (codeUnit >= 0xDC00 && codeUnit <= 0xDFFF) {    // 하위 서로게이트 — 짝 완성
        if (pendingHighSurrogate == 0) return false;   // 짝 없는 하위 → 폐기
        buffer[0] = pendingHighSurrogate;
        buffer[1] = codeUnit;
        count = 2;
        pendingHighSurrogate = 0;
    } else {
        buffer[0] = codeUnit;
        count = 1;
        pendingHighSurrogate = 0;
    }

    char utf8[8] = {};
    const int bytes = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, buffer, count, utf8, sizeof(utf8) - 1,
                                            nullptr, nullptr);
    if (bytes <= 0) return false;
    out = TextInputEvent{};
    for (int i = 0; i < bytes && i < 7; ++i) out.utf8[i] = utf8[i];
    return true;
}

} // namespace

namespace {
struct ImeContext {
    HWND window;
    HIMC value;
    explicit ImeContext(void* handle) : window(ToHwnd(handle)), value(ImmGetContext(window)) {}
    ~ImeContext() { if (value) ImmReleaseContext(window, value); }
};
Expected<std::string, Error> CompositionUtf8(std::wstring_view text) {
    if (text.empty()) return std::string{};
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0 || length > 4096) return Error{"IME text is invalid UTF-16 or exceeds 4096 UTF-8 bytes", 1};
    std::string result(static_cast<size_t>(length), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
        result.data(), length, nullptr, nullptr) != length) return Error{"IME UTF-8 conversion failed", 1};
    return result;
}
Expected<std::wstring, Error> ReadComposition(HIMC context, DWORD kind) {
    const auto bytes = ImmGetCompositionStringW(context, kind, nullptr, 0);
    if (bytes < 0 || bytes > 8192 || bytes % sizeof(wchar_t) != 0) return Error{"IME composition size is invalid or unavailable", 1};
    std::wstring result(static_cast<size_t>(bytes) / sizeof(wchar_t), L'\0');
    if (bytes && ImmGetCompositionStringW(context, kind, result.data(), bytes) != bytes)
        return Error{"IME composition read failed", 1};
    return result;
}
}

bool HandleTextInputMessage(InputState* state, TextMessageState& messages, void* handle,
    uint32_t msg, uint64_t wparam, int64_t lparam, EventBus* events) {
    const auto id=state ? state->TextFocusId() : 0;
    if (messages.focusId != id) { messages.pendingHighSurrogate=0; messages.focusId=id; }
    auto& pending=messages.pendingHighSurrogate;
    if (msg == WM_KILLFOCUS) { pending = 0; if (state) state->SetTextInputFocus(0); }
    const bool editing = state && state->TextFocusId() && !state->IsKeyboardSuppressed();
    auto accept = [&](TextEdit edit) {
        auto result = state->OnTextEdit(std::move(edit));
        if (!result) state->ReportTextInputError(result.GetError());
    };
    if (msg == WM_CHAR) {
        if (wparam > 0xffff) { if (editing) state->ReportTextInputError(Error{"WM_CHAR requires a UTF-16 code unit",1}); return false; }
        if (editing && pending && (wparam < 0xdc00 || wparam > 0xdfff))
            state->ReportTextInputError(Error{"Text input contains unpaired UTF-16",1});
        TextInputEvent event{};
        if (EncodeWmChar(static_cast<wchar_t>(wparam), pending, event)) {
            if (events) events->Publish(event);
            if (editing && !state->IsTextComposing() && static_cast<unsigned char>(event.utf8[0]) >= 32 && event.utf8[0] != 127)
                accept(TextEdit{TextEdit::Kind::Insert, event.utf8});
        } else if (editing && !pending) state->ReportTextInputError(Error{"Text input contains unpaired UTF-16", 1});
        return false;
    }
    if (!editing) return false;
    if (msg == WM_IME_SETCONTEXT) {
        DefWindowProcW(ToHwnd(handle), msg, wparam, lparam & ~ISC_SHOWUICOMPOSITIONWINDOW);
        return true;
    }
    if (msg == WM_IME_STARTCOMPOSITION || msg == WM_IME_ENDCOMPOSITION) {
        TextEdit edit; edit.kind = TextEdit::Kind::Composition; edit.composing = msg == WM_IME_STARTCOMPOSITION;
        accept(std::move(edit)); return true;
    }
    if (msg == WM_IME_CHAR) return true; // GCS_RESULTSTR owns committed text; never insert it twice.
    if (msg != WM_IME_COMPOSITION) return false;
    ImeContext context(handle);
    if (!context.value) { state->ReportTextInputError(Error{"IME context is unavailable", 1}); return true; }
    if (lparam & GCS_RESULTSTR) {
        auto wide = ReadComposition(context.value, GCS_RESULTSTR);
        auto value = wide ? CompositionUtf8(wide.Value()) : Expected<std::string, Error>{wide.GetError()};
        if (!value) state->ReportTextInputError(value.GetError());
        else accept(TextEdit{TextEdit::Kind::Insert, std::move(value).Value()});
    }
    TextEdit edit; edit.kind = TextEdit::Kind::Composition;
    if (lparam & (GCS_COMPSTR | GCS_CURSORPOS)) {
        auto wide = ReadComposition(context.value, GCS_COMPSTR);
        if (!wide) { state->ReportTextInputError(wide.GetError()); return true; }
        auto value = CompositionUtf8(wide.Value());
        if (!value) { state->ReportTextInputError(value.GetError()); return true; }
        LONG position = static_cast<LONG>(wide.Value().size());
        if (lparam & GCS_CURSORPOS) position = ImmGetCompositionStringW(context.value, GCS_CURSORPOS, nullptr, 0);
        if (position < 0 || static_cast<size_t>(position) > wide.Value().size()) {
            state->ReportTextInputError(Error{"IME cursor is outside composition text", 1}); return true;
        }
        auto prefix = CompositionUtf8(std::wstring_view{wide.Value()}.substr(0, static_cast<size_t>(position)));
        if (!prefix) { state->ReportTextInputError(prefix.GetError()); return true; }
        edit.text = std::move(value).Value(); edit.cursorBytes = static_cast<uint32_t>(prefix.Value().size()); edit.composing = true;
    } else if (!(lparam & GCS_RESULTSTR) && lparam != 0) return true;
    accept(std::move(edit));
    return true;
}

Expected<void, Error> ConfigureTextInput(InputState& state, void* handle, std::optional<TextInputFocus> focus) {
    if (focus && (!focus->id || !std::isfinite(focus->caret.x) || !std::isfinite(focus->caret.y) || !std::isfinite(focus->caret.w) ||
        !std::isfinite(focus->caret.h) || std::abs(focus->caret.x) > 1048576 || std::abs(focus->caret.y) > 1048576 ||
        focus->caret.w < 0 || focus->caret.h < 0 || focus->caret.w > 1048576 || focus->caret.h > 1048576))
        return Error{"IME caret rectangle is invalid", 1};
    ImeContext context(handle);
    const auto id = focus ? focus->id : 0;
    if (id != state.TextFocusId() && state.IsTextComposing() &&
        (!context.value || !ImmNotifyIME(context.value, NI_COMPOSITIONSTR, CPS_CANCEL, 0)))
        return Error{"IME composition cancellation failed", 1};
    state.SetTextInputFocus(id);
    if (!focus || !context.value) return {};
    const auto& r = focus->caret;
    const POINT position{static_cast<LONG>(std::round(r.x)), static_cast<LONG>(std::round(r.y))};
    COMPOSITIONFORM composition{}; composition.dwStyle = CFS_POINT; composition.ptCurrentPos = position;
    CANDIDATEFORM candidate{}; candidate.dwStyle = CFS_EXCLUDE;
    candidate.ptCurrentPos = {position.x, static_cast<LONG>(std::round(r.y+r.h))};
    candidate.rcArea = {position.x,position.y,static_cast<LONG>(std::round(r.x+r.w)),candidate.ptCurrentPos.y};
    if (!ImmSetCompositionWindow(context.value, &composition) || !ImmSetCandidateWindow(context.value, &candidate))
        return Error{"IME candidate/composition positioning failed", 1};
    return {};
}

KeyCode ScanCodeToKeyCode(uint16_t makeCode, bool e0, bool e1, uint16_t vkey) {
    (void)e1;   // E1(Pause 등)은 현재 매핑 대상 아님 — 폴백 처리
    if (makeCode != 0) {
        const KeyCode mapped = e0 ? MapExtended(makeCode) : MapNonExtended(makeCode);
        if (mapped != KeyCode::Unknown) return mapped;
    }
    return MapVirtualKeyFallback(vkey);
}

// ---------------------------------------------------------------------------
// Win32InputBackend
// ---------------------------------------------------------------------------
Win32InputBackend::Win32InputBackend(IWindow& window, EventBus& bus, InputState* state, int priority)
    : m_window(window), m_bus(bus), m_state(state), m_priority(priority) {}

Expected<std::unique_ptr<Win32InputBackend>, Error>
Win32InputBackend::Create(IWindow& window, EventBus& bus, InputState* state, int hookPriority) {
    HWND hwnd = ToHwnd(window.GetNativeHandle());
    if (hwnd == nullptr) {
        return Error{"Win32InputBackend: 윈도우 네이티브 핸들이 없음(HWND null)"};
    }

    // Raw Input 장치 등록: Generic Desktop 페이지(0x01)의 키보드(0x06)·마우스(0x02).
    // RIDEV_INPUTSINK 미사용 → 포그라운드일 때만 수신(백그라운드 입력 불필요).
    RAWINPUTDEVICE rid[2] = {};
    rid[0].usUsagePage = 0x01; rid[0].usUsage = 0x06;   // 키보드
    rid[0].dwFlags = 0; rid[0].hwndTarget = hwnd;
    rid[1].usUsagePage = 0x01; rid[1].usUsage = 0x02;   // 마우스
    rid[1].dwFlags = 0; rid[1].hwndTarget = hwnd;

    if (::RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE)) == FALSE) {
        return Error{std::format("RegisterRawInputDevices 실패 (GetLastError={})", ::GetLastError()),
                     static_cast<int32_t>(::GetLastError())};
    }

    auto backend = std::unique_ptr<Win32InputBackend>(
        new Win32InputBackend(window, bus, state, hookPriority));
    window.AddMessageHook(backend.get(), hookPriority);

    MYE_LOG_INFO("Input", "win32 Raw Input 백엔드 등록 완료 (hook priority {})", hookPriority);
    return backend;
}

Win32InputBackend::~Win32InputBackend() {
    m_window.RemoveMessageHook(this);
    // Raw Input 장치 해제(RIDEV_REMOVE) — hwndTarget은 null이어야 함.
    RAWINPUTDEVICE rid[2] = {};
    rid[0].usUsagePage = 0x01; rid[0].usUsage = 0x06; rid[0].dwFlags = RIDEV_REMOVE;
    rid[1].usUsagePage = 0x01; rid[1].usUsage = 0x02; rid[1].dwFlags = RIDEV_REMOVE;
    ::RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
}

bool Win32InputBackend::OnMessage(void* hwnd, uint32_t msg, uint64_t wparam, int64_t lparam) {
    const bool textHandled = HandleTextInputMessage(m_state, m_textMessages, hwnd, msg, wparam, lparam, &m_bus);

    // 입력 선점 주의: ImGui WndProc 훅은 WM_KEYDOWN/CHAR/마우스 레거시 메시지만 소비하며
    // WM_INPUT(Raw Input)은 소비하지 않는다. 따라서 UI 캡처 중 게임 입력 선점은 여기서 훅으로
    // 막히지 않는다. 선점은 InputState 내부의 억제 플래그(SetKeyboard/MouseSuppressed)가
    // 담당한다 — OnKey/OnMouse* 호출이 억제 시 no-op이 되므로 아래 갱신은 그대로 두어도 안전.
    // (DebugUi가 프레임마다 WantCaptureKeyboard/Mouse를 InputState에 주입한다.)
    switch (msg) {
    case WM_KILLFOCUS:
        if (m_state) { m_state->SetKeyboardSuppressed(true); m_state->SetMouseSuppressed(true); }
        break;
    case WM_SETFOCUS:
        if (m_state) { m_state->SetKeyboardSuppressed(false); m_state->SetMouseSuppressed(false); }
        break;
    case WM_INPUT: {
        UINT size = 0;
        ::GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, nullptr, &size,
                          sizeof(RAWINPUTHEADER));
        if (size == 0) return false;

        // 스택 버퍼로 충분(키보드/마우스 RAWINPUT은 작음) — 초과 시 힙 폴백.
        std::array<std::byte, 128> stackBuf{};
        std::vector<std::byte> heapBuf;
        void* dataPtr = stackBuf.data();
        if (size > stackBuf.size()) { heapBuf.resize(size); dataPtr = heapBuf.data(); }

        if (::GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, dataPtr, &size,
                              sizeof(RAWINPUTHEADER)) != size) {
            return false;
        }
        const auto* raw = reinterpret_cast<const RAWINPUT*>(dataPtr);

        if (raw->header.dwType == RIM_TYPEKEYBOARD) {
            const RAWKEYBOARD& kb = raw->data.keyboard;
            // 일부 장치가 보내는 오버런/미정의 스캔코드 무시.
            if (kb.MakeCode == 0 && kb.VKey == 0) break;
            if (kb.VKey == 0xFF) break;   // 가짜 키(마이크로소프트 키보드 확장 프리픽스)

            const bool e0 = (kb.Flags & RI_KEY_E0) != 0;
            const bool e1 = (kb.Flags & RI_KEY_E1) != 0;
            const bool pressed = (kb.Flags & RI_KEY_BREAK) == 0;   // BREAK=up
            const KeyCode key = ScanCodeToKeyCode(kb.MakeCode, e0, e1, static_cast<uint16_t>(kb.VKey));
            if (key == KeyCode::Unknown) break;

            // Raw Input은 하드웨어 반복 없음 → 반복은 InputState의 이전 상태로 판정.
            const bool wasDown = (m_state != nullptr) && m_state->IsDown(key);
            const bool repeat = pressed && wasDown;

            if (m_state != nullptr) m_state->OnKey(key, pressed);
            m_bus.Publish(RawKeyEvent{.key = key, .pressed = pressed, .repeat = repeat});
        } else if (raw->header.dwType == RIM_TYPEMOUSE) {
            const RAWMOUSE& ms = raw->data.mouse;

            // 고해상도 상대 델타(대부분의 마우스는 MOUSE_MOVE_RELATIVE).
            if ((ms.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
                const Vec2 delta{static_cast<float>(ms.lLastX), static_cast<float>(ms.lLastY)};
                if (delta.x != 0.0f || delta.y != 0.0f) {
                    // 클라이언트 좌표 위치는 커서 위치에서 계산(raw는 상대값만 줌).
                    POINT pt{};
                    ::GetCursorPos(&pt);
                    ::ScreenToClient(ToHwnd(hwnd), &pt);
                    const Vec2i pos{static_cast<int32_t>(pt.x), static_cast<int32_t>(pt.y)};
                    if (m_state != nullptr) m_state->OnMouseMove(pos, delta);
                    m_bus.Publish(RawMouseMoveEvent{.delta = delta, .position = pos});
                }
            }

            // 버튼 전이 플래그 → MouseButtonEvent.
            const auto handleButton = [&](USHORT downFlag, USHORT upFlag, MouseButton btn) {
                POINT pt{}; ::GetCursorPos(&pt); ::ScreenToClient(ToHwnd(hwnd), &pt);
                const Vec2i pos{static_cast<int32_t>(pt.x), static_cast<int32_t>(pt.y)};
                if (ms.usButtonFlags & downFlag) {
                    if (m_state != nullptr) m_state->OnMouseButton(btn, true);
                    m_bus.Publish(MouseButtonEvent{.button = btn, .pressed = true, .position = pos});
                }
                if (ms.usButtonFlags & upFlag) {
                    if (m_state != nullptr) m_state->OnMouseButton(btn, false);
                    m_bus.Publish(MouseButtonEvent{.button = btn, .pressed = false, .position = pos});
                }
            };
            handleButton(RI_MOUSE_LEFT_BUTTON_DOWN,   RI_MOUSE_LEFT_BUTTON_UP,   MouseButton::Left);
            handleButton(RI_MOUSE_RIGHT_BUTTON_DOWN,  RI_MOUSE_RIGHT_BUTTON_UP,  MouseButton::Right);
            handleButton(RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP, MouseButton::Middle);
            handleButton(RI_MOUSE_BUTTON_4_DOWN,      RI_MOUSE_BUTTON_4_UP,      MouseButton::X1);
            handleButton(RI_MOUSE_BUTTON_5_DOWN,      RI_MOUSE_BUTTON_5_UP,      MouseButton::X2);

            if (ms.usButtonFlags & RI_MOUSE_WHEEL) {
                const float delta = static_cast<float>(static_cast<SHORT>(ms.usButtonData)) /
                                    static_cast<float>(WHEEL_DELTA);
                if (m_state != nullptr) m_state->OnWheel(delta);
                m_bus.Publish(MouseWheelEvent{.deltaY = delta, .deltaX = 0.0f});
            }
            if (ms.usButtonFlags & RI_MOUSE_HWHEEL) {
                const float delta = static_cast<float>(static_cast<SHORT>(ms.usButtonData)) /
                                    static_cast<float>(WHEEL_DELTA);
                m_bus.Publish(MouseWheelEvent{.deltaY = 0.0f, .deltaX = delta});
            }
        }
        return false;   // WM_INPUT은 DefWindowProc(정리)로 폴스루 필요 → 소비하지 않음
    }

    // 클라이언트 좌표 마우스 위치 갱신(raw 상대델타와 별개로 절대 위치 확보 — UI 히트테스트용).
    case WM_MOUSEMOVE: {
        const Vec2i pos{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        if (m_state != nullptr) m_state->OnMouseMove(pos, Vec2{0.0f, 0.0f});  // 델타는 raw가 소유
        return false;
    }

    // 문자 스트림(물리 키와 분리) — 확정 문자를 UTF-8로.
    case WM_CHAR: {
        return textHandled;
    }

    // IME는 해석하지 않고 06으로 그대로 전달(통로만).
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_ENDCOMPOSITION:
    case WM_IME_COMPOSITION:
    case WM_IME_CHAR:
    case WM_IME_SETCONTEXT:
    case WM_IME_NOTIFY:
        m_bus.Publish(ImeRawMessageEvent{.hwnd = hwnd, .msg = msg,
                                         .wparam = wparam, .lparam = lparam});
        return textHandled;

    default:
        return textHandled;
    }
    return false;
}

} // namespace mye::win32
