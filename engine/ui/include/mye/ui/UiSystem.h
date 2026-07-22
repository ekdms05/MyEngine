// mye/ui/UiSystem.h — 인게임 UI 시스템 진입점 (docs/06 §1 시스템)
//
// 소유: 06 (M5-A). UiRoot(화면당 1개) → UiCanvas(레이어) → Widget 트리. 문서 열기/닫기,
//   스킨 세팅, 히트 테스트(입력 컨텍스트가 질의), update(레이아웃·툴팁 타이머), render.
//   입력: core InputState 를 소비해 UiEvent 로 변환·라우팅(터널→타깃→버블). Lua 컨트롤러 라우팅
//   표면 정의(구현 바인딩은 05). MYE_SERVICE 로 EngineContext 등록 가능.
#pragma once

#include "mye/ui/UiTypes.h"
#include "mye/ui/UiDocument.h"
#include "mye/ui/UiSkin.h"
#include "mye/ui/UiRenderer.h"
#include "mye/core/Base.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace mye { class InputState; }
namespace mye::render { class SpriteBatch; }
namespace mye::rhi    { class IDevice; class ICommandContext; }
namespace mye::text   { class GlyphAtlas; class TextRenderer; class FontRegistry; }

namespace mye::ui {

class Widget;

// 레이어 단위. HUD·창·툴팁 등. 캔버스별 z-order·정렬. 창은 캔버스 안에서 z 를 갖고 클릭 시 최상단.
class UiCanvas {
public:
    explicit UiCanvas(std::string name) : m_name(std::move(name)) {}
    Widget* root() { return m_root.get(); }
    void    setRoot(WidgetPtr root) { m_root = std::move(root); }
    std::string_view name() const { return m_name; }
    int  sortOrder = 0;                 // 캔버스 간 그리기 순서(작을수록 먼저=아래)
    bool visible = true;
private:
    std::string m_name;
    WidgetPtr   m_root;
};

// 열린 문서 핸들(불투명).
struct UiDocumentHandle {
    uint32_t v = 0xFFFF'FFFFu;
    bool IsValid() const { return v != 0xFFFF'FFFFu; }
};

class UiSystem {
public:
    MYE_SERVICE(mye::ui::UiSystem);

    UiSystem() = default;
    ~UiSystem() = default;
    UiSystem(const UiSystem&) = delete;
    UiSystem& operator=(const UiSystem&) = delete;

    // 렌더 리소스·폰트·아틀라스 참조 주입. 텍스트 스택은 외부 소유(수명 > UiSystem).
    void Init(rhi::IDevice& device, text::FontRegistry& fonts,
              text::GlyphAtlas& atlas, text::TextRenderer& textRenderer);
    void Shutdown();

    void SetScreenSize(Vec2i pixels);

    // --- 캔버스 ---
    UiCanvas* CreateCanvas(std::string name, int sortOrder = 0);
    UiCanvas* FindCanvas(std::string_view name);
    void      RemoveCanvas(std::string_view name);

    // --- 문서 ---
    // 문서를 인스턴스화해 canvas 에 붙인다. controller 는 Lua 참조(05 바인딩 — MVP 는 미배선 null).
    Expected<UiDocumentHandle, Error> Open(const UiDocument& doc, UiCanvas& canvas);
    void Close(UiDocumentHandle h);

    // --- 스킨 ---
    void SetSkin(std::shared_ptr<UiSkin> skin);
    UiSkin* Skin() { return m_skin.get(); }

    // --- 입력·업데이트 ---
    // 최상단(z·캔버스 순) 위젯 히트. 입력 컨텍스트(mye::input)가 "포인터가 UI 위인가" 질의.
    Widget* HitTest(Vec2 uiPos) const;
    // core InputState 를 소비해 UiEvent 생성·라우팅. 소비 여부(=UI 가 입력을 먹었나) 반환.
    bool HandleInput(const InputState& input, Vec2 uiPointer);
    // 레이아웃(dirty 시 measure/arrange)·툴팁 타이머·애니(후속 UiTween).
    void Update(float dt);

    // --- 렌더 ---
    void Render(rhi::ICommandContext& ctx, render::SpriteBatch& batch);

    // --- 위젯 팩토리(커스텀 위젯 등록 확장점) ---
    WidgetFactory& Factory() { return m_factory; }

    // 포커스 위젯(텍스트 입력 라우팅 대상). 입력 컨텍스트가 "포커스가 텍스트를 받나" 질의.
    Widget* focused() const { return m_focused; }

private:
    // 이벤트 라우팅: target 까지 터널 → target → 루트까지 버블.
    bool RouteEvent(Widget* target, UiEvent& e);

    // root 서브트리(root 포함)를 가리키던 hovered/focused/pressed 포인터를 해제(파괴 시 dangling 방지).
    void ClearInteractionState(const Widget* root);

    WidgetFactory                          m_factory;
    std::vector<std::unique_ptr<UiCanvas>> m_canvases;   // sortOrder 정렬 유지
    std::shared_ptr<UiSkin>                m_skin;
    UiRenderer                             m_renderer;

    text::FontRegistry*  m_fonts = nullptr;
    text::GlyphAtlas*    m_atlas = nullptr;
    text::TextRenderer*  m_text  = nullptr;

    Widget*  m_hovered = nullptr;
    Widget*  m_focused = nullptr;
    Widget*  m_pressed = nullptr;    // pointer down 을 받은 위젯(click 판정)
    Vec2     m_pressPos{};           // down 시점 포인터(드래그 임계 판정)
    Vec2     m_lastPointer{};        // 직전 프레임 포인터(드래그 델타)
    bool     m_dragging = false;
    static constexpr float kDragThreshold = 3.0f;   // 드래그 시작 픽셀 임계
    Vec2i    m_screen{960, 540};
    uint32_t m_nextDocId = 0;
    std::vector<std::pair<uint32_t, Widget*>> m_openDocs;   // docId → 루트 위젯(캔버스 내)
};

} // namespace mye::ui
