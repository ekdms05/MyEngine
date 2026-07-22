// mye/ui/UiSystem.cpp — 인게임 UI 시스템 (M5-A 골격; 라우팅·히트테스트·Lua 라우팅은 구현 에이전트)
#include "mye/ui/UiSystem.h"
#include "mye/ui/Widget.h"

#include "mye/core/Input.h"
#include "mye/text/TextRenderer.h"
#include "mye/text/GlyphAtlas.h"

#include <algorithm>

namespace mye::ui {

void UiSystem::Init(rhi::IDevice& device, text::FontRegistry& fonts,
                    text::GlyphAtlas& atlas, text::TextRenderer& textRenderer) {
    m_fonts = &fonts;
    m_atlas = &atlas;
    m_text  = &textRenderer;
    m_renderer.Init(device);
    m_renderer.SetScreenSize(m_screen);
}

void UiSystem::Shutdown() {
    m_renderer.Shutdown();
    m_canvases.clear();
    m_openDocs.clear();
    m_hovered = m_focused = m_pressed = nullptr;
}

void UiSystem::SetScreenSize(Vec2i pixels) {
    m_screen = pixels;
    m_renderer.SetScreenSize(pixels);
}

UiCanvas* UiSystem::CreateCanvas(std::string name, int sortOrder) {
    auto c = std::make_unique<UiCanvas>(std::move(name));
    c->sortOrder = sortOrder;
    UiCanvas* raw = c.get();
    m_canvases.push_back(std::move(c));
    std::stable_sort(m_canvases.begin(), m_canvases.end(),
                     [](const auto& a, const auto& b) { return a->sortOrder < b->sortOrder; });
    return raw;
}

UiCanvas* UiSystem::FindCanvas(std::string_view name) {
    for (auto& c : m_canvases) if (c->name() == name) return c.get();
    return nullptr;
}

void UiSystem::RemoveCanvas(std::string_view name) {
    m_canvases.erase(std::remove_if(m_canvases.begin(), m_canvases.end(),
                     [&](const auto& c) { return c->name() == name; }), m_canvases.end());
}

Expected<UiDocumentHandle, Error> UiSystem::Open(const UiDocument& doc, UiCanvas& canvas) {
    auto tree = doc.Instantiate(m_factory);
    if (!tree) return tree.GetError();
    Widget* raw = tree.Value().get();
    canvas.setRoot(std::move(tree).Value());
    const uint32_t id = m_nextDocId++;
    m_openDocs.emplace_back(id, raw);
    // TODO(impl): controllerScript 를 05 Lua 로 바인딩(onOpen 호출).
    return UiDocumentHandle{id};
}

void UiSystem::Close(UiDocumentHandle h) {
    if (!h.IsValid()) return;
    // 캔버스에서 해당 루트 위젯 제거(+ 상태머신 참조 무효화).
    Widget* root = nullptr;
    for (const auto& p : m_openDocs) if (p.first == h.v) { root = p.second; break; }
    if (root) {
        for (auto& c : m_canvases) {
            if (c->root() == root) { c->setRoot(nullptr); break; }
        }
        if (m_hovered == root) m_hovered = nullptr;
        if (m_focused == root) m_focused = nullptr;
        if (m_pressed == root) { m_pressed = nullptr; m_dragging = false; }
    }
    m_openDocs.erase(std::remove_if(m_openDocs.begin(), m_openDocs.end(),
                     [&](const auto& p) { return p.first == h.v; }), m_openDocs.end());
}

void UiSystem::SetSkin(std::shared_ptr<UiSkin> skin) { m_skin = std::move(skin); }

// 서브트리에서 uiPos 를 포함하는 최상단(자식 우선·후순위 형제 우선) 위젯을 찾는다.
//   그리기 순서: children() 순서대로 위로 쌓이므로 역순 탐색이 최상단.
static Widget* HitTestSubtree(Widget* w, Vec2 uiPos) {
    if (!w || w->visibility != Visibility::Visible) return nullptr;
    // 자식(위에 그려진 것)부터 역순.
    auto kids = w->children();
    for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
        if (Widget* hit = HitTestSubtree(it->get(), uiPos)) return hit;
    }
    // 자기 자신.
    if (w->interactive && w->computedRect.Contains(uiPos)) return w;
    return nullptr;
}

Widget* UiSystem::HitTest(Vec2 uiPos) const {
    // sortOrder 큰 캔버스(위 레이어) 먼저.
    for (auto it = m_canvases.rbegin(); it != m_canvases.rend(); ++it) {
        if (!(*it)->visible) continue;
        Widget* root = (*it)->root();
        if (!root) continue;
        if (Widget* hit = HitTestSubtree(root, uiPos)) return hit;
    }
    return nullptr;
}

bool UiSystem::HandleInput(const InputState& input, Vec2 uiPointer) {
    bool consumed = false;
    Widget* hit = HitTest(uiPointer);

    // --- hover(enter/leave) ---
    if (hit != m_hovered) {
        if (m_hovered) {
            UiEvent leave{}; leave.type = UiEventType::PointerLeave; leave.pointerPos = uiPointer;
            RouteEvent(m_hovered, leave);
        }
        m_hovered = hit;
        if (m_hovered) {
            UiEvent enter{}; enter.type = UiEventType::PointerEnter; enter.pointerPos = uiPointer;
            RouteEvent(m_hovered, enter);
        }
    }

    // --- pointer down ---
    if (input.WasPressed(MouseButton::Left)) {
        m_pressed = hit;
        m_pressPos = uiPointer;
        m_dragging = false;
        if (hit) {
            m_focused = hit;
            UiEvent down{}; down.type = UiEventType::PointerDown; down.pointerPos = uiPointer;
            consumed |= RouteEvent(hit, down);
        }
    }

    // --- drag ---
    if (input.IsDown(MouseButton::Left) && m_pressed) {
        const Vec2 d = uiPointer - m_pressPos;
        const float dist2 = d.x * d.x + d.y * d.y;
        if (!m_dragging && dist2 >= kDragThreshold * kDragThreshold) {
            m_dragging = true;
            UiEvent ds{}; ds.type = UiEventType::DragStart;
            ds.pointerPos = uiPointer; ds.pointerDelta = d;
            consumed |= RouteEvent(m_pressed, ds);
        }
        if (m_dragging) {
            UiEvent dr{}; dr.type = UiEventType::Drag;
            dr.pointerPos = uiPointer; dr.pointerDelta = uiPointer - m_lastPointer;
            consumed |= RouteEvent(m_pressed, dr);
        }
    }

    // --- pointer up / click ---
    if (input.WasReleased(MouseButton::Left)) {
        if (m_pressed) {
            UiEvent up{}; up.type = UiEventType::PointerUp; up.pointerPos = uiPointer;
            consumed |= RouteEvent(m_pressed, up);
            if (m_dragging) {
                UiEvent drop{}; drop.type = UiEventType::Drop; drop.pointerPos = uiPointer;
                RouteEvent(m_pressed, drop);
            } else if (m_pressed == hit) {
                // down·up 이 같은 위젯 → click.
                UiEvent click{}; click.type = UiEventType::PointerClick; click.pointerPos = uiPointer;
                consumed |= RouteEvent(m_pressed, click);
            }
        }
        m_pressed = nullptr;
        m_dragging = false;
    }

    // --- scroll ---
    const float wheel = input.WheelDelta();
    if (wheel != 0.0f && hit) {
        UiEvent sc{}; sc.type = UiEventType::Scroll; sc.pointerPos = uiPointer; sc.scrollDelta = wheel;
        consumed |= RouteEvent(hit, sc);
    }

    m_lastPointer = uiPointer;
    return consumed || hit != nullptr;
}

void UiSystem::Update(float /*dt*/) {
    // TODO(impl): dirty 레이아웃 measure/arrange, 툴팁 타이머, UiTween(후속).
    for (auto& c : m_canvases) {
        if (Widget* root = c->root()) {
            const Vec2 avail{static_cast<float>(m_screen.x), static_cast<float>(m_screen.y)};
            root->measure(avail);
            root->arrange(UiRect{0, 0, avail.x, avail.y});
        }
    }
}

void UiSystem::Render(rhi::ICommandContext& ctx, render::SpriteBatch& batch) {
    if (!m_fonts || !m_atlas || !m_text) return;
    for (auto& c : m_canvases) {
        if (!c->visible) continue;
        if (Widget* root = c->root())
            m_renderer.Render(ctx, batch, *m_atlas, *m_text, *m_fonts, *root);
    }
}

bool UiSystem::RouteEvent(Widget* target, UiEvent& e) {
    if (!target) return false;
    e.target = target;

    // target 의 조상 경로 수집(터널·버블 순서). target 자신은 별도 처리.
    std::vector<Widget*> ancestors;
    for (Widget* w = target->parent(); w; w = w->parent()) ancestors.push_back(w);
    // ancestors: [parent, ..., root].

    // 1) 터널(capture): root → parent.
    e.phase = UiRoutePhase::Tunnel;
    for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it) {
        if ((*it)->onEvent(e) || e.handled) { e.handled = true; return true; }
    }
    // 2) 타깃.
    e.phase = UiRoutePhase::Target;
    if (target->onEvent(e) || e.handled) { e.handled = true; return true; }
    // 3) 버블: target → root.
    e.phase = UiRoutePhase::Bubble;
    for (Widget* w = target->parent(); w; w = w->parent()) {
        if (w->onEvent(e) || e.handled) { e.handled = true; return true; }
    }
    return e.handled;
}

} // namespace mye::ui
