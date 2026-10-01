#include "mye/editor/PlayWindow.h"
#include "mye/editor/Viewport.h"
#include "mye/core/platform/Win32Input.h"
#include "mye/render/HybridRenderer.h"
#include "mye/scene/RenderExtract.h"
#include <Windows.h>

namespace mye::editor {
PlayWindow::~PlayWindow() { Close(); }

Expected<void, Error> PlayWindow::Open(rhi::IDevice& device) {
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
    m_resized = ScopedSubscription{m_events, m_events.Subscribe<WindowResizedEvent>([this](const auto& event) {
        if (m_swapChain) m_swapChain->Resize(event.clientSize.x, event.clientSize.y);
        return false;
    })};
    m_input = {};
    m_input.SetKeyboardSuppressed(!HasFocus());
    m_paused = false;
    m_window->AddMessageHook(this, 0);
    return {};
}

void PlayWindow::Close() {
    m_resized.Reset();
    if (m_window) m_window->RemoveMessageHook(this);
    m_target.Shutdown();
    m_swapChain.reset();
    m_window.reset();
    m_input = {};
}
bool PlayWindow::CloseRequested() const { return m_window && m_window->IsCloseRequested(); }
bool PlayWindow::HasFocus() const {
    return m_window && GetForegroundWindow() == m_window->GetNativeHandle();
}
bool PlayWindow::OnMessage(void*, uint32_t message, uint64_t wparam, int64_t lparam) {
    if (message == WM_SETFOCUS) m_input.SetKeyboardSuppressed(false);
    if (message == WM_KILLFOCUS) m_input.SetKeyboardSuppressed(true);
    if (message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) {
        const auto key = win32::ScanCodeToKeyCode(static_cast<uint16_t>((lparam >> 16) & 0xff),
            (lparam & (int64_t{1} << 24)) != 0, false, static_cast<uint16_t>(wparam));
        m_input.OnKey(key, message == WM_KEYDOWN || message == WM_SYSKEYDOWN);
    }
    // Raw Input stays registered to the editor. Registering a second backend would steal it.
    return false;
}
void PlayWindow::Render(render::HybridRenderer& renderer, const scene::RenderProxyList& proxies,
                        Vec2 center, bool paused, rhi::ICommandContext& command) {
    if (!m_window || !m_swapChain || !m_target.IsInitialized()) return;
    if (paused != m_paused) {
        m_window->SetTitle(paused ? "MyEngine — Play · 일시정지" : "MyEngine — Play");
        m_paused = paused;
    }
    ViewportCamera camera;
    camera.center = center;
    m_target.BeginScenePass(command, {.09f, .10f, .13f, 1});
    renderer.Render(proxies, BuildViewportView(camera, m_target.Width(), m_target.Height()), command);
    m_target.EndScenePass(command);
    const auto size = m_window->GetClientSize();
    if (size.x > 0 && size.y > 0) m_target.Blit(command, Backbuffer(), size, {});
}
void PlayWindow::Present() { if (m_swapChain) m_swapChain->Present(false); }
rhi::TextureHandle PlayWindow::Backbuffer() const {
    return m_swapChain ? m_swapChain->GetCurrentBackBuffer() : rhi::TextureHandle{};
}
} // namespace mye::editor
