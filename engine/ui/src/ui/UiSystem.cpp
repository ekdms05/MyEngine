// 게임 UI 캔버스와 포인터·키보드 입력 라우팅.
#include "mye/ui/UiSystem.h"
#include "mye/ui/Widget.h"
#include "mye/ui/Widgets.h"

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
    ResetInput();
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
    // 제거되는 캔버스의 루트 서브트리를 가리키던 상호작용 포인터를 먼저 해제(use-after-free 방지).
    for (auto& c : m_canvases)
        if (c->name() == name) ClearInteractionState(c->root());
    m_canvases.erase(std::remove_if(m_canvases.begin(), m_canvases.end(),
                     [&](const auto& c) { return c->name() == name; }), m_canvases.end());
}

namespace {
// w 가 root 서브트리(root 자신 포함)에 속하는지.
bool IsInSubtree(const Widget* w, const Widget* root) {
    for (const Widget* p = w; p; p = p->parent())
        if (p == root) return true;
    return false;
}
} // namespace

// root 서브트리가 파괴될 때, 그 안을 가리키던 상호작용 포인터를 모두 해제(다음 프레임
//   RouteEvent 의 use-after-free 방지). hovered/focused/pressed/dragging 전부 대상.
void UiSystem::ClearInteractionState(const Widget* root) {
    if (!root) return;
    if (IsInSubtree(m_hovered, root) || IsInSubtree(m_focused, root) ||
        IsInSubtree(m_pressed, root) || IsInSubtree(m_keyPressed, root) || IsInSubtree(m_modal, root)) ResetInput();
}

Expected<UiDocumentHandle, Error> UiSystem::Open(const UiDocument& doc, UiCanvas& canvas) {
    auto tree = doc.Instantiate(m_factory);
    if (!tree) return tree.GetError();
    ClearInteractionState(canvas.root());
    std::erase_if(m_openDocs, [&](const auto& entry) { return entry.second == canvas.root(); });
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
        // 서브트리 안을 가리키던 상호작용 포인터를 setRoot(nullptr) 전에 해제(자식 히트 대응).
        ClearInteractionState(root);
        for (auto& c : m_canvases) {
            if (c->root() == root) { c->setRoot(nullptr); break; }
        }
    }
    m_openDocs.erase(std::remove_if(m_openDocs.begin(), m_openDocs.end(),
                     [&](const auto& p) { return p.first == h.v; }), m_openDocs.end());
}

void UiSystem::SetSkin(std::shared_ptr<UiSkin> skin) { m_skin = std::move(skin); }

// 서브트리에서 uiPos 를 포함하는 최상단(자식 우선·후순위 형제 우선) 위젯을 찾는다.
//   그리기 순서: children() 순서대로 위로 쌓이므로 역순 탐색이 최상단.
static Widget* HitTestSubtree(Widget* w, Vec2 uiPos) {
    if (!w || w->visibility != Visibility::Visible) return nullptr;
    if (w->clipChildren && !w->computedRect.Contains(uiPos)) return nullptr;
    if (const auto* button = w->As<Button>(); button && button->state == Button::State::Disabled) return nullptr;
    // 자식(위에 그려진 것)부터 역순.
    auto kids = w->children();
    for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
        if (Widget* hit = HitTestSubtree(it->get(), uiPos)) return hit;
    }
    // 자기 자신.
    if (w->interactive && w->hitTest(uiPos) && uiPos.x < w->computedRect.x + w->computedRect.w &&
        uiPos.y < w->computedRect.y + w->computedRect.h) return w;
    return nullptr;
}

Widget* UiSystem::HitTest(Vec2 uiPos) const {
    if (uiPos.x < 0 || uiPos.y < 0 || uiPos.x >= m_screen.x || uiPos.y >= m_screen.y) return nullptr;
    if (m_modal) {
        for (auto* parent = m_modal->parent(); parent; parent = parent->parent())
            if (parent->clipChildren && !parent->computedRect.Contains(uiPos)) return nullptr;
        return HitTestSubtree(m_modal, uiPos);
    }
    // sortOrder 큰 캔버스(위 레이어) 먼저.
    for (auto it = m_canvases.rbegin(); it != m_canvases.rend(); ++it) {
        if (!(*it)->visible) continue;
        Widget* root = (*it)->root();
        if (!root) continue;
        if (Widget* hit = HitTestSubtree(root, uiPos)) return hit;
    }
    return nullptr;
}

namespace {
Widget* VisibleModal(Widget* root) {
    if (!root || root->visibility != Visibility::Visible) return nullptr;
    auto children = root->children();
    for (auto it = children.rbegin(); it != children.rend(); ++it)
        if (auto* modal = VisibleModal(it->get())) return modal;
    return root->modal ? root : nullptr;
}
bool CanFocus(Widget* widget) {
    if (!widget || !widget->As<Button>() || !widget->interactive || widget->As<Button>()->state == Button::State::Disabled) return false;
    for (auto* parent = widget; parent; parent = parent->parent())
        if (parent->visibility != Visibility::Visible) return false;
    return true;
}
void FocusableButtons(Widget* root, std::vector<Widget*>& buttons) {
    if (!root || root->visibility != Visibility::Visible) return;
    if (CanFocus(root)) buttons.push_back(root);
    for (const auto& child : root->children()) FocusableButtons(child.get(), buttons);
}
}
Widget* UiSystem::ActiveModal() const {
    for (auto it = m_canvases.rbegin(); it != m_canvases.rend(); ++it)
        if ((*it)->visible) if (auto* modal = VisibleModal((*it)->root())) return modal;
    return nullptr;
}
bool UiSystem::HasModal() const { return ActiveModal() != nullptr; }
bool UiSystem::Focus(Widget* widget) {
    if (widget) {
        auto* modal = ActiveModal();
        if (modal != m_modal) { ResetInput(); m_modal = modal; }
        bool owned = false;
        for (const auto& canvas : m_canvases) owned |= canvas->visible && IsInSubtree(widget, canvas->root());
        if (!owned || !CanFocus(widget) || (m_modal && !IsInSubtree(widget, m_modal))) return false;
    }
    if (m_focused == widget) return true;
    if (m_focused) {
        m_focused->keyboardFocused = false;
        UiEvent lost{}; lost.type = UiEventType::FocusLost; RouteEvent(m_focused, lost);
    }
    m_keyPressed = nullptr; m_activationKey = KeyCode::Unknown;
    m_focused = widget;
    if (widget) { UiEvent gained{}; gained.type = UiEventType::FocusGained; RouteEvent(widget, gained); }
    return true;
}
void UiSystem::ResetInput() {
    if (m_pressed) { UiEvent up{}; up.type = UiEventType::PointerUp; RouteEvent(m_pressed, up); }
    if (m_hovered) { UiEvent leave{}; leave.type = UiEventType::PointerLeave; RouteEvent(m_hovered, leave); }
    Focus(nullptr);
    m_hovered = m_pressed = m_keyPressed = m_modal = nullptr;
    m_dragging = false; m_activationKey = KeyCode::Unknown;
}

bool UiSystem::HandleInput(const InputState& input, Vec2 uiPointer) {
    auto* modal = ActiveModal();
    if (modal != m_modal) { ResetInput(); m_modal = modal; }
    if (m_focused && !Focus(m_focused)) Focus(nullptr);
    if (m_pressed && (!m_pressed->interactive || m_pressed->visibility != Visibility::Visible)) { ResetInput(); m_modal = modal; }
    std::vector<Widget*> buttons;
    if ((m_modal && !m_focused) || input.WasPressed(KeyCode::Tab)) {
        if (m_modal) FocusableButtons(m_modal, buttons);
        else for (const auto& canvas : m_canvases) if (canvas->visible) FocusableButtons(canvas->root(), buttons);
        if (!buttons.empty()) {
            const auto found = std::find(buttons.begin(), buttons.end(), m_focused);
            const bool reverse = input.IsDown(KeyCode::LeftShift) || input.IsDown(KeyCode::RightShift);
            size_t index = found == buttons.end() ? (reverse ? buttons.size()-1 : 0) : static_cast<size_t>(found-buttons.begin());
            if (found != buttons.end() && input.WasPressed(KeyCode::Tab)) index = reverse ? (index+buttons.size()-1)%buttons.size() : (index+1)%buttons.size();
            Focus(buttons[index]); m_focused->keyboardFocused = true;
        }
    }
    if (input.WasPressed(KeyCode::Escape) && !m_modal) Focus(nullptr);
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
            Focus(CanFocus(hit) ? hit : nullptr);
            if (m_focused) m_focused->keyboardFocused = false;
            UiEvent down{}; down.type = UiEventType::PointerDown; down.pointerPos = uiPointer;
            consumed |= RouteEvent(hit, down);
        } else Focus(nullptr);
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

    if (m_focused) {
        for (const auto key : {KeyCode::Enter, KeyCode::Space}) {
            if (input.WasPressed(key) && !m_keyPressed) {
                m_keyPressed = m_focused; m_activationKey = key; m_focused->keyboardFocused = true;
                UiEvent down{}; down.type = UiEventType::KeyDown; down.key = key; RouteEvent(m_focused, down);
            }
            if (input.WasReleased(key) && m_keyPressed && m_activationKey == key) {
                UiEvent up{}; up.type = UiEventType::KeyUp; up.key = key; RouteEvent(m_keyPressed, up);
                if (m_keyPressed == m_focused && CanFocus(m_focused)) {
                    UiEvent click{}; click.type = UiEventType::PointerClick; RouteEvent(m_focused, click);
                }
                m_keyPressed = nullptr; m_activationKey = KeyCode::Unknown;
            }
        }
    }
    m_lastPointer = uiPointer;
    return consumed || hit != nullptr || m_pressed != nullptr || m_modal != nullptr;
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
