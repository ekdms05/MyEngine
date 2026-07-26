// DotEditorPanel.cpp — 도트(픽셀아트) 에디터 패널 (docs/07 콘텐츠 제작 도구 확장)
//
// 게임 에셋용 픽셀아트를 에디터 안에서 직접 찍는 캔버스. RHI 텍스처 대신 ImDrawList 로
// 셀을 그려(작은 캔버스는 충분히 빠르고 픽셀 단위 편집/히트테스트가 단순) 브러시·지우개·
// 스포이드·채우기 툴과 팔레트를 제공하고, 결과를 assets/sprites/<이름>.png 로 저장한다.
// (MCP dot_write_sprite/dot_from_photo 로 AI가 생성한 스프라이트와 같은 폴더로 흘러 파이프라인 통일.)
//
// 저장은 stb_image_write(고유 심볼) 사용 — mye_asset 의 stb_image(로드) 와 심볼 충돌 없음.
#include "mye/editor/Panel.h"
#include "mye/editor/EditorContext.h"

#include "mye/core/Json.h"
#include "mye/core/I18n.h"

#include "imgui.h"

#if defined(_MSC_VER)
#  pragma warning(push, 0)
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace mye::editor {

namespace {

const PanelDesc kDotEditorDesc{
    /*id*/ "mye.doteditor",
    /*title*/ "닷 에디터",
    /*allowMultiple*/ false,
    /*defaultDock*/ DockSlot::Center,
};

// 기본 팔레트(감각적인 16색 — 도트 작업 시작점). ImU32 = 메모리상 RGBA(리틀엔디안).
const ImU32 kDefaultPalette[] = {
    IM_COL32(0, 0, 0, 0),          // 0: 투명(지우개 색)
    IM_COL32(26, 28, 44, 255),     IM_COL32(93, 39, 93, 255),
    IM_COL32(177, 62, 83, 255),    IM_COL32(239, 125, 87, 255),
    IM_COL32(255, 205, 117, 255),  IM_COL32(167, 240, 112, 255),
    IM_COL32(56, 183, 100, 255),   IM_COL32(37, 113, 121, 255),
    IM_COL32(41, 54, 111, 255),    IM_COL32(59, 93, 201, 255),
    IM_COL32(65, 166, 246, 255),   IM_COL32(115, 239, 247, 255),
    IM_COL32(244, 244, 244, 255),  IM_COL32(148, 176, 194, 255),
    IM_COL32(86, 108, 134, 255),
};

enum class Tool : int { Brush = 0, Eraser, Eyedropper, Bucket };

} // namespace

// -----------------------------------------------------------------------------
class DotEditorPanel final : public IEditorPanel {
public:
    DotEditorPanel() {
        for (ImU32 c : kDefaultPalette) m_palette.push_back(c);
        Resize(32, 32);
        m_color = ImVec4(0.69f, 0.24f, 0.33f, 1.0f);   // 팔레트의 붉은 톤
    }

    const PanelDesc& Desc() const override { return kDotEditorDesc; }

    void OnGui(EditorContext& /*ctx*/) override {
        if (m_firstFrame) { ImGui::SetNextWindowFocus(); m_firstFrame = false; }
        if (!ImGui::Begin(PanelWindowTitle("panel.doteditor", "mye.doteditor").c_str())) { ImGui::End(); return; }

        DrawToolbar();
        ImGui::Separator();
        DrawCanvas();

        ImGui::End();
    }

    void SerializeState(json::Value& out) const override {
        json::Value::Object o;
        o["type"] = json::Value(std::string("mye.doteditor"));
        out = json::Value(std::move(o));
    }

private:
    int                 m_w = 0, m_h = 0;  // 최초 Resize 전 0 — 생성자 Resize의 복사 루프가 빈 버퍼를 읽지 않도록
    std::vector<ImU32>  m_pixels;         // RGBA(ImU32), 알파 0 = 투명
    std::vector<ImU32>  m_palette;
    ImVec4              m_color{1, 1, 1, 1};
    Tool                m_tool = Tool::Brush;
    bool                m_showGrid = true;
    bool                m_firstFrame = true;
    char                m_name[64] = "sprite";
    std::string         m_status;

    void Resize(int w, int h) {
        std::vector<ImU32> next(static_cast<size_t>(w) * h, IM_COL32(0, 0, 0, 0));
        // 기존 내용 좌상단 정렬 보존.
        for (int y = 0; y < h && y < m_h; ++y)
            for (int x = 0; x < w && x < m_w; ++x)
                next[static_cast<size_t>(y) * w + x] = m_pixels[static_cast<size_t>(y) * m_w + x];
        m_pixels = std::move(next);
        m_w = w; m_h = h;
    }

    void DrawToolbar() {
        // 툴 선택.
        auto toolBtn = [&](const char* label, Tool t) {
            const bool sel = (m_tool == t);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(label)) m_tool = t;
            if (sel) ImGui::PopStyleColor();
        };
        using mye::i18n::T;
        toolBtn(T("dot.brush"), Tool::Brush);     ImGui::SameLine();
        toolBtn(T("dot.eraser"), Tool::Eraser);    ImGui::SameLine();
        toolBtn(T("dot.eyedropper"), Tool::Eyedropper); ImGui::SameLine();
        toolBtn(T("dot.bucket"), Tool::Bucket);
        ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
        ImGui::Checkbox(T("dot.grid"), &m_showGrid);

        // 현재 색.
        ImGui::SetNextItemWidth(220);
        ImGui::ColorEdit4(T("dot.color"), &m_color.x,
                          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreview);

        // 팔레트 스와치.
        const float sw = 20.0f;
        for (size_t i = 0; i < m_palette.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const ImU32 c = m_palette[i];
            if (ImGui::ColorButton("##sw", ImGui::ColorConvertU32ToFloat4(c),
                                   ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_AlphaPreview,
                                   ImVec2(sw, sw))) {
                m_color = ImGui::ColorConvertU32ToFloat4(c);
                if ((c >> IM_COL32_A_SHIFT) == 0) m_tool = Tool::Eraser; // 투명 스와치 = 지우개
                else m_tool = Tool::Brush;
            }
            ImGui::PopID();
            if ((i + 1) % 8 != 0 && i + 1 < m_palette.size()) ImGui::SameLine();
        }

        // 캔버스 크기.
        int size = m_w;
        const char* sizes[] = {"16", "24", "32", "48", "64"};
        const int sizeVals[] = {16, 24, 32, 48, 64};
        int cur = 2;
        for (int i = 0; i < 5; ++i) if (sizeVals[i] == m_w) cur = i;
        ImGui::SetNextItemWidth(80);
        if (ImGui::Combo(T("dot.size"), &cur, sizes, 5)) { size = sizeVals[cur]; Resize(size, size); }
        ImGui::SameLine();
        if (ImGui::Button(T("dot.clear"))) std::fill(m_pixels.begin(), m_pixels.end(), IM_COL32(0, 0, 0, 0));

        // 저장.
        ImGui::SetNextItemWidth(160);
        ImGui::InputText(T("dot.name"), m_name, sizeof(m_name));
        ImGui::SameLine();
        if (ImGui::Button(T("dot.savepng"))) SavePng();
        if (!m_status.empty()) { ImGui::SameLine(); ImGui::TextDisabled("%s", m_status.c_str()); }
    }

    void DrawCanvas() {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float cell = std::max(1.0f, std::floor(std::min(avail.x / m_w, avail.y / m_h)));
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 canvasSize(cell * m_w, cell * m_h);
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // 투명 체커보드 배경.
        const ImU32 c0 = IM_COL32(70, 70, 76, 255), c1 = IM_COL32(52, 52, 58, 255);
        for (int y = 0; y < m_h; ++y)
            for (int x = 0; x < m_w; ++x) {
                const ImVec2 p(origin.x + x * cell, origin.y + y * cell);
                dl->AddRectFilled(p, ImVec2(p.x + cell, p.y + cell), ((x + y) & 1) ? c0 : c1);
            }

        // 픽셀.
        for (int y = 0; y < m_h; ++y)
            for (int x = 0; x < m_w; ++x) {
                const ImU32 col = m_pixels[static_cast<size_t>(y) * m_w + x];
                if ((col >> IM_COL32_A_SHIFT) == 0) continue;
                const ImVec2 p(origin.x + x * cell, origin.y + y * cell);
                dl->AddRectFilled(p, ImVec2(p.x + cell, p.y + cell), col);
            }

        // 격자.
        if (m_showGrid && cell >= 5.0f) {
            const ImU32 g = IM_COL32(255, 255, 255, 24);
            for (int x = 0; x <= m_w; ++x)
                dl->AddLine(ImVec2(origin.x + x * cell, origin.y), ImVec2(origin.x + x * cell, origin.y + canvasSize.y), g);
            for (int y = 0; y <= m_h; ++y)
                dl->AddLine(ImVec2(origin.x, origin.y + y * cell), ImVec2(origin.x + canvasSize.x, origin.y + y * cell), g);
        }
        // 외곽선.
        dl->AddRect(origin, ImVec2(origin.x + canvasSize.x, origin.y + canvasSize.y), IM_COL32(255, 255, 255, 90));

        // 입력.
        ImGui::InvisibleButton("##dotcanvas", canvasSize,
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool active = ImGui::IsItemActive() || ImGui::IsItemHovered();
        if (active && (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right))) {
            const ImVec2 m = ImGui::GetIO().MousePos;
            const int cx = static_cast<int>((m.x - origin.x) / cell);
            const int cy = static_cast<int>((m.y - origin.y) / cell);
            if (cx >= 0 && cx < m_w && cy >= 0 && cy < m_h) {
                const bool erase = ImGui::IsMouseDown(ImGuiMouseButton_Right) || m_tool == Tool::Eraser;
                ApplyTool(cx, cy, erase);
            }
        }
    }

    void ApplyTool(int x, int y, bool erase) {
        const size_t idx = static_cast<size_t>(y) * m_w + x;
        const ImU32 cur = ImGui::ColorConvertFloat4ToU32(m_color);
        switch (m_tool) {
        case Tool::Eyedropper: {
            const ImU32 c = m_pixels[idx];
            if ((c >> IM_COL32_A_SHIFT) != 0) m_color = ImGui::ColorConvertU32ToFloat4(c);
            break;
        }
        case Tool::Bucket: {
            if (!erase) FloodFill(x, y, m_pixels[idx], cur);
            else FloodFill(x, y, m_pixels[idx], IM_COL32(0, 0, 0, 0));
            break;
        }
        case Tool::Brush:
        case Tool::Eraser:
        default:
            m_pixels[idx] = erase ? IM_COL32(0, 0, 0, 0) : cur;
            break;
        }
    }

    void FloodFill(int x, int y, ImU32 target, ImU32 repl) {
        if (target == repl) return;
        std::vector<std::pair<int, int>> stack{{x, y}};
        while (!stack.empty()) {
            auto [cx, cy] = stack.back(); stack.pop_back();
            if (cx < 0 || cx >= m_w || cy < 0 || cy >= m_h) continue;
            const size_t i = static_cast<size_t>(cy) * m_w + cx;
            if (m_pixels[i] != target) continue;
            m_pixels[i] = repl;
            stack.push_back({cx + 1, cy}); stack.push_back({cx - 1, cy});
            stack.push_back({cx, cy + 1}); stack.push_back({cx, cy - 1});
        }
    }

    void SavePng() {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path dir = fs::path("assets") / "sprites";
        fs::create_directories(dir, ec);
        std::string fname = m_name[0] ? m_name : "sprite";
        const fs::path out = dir / (fname + ".png");
        // m_pixels 는 이미 RGBA 바이트 순서 → stb 에 그대로 전달.
        const int ok = stbi_write_png(out.string().c_str(), m_w, m_h, 4, m_pixels.data(), m_w * 4);
        if (ok) m_status = "저장: " + fs::absolute(out, ec).string();
        else    m_status = "저장 실패";
    }
};

// -----------------------------------------------------------------------------
class DotEditorPanelFactory final : public IEditorPanelFactory {
public:
    const PanelDesc& Desc() const override { return kDotEditorDesc; }
    std::unique_ptr<IEditorPanel> Create() override { return std::make_unique<DotEditorPanel>(); }
};

std::unique_ptr<IEditorPanelFactory> MakeDotEditorPanelFactory() {
    return std::make_unique<DotEditorPanelFactory>();
}

} // namespace mye::editor
