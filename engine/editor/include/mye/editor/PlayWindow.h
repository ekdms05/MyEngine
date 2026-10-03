#pragma once

#include "mye/core/Events.h"
#include "mye/core/Input.h"
#include "mye/core/platform/Win32Window.h"
#include "mye/render/PixelPerfectTarget.h"
#include "mye/rhi/Rhi.h"
#include "mye/ui/GameOverlay.h"

namespace mye::render { class HybridRenderer; struct HybridViewInfo; }
namespace mye::scene { struct RenderProxyList; }

namespace mye::editor {
// A separate presentation/input surface; simulation remains in PlayModeController.
class PlayWindow final : private IWindowMessageHook {
public:
    ~PlayWindow();
    Expected<void, Error> Open(rhi::IDevice& device, std::string_view fontPath);
    void Close();
    bool IsOpen() const { return m_window != nullptr; }
    bool CloseRequested() const;
    bool HasFocus() const;
    InputState& Input() { return m_input; }
    Expected<void, Error> Render(render::HybridRenderer& renderer, const scene::RenderProxyList& proxies,
                const render::HybridViewInfo& view, bool paused, rhi::ICommandContext& command,
                std::string_view prompt = {}, std::string_view message = {}, ui::Widget* root = nullptr);
    void Present();
    rhi::TextureHandle Backbuffer() const;

private:
    bool OnMessage(void*, uint32_t message, uint64_t wparam, int64_t lparam) override;
    EventBus m_events;
    std::unique_ptr<win32::Win32Window> m_window;
    std::unique_ptr<rhi::ISwapChain> m_swapChain;
    render::PixelPerfectTarget m_target;
    ui::GameOverlay m_overlay;
    ScopedSubscription m_resized;
    InputState m_input;
    bool m_paused = false;
};
} // namespace mye::editor
