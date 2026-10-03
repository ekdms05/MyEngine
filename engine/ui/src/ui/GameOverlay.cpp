#include "mye/ui/GameOverlay.h"
#include "mye/ui/Widget.h"
#include "mye/core/JsonFile.h"
#include "mye/rhi/Rhi.h"

#include <algorithm>
#include <fstream>

namespace mye::ui {
namespace {
constexpr int32_t kMargin = 16;
constexpr int32_t kPadding = 12;
constexpr int32_t kTextInset = 2 * (kMargin + kPadding);
// ponytail: feedback caps at 180 logical pixels; longer dialogue needs an authored scrollable UI.
constexpr float kMaxPanelHeight = 180;
constexpr float kMaxPromptHeight = 26;
constexpr size_t kMaxTextBytes = 4096;
// Message/prompt are plain text; authored braces must not become rich-text commands.
std::string EscapeText(std::string_view source) {
    std::string result;
    result.reserve(source.size());
    for (const auto character : source) {
        result.push_back(character);
        if (character == '{') result.push_back('{');
    }
    return result;
}
} // namespace

GameOverlay::~GameOverlay() { Shutdown(); }

Expected<void, Error> GameOverlay::Init(rhi::IDevice& device, rhi::Format format, std::string_view fontPath) {
    Shutdown();
    try {
        std::ifstream file(Utf8Path(fontPath), std::ios::binary | std::ios::ate);
        const auto size = file ? file.tellg() : std::streampos{-1};
        if (size <= 0 || size > 16 * 1024 * 1024)
            return Error{std::string(fontPath) + ": game UI font is missing, empty or exceeds 16 MiB", 1};
        std::vector<std::byte> bytes(static_cast<size_t>(size));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            return Error{std::string(fontPath) + ": cannot read game UI font", 1};
        auto fonts = std::make_unique<text::FontRegistry>();
        auto registered = fonts->RegisterTtf(bytes, "game UI");
        if (!registered) return Error{std::string(fontPath) + ": " + registered.GetError().message, 1};
        m_fonts = std::move(fonts);
    } catch (const std::system_error& error) {
        return Error{std::string(fontPath) + ": " + error.what(), error.code().value()};
    }
    m_renderer.Init(device);
    m_batch.Init(device, format, false);
    m_atlas.Init(device);
    if (!m_renderer.WhiteTexture().IsValid() || !m_batch.IsInitialized()) {
        Shutdown();
        return Error{"Game UI render resources could not be initialized", 1};
    }
    return {};
}

void GameOverlay::Shutdown() {
    m_promptLayout.Clear();
    m_messageLayout.Clear();
    m_batch.Shutdown();
    m_renderer.Shutdown();
    m_atlas.Shutdown();
    m_fonts.reset();
    m_prompt.clear();
    m_message.clear();
    m_logicalSize = {};
    m_frame = 0;
}

Expected<void, Error> GameOverlay::Render(rhi::ICommandContext& command, rhi::TextureHandle target,
    Vec2i logicalSize, RectInt destination, std::string_view prompt, std::string_view message) {
    if (!IsInitialized() || !target.IsValid()) return Error{"Game UI render surface is unavailable", 1};
    if (logicalSize.x <= kTextInset || logicalSize.y <= kTextInset || destination.w <= 0 || destination.h <= 0)
        return Error{"Game UI render dimensions are invalid", 1};
    if (prompt.size() > kMaxTextBytes || message.size() > kMaxTextBytes)
        return Error{"Game UI prompt/message exceeds 4096 UTF-8 bytes", 1};
    if (prompt.empty() && message.empty()) return {};

    const bool resized = m_logicalSize != logicalSize;
    m_atlas.BeginFrame(++m_frame);
    text::LayoutParams parameters;
    parameters.maxWidth = static_cast<float>(logicalSize.x - kTextInset);
    parameters.lineSpacing = 2;
    text::TextStyle style;
    style.size = 16;
    if (resized || m_prompt != prompt) {
        m_prompt.assign(prompt);
        parameters.wrap = text::WrapMode::None;
        style.color = {.73f, .8f, .9f, 1};
        m_promptLayout.Set(command, m_atlas, *m_fonts, EscapeText(prompt), style, parameters);
    }
    if (resized || m_message != message) {
        m_message.assign(message);
        parameters.wrap = text::WrapMode::WordChar;
        style.color = Color::White();
        m_messageLayout.Set(command, m_atlas, *m_fonts, EscapeText(message), style, parameters);
    }
    m_logicalSize = logicalSize;
    m_renderer.SetScreenSize(logicalSize);

    const float promptHeight = prompt.empty() ? 0.0f : std::min(kMaxPromptHeight, static_cast<float>(m_promptLayout.measuredSize().y));
    const float messageHeight = message.empty() ? 0.0f : static_cast<float>(m_messageLayout.measuredSize().y);
    const float gap = prompt.empty() || message.empty() ? 0.0f : 6.0f;
    const float height = std::min({kMaxPanelHeight, static_cast<float>(logicalSize.y - 2 * kMargin), promptHeight + messageHeight + gap + 2 * kPadding});
    AnchorRect anchor;
    anchor.anchorMin = {0, 1}; anchor.anchorMax = {1, 1}; anchor.pivot = {0, 1};
    anchor.offsetMin = {kMargin, -kMargin}; anchor.offsetMax = {kMargin, 0}; anchor.sizeDelta = {0, height};
    const auto panel = ComputeAnchoredRect(anchor, {0, 0, static_cast<float>(logicalSize.x), static_cast<float>(logicalSize.y)});

    rhi::RenderPassColorAttachment color;
    color.texture = target;
    color.loadOp = rhi::LoadOp::Load;
    rhi::RenderPassBeginDesc pass;
    pass.colorAttachments = {&color, 1};
    pass.debugName = "game.ui.feedback";
    command.BeginRenderPass(pass);
    rhi::Viewport viewport;
    viewport.x = static_cast<float>(destination.x); viewport.y = static_cast<float>(destination.y);
    viewport.width = static_cast<float>(destination.w); viewport.height = static_cast<float>(destination.h);
    viewport.maxDepth = 1;
    command.SetViewport(viewport);
    m_batch.Begin(m_renderer.ViewProj());
    UiDrawContext draw(m_batch, command, m_atlas, m_text, *m_fonts);
    draw.SetScreenSize(logicalSize);
    draw.SetWhiteTexture(m_renderer.WhiteTexture());
    draw.DrawRect(panel, {.035f, .045f, .065f, .94f});
    draw.PushScissor({panel.x + kPadding, panel.y + kPadding, panel.w - 2 * kPadding, panel.h - 2 * kPadding});
    const Vec2 origin{panel.x + kPadding, panel.y + kPadding};
    if (!prompt.empty()) {
        draw.PushScissor({origin.x, origin.y, panel.w - 2 * kPadding, promptHeight});
        draw.DrawTextLayout(m_promptLayout, origin);
        draw.PopScissor();
    }
    if (!message.empty()) draw.DrawTextLayout(m_messageLayout, {origin.x, origin.y + promptHeight + gap});
    draw.PopScissor();
    m_batch.End(command);
    command.EndRenderPass();
    return {};
}

} // namespace mye::ui
