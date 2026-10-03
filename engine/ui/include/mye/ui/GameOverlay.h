#pragma once

#include "mye/render/SpriteBatch.h"
#include "mye/text/GlyphAtlas.h"
#include "mye/text/TextRenderer.h"
#include "mye/ui/UiRenderer.h"

#include <memory>
#include <string>

namespace mye::ui {
class Widget;

// Read-only saved widgets and game feedback. The app owns simulation and chooses the presentation surface.
class GameOverlay {
public:
    GameOverlay() = default;
    ~GameOverlay();
    GameOverlay(const GameOverlay&) = delete;
    GameOverlay& operator=(const GameOverlay&) = delete;

    Expected<void, Error> Init(rhi::IDevice& device, rhi::Format format, std::string_view fontPath);
    void Shutdown();
    bool IsInitialized() const { return m_batch.IsInitialized() && m_fonts != nullptr; }

    // Destination uses the same integer upscale/letterbox rectangle as the world, without its camera residual.
    // Open a separate load pass with no depth, after world rendering/blit. One render per instance per frame.
    Expected<void, Error> Render(rhi::ICommandContext& command, rhi::TextureHandle target,
        Vec2i logicalSize, RectInt destination, std::string_view prompt, std::string_view message, Widget* root = nullptr);

private:
    UiRenderer m_renderer;
    render::SpriteBatch m_batch;
    text::GlyphAtlas m_atlas;
    text::TextRenderer m_text;
    std::unique_ptr<text::FontRegistry> m_fonts;
    text::TextLayout m_promptLayout, m_messageLayout;
    std::string m_prompt, m_message;
    Vec2i m_logicalSize{};
    uint64_t m_frame = 0;
};

} // namespace mye::ui
