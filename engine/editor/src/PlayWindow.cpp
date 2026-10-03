#include "mye/editor/PlayWindow.h"
#include "mye/editor/Viewport.h"
#include "mye/core/platform/Win32Input.h"
#include "mye/render/HybridRenderer.h"
#include "mye/scene/RenderExtract.h"
#include <Windows.h>

namespace mye::editor {
PlayWindow::~PlayWindow() { Close(); }

Expected<void, Error> PlayWindow::Open(rhi::IDevice& device, std::string_view fontPath) {
    if (IsOpen()) return {};
    WindowDesc description;
    description.title = "MyEngine — Play";
    description.clientSize = {960, 540};
    auto created = win32::Win32Window::Create(description, m_events);
    if (!created) return created.GetError();
    m_window = std::move(created).Value();
    rhi::SwapChainDesc swapDescription;
    swapDescription.format = rhi::Format::BGRA8Unorm;
    m_swapChain = device.CreateSwapChain(m_window->GetNativeHandle(), swapDescription);
    if (!m_swapChain) { Close(); return Error{"게임 창의 렌더 표면을 만들 수 없습니다.", 1}; }
    render::PixelPerfectDesc targetDescription;
    targetDescription.useDepth = true;
    targetDescription.backbufferFormat = m_swapChain->GetFormat();
    m_target.Init(device, targetDescription);
    if (!m_target.IsInitialized()) { Close(); return Error{"게임 창의 픽셀 타깃을 만들 수 없습니다.", 1}; }
    auto overlay = m_overlay.Init(device, m_swapChain->GetFormat(), fontPath);
    if (!overlay) { Close(); return overlay.GetError(); }
    m_resized = ScopedSubscription{m_events, m_events.Subscribe<WindowResizedEvent>([this](const auto& event) {
        if (m_swapChain) m_swapChain->Resize(event.clientSize.x, event.clientSize.y);
        return false;
    })};
    m_input = {};
    m_textMessages = {};
    m_input.SetKeyboardSuppressed(!HasFocus());
    m_input.SetMouseSuppressed(!HasFocus());
    m_paused = false;
    m_window->AddMessageHook(this, 0);
    return {};
}

void PlayWindow::Close() {
    m_resized.Reset();
    if (m_window) m_window->RemoveMessageHook(this);
    m_overlay.Shutdown();
    m_target.Shutdown();
    m_swapChain.reset();
    m_window.reset();
    m_input = {};
}
bool PlayWindow::CloseRequested() const { return m_window && m_window->IsCloseRequested(); }
bool PlayWindow::HasFocus() const {
    return m_window && GetForegroundWindow() == m_window->GetNativeHandle();
}
bool PlayWindow::OnMessage(void* handle, uint32_t message, uint64_t wparam, int64_t lparam) {
    const bool textHandled=win32::HandleTextInputMessage(&m_input,m_textMessages,handle,message,wparam,lparam);
    if (message == WM_SETFOCUS) { m_input.SetKeyboardSuppressed(false); m_input.SetMouseSuppressed(false); }
    if (message == WM_KILLFOCUS) { m_input.SetKeyboardSuppressed(true); m_input.SetMouseSuppressed(true); ReleaseCapture(); }
    MouseButton button = MouseButton::Count;
    bool pressed = false;
    switch (message) {
    case WM_LBUTTONDOWN: pressed = true; [[fallthrough]];
    case WM_LBUTTONUP: button = MouseButton::Left; break;
    case WM_RBUTTONDOWN: pressed = true; [[fallthrough]];
    case WM_RBUTTONUP: button = MouseButton::Right; break;
    case WM_MBUTTONDOWN: pressed = true; [[fallthrough]];
    case WM_MBUTTONUP: button = MouseButton::Middle; break;
    case WM_XBUTTONDOWN: pressed = true; [[fallthrough]];
    case WM_XBUTTONUP: button = HIWORD(wparam) == XBUTTON1 ? MouseButton::X1 : MouseButton::X2; break;
    }
    if (button != MouseButton::Count && !m_input.IsMouseSuppressed()) {
        m_input.OnMouseButton(button, pressed);
        if (pressed) {
            const Vec2i position{static_cast<int16_t>(lparam&0xffff),static_cast<int16_t>((lparam>>16)&0xffff)};
            m_input.OnMouseMove(position,{}); SetCapture(static_cast<HWND>(handle));
        } else if (!m_input.IsDown(MouseButton::Left) && !m_input.IsDown(MouseButton::Right) &&
                   !m_input.IsDown(MouseButton::Middle) && !m_input.IsDown(MouseButton::X1) && !m_input.IsDown(MouseButton::X2)) ReleaseCapture();
    }
    if (message == WM_CAPTURECHANGED)
        for (int i = 0; i < static_cast<int>(MouseButton::Count); ++i) m_input.OnMouseButton(static_cast<MouseButton>(i), false);
    if (message == WM_MOUSEWHEEL) m_input.OnWheel(static_cast<int16_t>(HIWORD(wparam)) / float(WHEEL_DELTA));
    if (message==WM_MOUSEMOVE && !m_input.IsMouseSuppressed()) {
        const Vec2i position{static_cast<int16_t>(lparam&0xffff),static_cast<int16_t>((lparam>>16)&0xffff)};
        const auto previous=m_input.MousePosition();
        m_input.OnMouseMove(position, Vec2{float(position.x-previous.x),float(position.y-previous.y)});
    }
    if (!m_input.IsKeyboardSuppressed() && (message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP)) {
        const auto key = win32::ScanCodeToKeyCode(static_cast<uint16_t>((lparam >> 16) & 0xff),
            (lparam & (int64_t{1} << 24)) != 0, false, static_cast<uint16_t>(wparam));
        m_input.OnKey(key, message == WM_KEYDOWN || message == WM_SYSKEYDOWN);
    }
    // Raw Input stays registered to the editor. Registering a second backend would steal it.
    return textHandled;
}
Expected<void, Error> PlayWindow::Render(render::HybridRenderer& renderer, const scene::RenderProxyList& proxies,
                        const render::HybridViewInfo& view, bool paused, rhi::ICommandContext& command,
                        std::string_view prompt, std::string_view message, ui::Widget* root, std::optional<TextInputFocus> textFocus) {
    if (!m_window || !m_swapChain || !m_target.IsInitialized()) return Error{"Play render surface is unavailable", 1};
    if (paused != m_paused) {
        m_window->SetTitle(paused ? "MyEngine — Play · 일시정지" : "MyEngine — Play");
        m_paused = paused;
    }
    m_target.BeginScenePass(command, {.09f, .10f, .13f, 1});
    const auto rendered = renderer.Render(proxies, view, command);
    m_target.EndScenePass(command);
    if (!rendered) return rendered.GetError();
    const auto size = m_window->GetClientSize();
    if (size.x > 0 && size.y > 0) {
        m_target.Blit(command, Backbuffer(), size, view.subpixelResidual);
        const Vec2i logicalSize{static_cast<int32_t>(m_target.Width()), static_cast<int32_t>(m_target.Height())};
        auto overlay=m_overlay.Render(command, Backbuffer(), logicalSize,
            render::PixelPerfectTarget::ComputeLayout(logicalSize, size).destRect, prompt, message, root);
        if (!overlay) return overlay.GetError();
        if (paused || !HasFocus()) textFocus.reset();
        if (textFocus) textFocus->caret=render::PixelPerfectTarget::LogicalRectToWindow(logicalSize,size,textFocus->caret);
        return win32::ConfigureTextInput(m_input,m_window->GetNativeHandle(),textFocus);
    }
    return {};
}
void PlayWindow::Present() { if (m_swapChain) m_swapChain->Present(false); }
rhi::TextureHandle PlayWindow::Backbuffer() const {
    return m_swapChain ? m_swapChain->GetCurrentBackBuffer() : rhi::TextureHandle{};
}
} // namespace mye::editor
