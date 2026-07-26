// ViewportPanel.cpp — 씬 뷰포트 패널(오프스크린 RT 표시·팬/줌·그리드·픽킹·이동 기즈모)
//                                                         (docs/07 씬 뷰포트·기즈모)
// 07: 활성 World를 EditorModule이 오프스크린 RT에 렌더한 결과를 ImGui::Image로 표시한다.
//   상호작용:
//     - 팬: 마우스 중간/우클릭 드래그 → 카메라 center 이동.
//     - 줌: 휠 → 카메라 zoom(마우스 위치 앵커 유지).
//     - 픽킹: 좌클릭 → CPU 레이/스프라이트 AABB 히트테스트 → SelectionManager 선택.
//     - 이동 기즈모: 선택 엔티티의 화면 위치에 축 핸들을 그려 드래그 → LocalTransform.position
//       변경을 PropertyEditCommand(ActiveStack 경유)로 발행. Ctrl 스냅(0.25u).
//   패널은 IEditorViewport(EditorModule 구현)로만 렌더 자원·좌표 변환에 접근한다(RHI 직접 접근 금지).
#include "mye/editor/Viewport.h"
#include "mye/editor/EditorApp.h"
#include "mye/editor/EditorContext.h"
#include "mye/editor/Selection.h"
#include "mye/editor/Command.h"
#include "mye/editor/CommandStack.h"
#include "mye/editor/PlayMode.h"
#include "mye/editor/BuiltinPanels.h"   // InstantiateAssetToWorld("MYE_ASSET" 드롭 계약)

#include "mye/ecs/World.h"
#include "mye/ecs/View.h"
#include "mye/scene/Transform.h"
#include "mye/scene/Renderable.h"

#include "mye/core/Json.h"
#include "mye/core/I18n.h"
#include "mye/refl/PropertyPath.h"
#include "mye/refl/TypeInfo.h"
#include "mye/refl/TypeId.h"
#include "mye/refl/TypeRegistry.h"
#include "mye/ser/JsonArchive.h"

#include "imgui.h"

#include <cmath>
#include <string>
#include <vector>

namespace mye::editor {

namespace {

const PanelDesc kViewportDesc{
    /*id*/ "mye.viewport",
    /*title*/ "씬 뷰포트",
    /*allowMultiple*/ false,
    /*defaultDock*/ DockSlot::Center,
};

// 07 §4: 편집 대상 스택(플레이 중이면 플레이 스택, 아니면 포커스 문서 스택).
CommandStack* ActiveStack(EditorContext& ctx) {
    if (ctx.playMode && ctx.playMode->IsPlaying())
        return ctx.playMode->PlayCommandStack();
    return ctx.commands;
}

// LocalTransform 리플렉션 TypeInfo(정렬·커맨드 target). 없으면 null.
const refl::TypeInfo* LocalTransformType() {
    static const refl::TypeInfo* t = []() -> const refl::TypeInfo* {
        for (const refl::TypeInfo* ti : refl::TypeRegistry::Get().All())
            if (ti && ti->GetKind() == refl::Kind::Struct &&
                std::string_view(ti->Name()).find("LocalTransform") != std::string_view::npos)
                return ti;
        return nullptr;
    }();
    return t;
}

// LocalTransform.position 을 refl 경로로 직렬화한 ValueBlob(커맨드 before/after).
ValueBlob PositionBlob(const scene::LocalTransform& lt) {
    const refl::TypeInfo* t = LocalTransformType();
    if (!t) return ValueBlob{};
    refl::PropertyPath path;
    path.segments.push_back(refl::PathSegment{std::string("position")});
    auto wr = ser::JsonArchive::ForWrite();
    auto r = refl::ReadValue(*t, &lt, path, wr);
    if (!r) return ValueBlob{};
    return ValueBlob{json::Stringify(wr.Root(), 0)};
}

refl::PropertyPath PositionPath() {
    refl::PropertyPath path;
    path.segments.push_back(refl::PathSegment{std::string("position")});
    return path;
}

// 스프라이트 반경(월드 단위) 추정 — 픽킹 AABB용. 피벗·시트 크기 정보가 없으므로 보수적 기본.
constexpr float kPickHalfExtent = 0.5f;   // 1x1 unit 히트 박스(PPU48 → 48px 스프라이트 근사)

} // namespace

// -----------------------------------------------------------------------------
class ViewportPanel final : public IEditorPanel {
public:
    const PanelDesc& Desc() const override { return kViewportDesc; }

    void OnGui(EditorContext& ctx) override {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        const bool open = ImGui::Begin(PanelWindowTitle("panel.viewport", "mye.viewport").c_str());
        ImGui::PopStyleVar();
        if (!open) { ImGui::End(); return; }

        IEditorViewport* vp = ctx.app ? ctx.app->Viewport() : nullptr;
        if (!vp) {
            ImGui::TextDisabled("렌더러 미가용(헤드리스 또는 초기화 실패)");
            ImGui::End();
            return;
        }

        // 사용 가능한 영역 = 이미지 크기(정수 픽셀). 최소 1x1.
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const uint32_t wPx = static_cast<uint32_t>(avail.x < 1.0f ? 1.0f : avail.x);
        const uint32_t hPx = static_cast<uint32_t>(avail.y < 1.0f ? 1.0f : avail.y);
        vp->SetViewportSize(wPx, hPx);
        vp->SetCamera(m_cam);

        const ImVec2 imgPos = ImGui::GetCursorScreenPos();   // 이미지 좌상단(화면 좌표)

        void* tex = vp->ColorTextureId();
        if (tex) {
            ImGui::Image(reinterpret_cast<ImTextureID>(tex), ImVec2(avail.x, avail.y));
        } else {
            ImGui::Dummy(ImVec2(avail.x, avail.y));
        }

        // 이미지 영역을 상호작용 대상으로(픽킹·팬·기즈모 입력). InvisibleButton으로 hovered/active.
        ImGui::SetCursorScreenPos(imgPos);
        ImGui::InvisibleButton("##viewport_input", ImVec2(avail.x, avail.y),
                               ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
        const bool hovered = ImGui::IsItemHovered();
        ImGuiIO& io = ImGui::GetIO();

        // 이미지 로컬 픽셀 좌표 헬퍼(마우스 → 이미지 좌상단 기준).
        auto mouseLocal = [&]() -> Vec2 {
            return Vec2{io.MousePos.x - imgPos.x, io.MousePos.y - imgPos.y};
        };

        // ---- 에셋 드롭("MYE_ASSET") → 드롭 지점 월드 좌표에 인스턴스화(BuiltinPanels 계약) ----
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("MYE_ASSET")) {
                if (pl->Data && pl->DataSize > 0) {
                    const char* vpath = static_cast<const char*>(pl->Data);
                    const Vec2 worldPt = vp->ScreenToWorld(mouseLocal());
                    InstantiateAssetToWorld(ctx, std::string_view(vpath), worldPt);
                }
            }
            ImGui::EndDragDropTarget();
        }

        // ---- 팬(중간/우클릭 드래그) ----
        if (hovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
                        ImGui::IsMouseDragging(ImGuiMouseButton_Right))) {
            const ImVec2 d = io.MouseDelta;
            // 화면 픽셀 델타 → 월드 델타(줌·PPU 반영). WorldToScreen의 역: 1px = worldPerPixel.
            const Vec2 a = vp->ScreenToWorld(mouseLocal());
            const Vec2 b = vp->ScreenToWorld(Vec2{mouseLocal().x - d.x, mouseLocal().y - d.y});
            m_cam.center += (a - b);
        }

        // ---- 줌(휠, 마우스 앵커 유지) ----
        if (hovered && io.MouseWheel != 0.0f) {
            const Vec2 anchorWorldBefore = vp->ScreenToWorld(mouseLocal());
            const float factor = io.MouseWheel > 0.0f ? 1.1f : (1.0f / 1.1f);
            m_cam.zoom = Clamp(m_cam.zoom * factor, 0.1f, 16.0f);
            vp->SetCamera(m_cam);   // 새 줌 반영 후 앵커 재계산.
            const Vec2 anchorWorldAfter = vp->ScreenToWorld(mouseLocal());
            m_cam.center += (anchorWorldBefore - anchorWorldAfter);
        }

        ecs::World* world = ctx.activeWorld();
        const ImU32 kGrid = IM_COL32(255, 255, 255, 28);
        const ImU32 kAxisX = IM_COL32(220, 80, 80, 160);
        const ImU32 kAxisY = IM_COL32(80, 200, 80, 160);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(imgPos, ImVec2(imgPos.x + avail.x, imgPos.y + avail.y), true);

        DrawGrid(dl, vp, imgPos, avail, kGrid, kAxisX, kAxisY);

        // ---- 기즈모(선택 엔티티 이동) — 픽킹보다 먼저 처리해 핸들 드래그가 선택보다 우선 ----
        bool gizmoActive = false;
        if (world) gizmoActive = HandleGizmo(ctx, *world, vp, imgPos, dl, io, hovered);

        // ---- 픽킹(좌클릭, 기즈모 비활성 시) ----
        if (world && hovered && !gizmoActive && ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            const Vec2 worldPt = vp->ScreenToWorld(mouseLocal());
            PickAt(ctx, *world, worldPt, io);
        }

        // 선택 아웃라인.
        if (world) DrawSelectionOutline(ctx, *world, vp, imgPos, dl);

        dl->PopClipRect();

        // 상태 오버레이(좌상단).
        ImGui::SetCursorScreenPos(ImVec2(imgPos.x + 8, imgPos.y + 6));
        ImGui::Text("zoom %.2fx  center (%.1f, %.1f)%s", m_cam.zoom,
                    m_cam.center.x, m_cam.center.y,
                    (ctx.playMode && ctx.playMode->IsPlaying()) ? "  [PLAY]" : "");

        ImGui::End();
    }

    void SerializeState(json::Value& out) const override {
        json::Value::Object o;
        o["type"] = json::Value(std::string("mye.viewport"));
        o["cx"] = json::Value(static_cast<double>(m_cam.center.x));
        o["cy"] = json::Value(static_cast<double>(m_cam.center.y));
        o["zoom"] = json::Value(static_cast<double>(m_cam.zoom));
        out = json::Value(std::move(o));
    }
    void DeserializeState(const json::Value& in) override {
        if (const json::Value* v = in.Find("cx")) m_cam.center.x = static_cast<float>(v->AsDouble());
        if (const json::Value* v = in.Find("cy")) m_cam.center.y = static_cast<float>(v->AsDouble());
        if (const json::Value* v = in.Find("zoom")) m_cam.zoom = static_cast<float>(v->AsDouble());
    }

private:
    ViewportCamera m_cam{};

    // 픽셀 그리드(1 unit 간격) + 원점 축. 화면 밖 라인은 클립됨.
    static void DrawGrid(ImDrawList* dl, IEditorViewport* vp, const ImVec2& origin,
                         const ImVec2& size, ImU32 grid, ImU32 axisX, ImU32 axisY) {
        // 화면 두 코너의 월드 좌표로 가시 범위 산출.
        const Vec2 tl = vp->ScreenToWorld(Vec2{0, 0});
        const Vec2 br = vp->ScreenToWorld(Vec2{size.x, size.y});
        const float minX = std::floor(tl.x < br.x ? tl.x : br.x);
        const float maxX = std::ceil (tl.x > br.x ? tl.x : br.x);
        const float minY = std::floor(tl.y < br.y ? tl.y : br.y);
        const float maxY = std::ceil (tl.y > br.y ? tl.y : br.y);
        // 라인 수 폭주 방지(과도 축소 시 그리드 생략).
        if ((maxX - minX) > 400.0f || (maxY - minY) > 400.0f) return;

        auto toScreen = [&](Vec2 w) {
            const Vec2 s = vp->WorldToScreen(w);
            return ImVec2(origin.x + s.x, origin.y + s.y);
        };
        for (float x = minX; x <= maxX; x += 1.0f) {
            const bool axis = (std::fabs(x) < 0.001f);
            dl->AddLine(toScreen(Vec2{x, minY}), toScreen(Vec2{x, maxY}),
                        axis ? axisY : grid, axis ? 1.5f : 1.0f);
        }
        for (float y = minY; y <= maxY; y += 1.0f) {
            const bool axis = (std::fabs(y) < 0.001f);
            dl->AddLine(toScreen(Vec2{minX, y}), toScreen(Vec2{maxX, y}),
                        axis ? axisX : grid, axis ? 1.5f : 1.0f);
        }
    }

    // 월드 점에서 가장 가까운 스프라이트 엔티티 히트테스트 → 선택.
    static void PickAt(EditorContext& ctx, ecs::World& world, Vec2 worldPt, ImGuiIO& io) {
        ecs::Entity best = ecs::Entity::Null();
        float bestDist = kPickHalfExtent;   // AABB half-extent 내만 후보.

        world.Query<scene::LocalTransform, scene::SpriteRenderer>().Each(
            [&](ecs::Entity e, scene::LocalTransform& lt, scene::SpriteRenderer&) {
                const float dx = std::fabs(worldPt.x - lt.position.x);
                const float dy = std::fabs(worldPt.y - lt.position.y);
                if (dx <= kPickHalfExtent && dy <= kPickHalfExtent) {
                    const float d = dx + dy;
                    if (d <= bestDist) { bestDist = d; best = e; }
                }
            });

        if (!ctx.selection) return;
        if (best.IsNull()) {
            if (!io.KeyCtrl && !io.KeyShift) ctx.selection->Clear();
            return;
        }
        SelectMode mode = SelectMode::Replace;
        if (io.KeyCtrl) mode = SelectMode::Toggle;
        else if (io.KeyShift) mode = SelectMode::Add;
        ctx.selection->Select(SelectableRef::OfEntity(best), mode);
    }

    // 선택 엔티티 외곽 박스.
    static void DrawSelectionOutline(EditorContext& ctx, ecs::World& world, IEditorViewport* vp,
                                     const ImVec2& origin, ImDrawList* dl) {
        if (!ctx.selection) return;
        for (const SelectableRef& r : ctx.selection->Current()) {
            if (!r.IsEntity()) continue;
            ecs::Entity e = r.AsEntity();
            if (!world.Valid(e)) continue;
            scene::LocalTransform* lt = world.TryGet<scene::LocalTransform>(e);
            if (!lt) continue;
            const Vec2 c{lt->position.x, lt->position.y};
            const Vec2 a = vp->WorldToScreen(Vec2{c.x - kPickHalfExtent, c.y + kPickHalfExtent});
            const Vec2 b = vp->WorldToScreen(Vec2{c.x + kPickHalfExtent, c.y - kPickHalfExtent});
            dl->AddRect(ImVec2(origin.x + a.x, origin.y + a.y),
                        ImVec2(origin.x + b.x, origin.y + b.y),
                        IM_COL32(255, 190, 40, 220), 0.0f, 0, 2.0f);
        }
    }

    // 이동 기즈모: primary 선택 엔티티에 X/Y 축 핸들을 그리고 드래그를 커맨드로 반영.
    //   반환 = 이번 프레임 기즈모가 마우스를 소비함(픽킹 억제).
    bool HandleGizmo(EditorContext& ctx, ecs::World& world, IEditorViewport* vp,
                     const ImVec2& origin, ImDrawList* dl, ImGuiIO& io, bool hovered) {
        if (!ctx.selection) return false;
        SelectableRef prim = ctx.selection->Primary();
        if (!prim.IsEntity()) return false;
        ecs::Entity e = prim.AsEntity();
        if (!world.Valid(e)) return false;
        scene::LocalTransform* lt = world.TryGet<scene::LocalTransform>(e);
        if (!lt) return false;

        const Vec2 originWorld{lt->position.x, lt->position.y};
        const Vec2 s = vp->WorldToScreen(originWorld);
        const ImVec2 gizmoScreen(origin.x + s.x, origin.y + s.y);
        constexpr float kAxisLen = 46.0f;   // 핸들 길이(px)
        constexpr float kGrab = 8.0f;       // 그랩 반경(px)

        const ImVec2 xEnd(gizmoScreen.x + kAxisLen, gizmoScreen.y);
        const ImVec2 yEnd(gizmoScreen.x, gizmoScreen.y - kAxisLen);   // +Y 위(화면 위쪽)

        // 축 하이라이트(호버/드래그 중).
        const bool xHot = m_dragAxis == 1 || (m_dragAxis == 0 && NearSegment(io.MousePos, gizmoScreen, xEnd, kGrab));
        const bool yHot = m_dragAxis == 2 || (m_dragAxis == 0 && NearSegment(io.MousePos, gizmoScreen, yEnd, kGrab));
        dl->AddLine(gizmoScreen, xEnd, xHot ? IM_COL32(255, 120, 120, 255) : IM_COL32(220, 60, 60, 255), 3.0f);
        dl->AddLine(gizmoScreen, yEnd, yHot ? IM_COL32(120, 255, 120, 255) : IM_COL32(60, 200, 60, 255), 3.0f);
        dl->AddCircleFilled(gizmoScreen, 4.0f, IM_COL32(240, 240, 240, 255));

        // 드래그 시작.
        if (m_dragAxis == 0 && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (xHot) m_dragAxis = 1;
            else if (yHot) m_dragAxis = 2;
            if (m_dragAxis != 0) {
                m_dragEntity = e;
                m_dragStartPos = lt->position;
                m_dragBeforeBlob = PositionBlob(*lt);
                m_dragStartWorld = vp->ScreenToWorld(
                    Vec2{io.MousePos.x - origin.x, io.MousePos.y - origin.y});
            }
        }

        if (m_dragAxis == 0) return false;   // 이 엔티티에 대한 활성 드래그 없음.

        // 드래그 중: 마우스 월드 델타를 축에 투영해 위치 갱신(라이브 반영).
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && world.Valid(m_dragEntity)) {
            const Vec2 cur = vp->ScreenToWorld(Vec2{io.MousePos.x - origin.x, io.MousePos.y - origin.y});
            const Vec2 dw = cur - m_dragStartWorld;
            scene::LocalTransform* dlt = world.TryGet<scene::LocalTransform>(m_dragEntity);
            if (dlt) {
                Vec3 p = m_dragStartPos;
                if (m_dragAxis == 1) p.x = m_dragStartPos.x + dw.x;
                else if (m_dragAxis == 2) p.y = m_dragStartPos.y + dw.y;
                if (io.KeyCtrl) {   // 스냅 0.25u.
                    p.x = std::round(p.x / 0.25f) * 0.25f;
                    p.y = std::round(p.y / 0.25f) * 0.25f;
                }
                dlt->position = p;
                dlt->dirty = true;
            }
            return true;
        }

        // 드래그 종료: PropertyEditCommand로 before→after 커밋(라이브 값은 이미 반영됨).
        //   커맨드 Execute가 after를 다시 적용하므로 결과는 동일(경로 왕복 검증 겸).
        if (world.Valid(m_dragEntity)) {
            scene::LocalTransform* dlt = world.TryGet<scene::LocalTransform>(m_dragEntity);
            if (dlt) {
                ValueBlob after = PositionBlob(*dlt);
                if (!m_dragBeforeBlob.Empty() && m_dragBeforeBlob.json != after.json) {
                    if (CommandStack* stk = ActiveStack(ctx)) {
                        if (const refl::TypeInfo* t = LocalTransformType()) {
                            stk->Push(std::make_unique<PropertyEditCommand>(
                                ObjectRef::Component(m_dragEntity, *t), PositionPath(),
                                m_dragBeforeBlob, after, std::string("이동")));
                        }
                    }
                }
            }
        }
        m_dragAxis = 0;
        m_dragEntity = ecs::Entity::Null();
        m_dragBeforeBlob = ValueBlob{};
        return true;
    }

    // 점 p가 선분 [a,b]로부터 tol 픽셀 이내인가.
    static bool NearSegment(ImVec2 p, ImVec2 a, ImVec2 b, float tol) {
        const float vx = b.x - a.x, vy = b.y - a.y;
        const float wx = p.x - a.x, wy = p.y - a.y;
        const float len2 = vx * vx + vy * vy;
        float t = len2 > 0.0f ? (wx * vx + wy * vy) / len2 : 0.0f;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        const float cx = a.x + t * vx, cy = a.y + t * vy;
        const float dx = p.x - cx, dy = p.y - cy;
        return (dx * dx + dy * dy) <= tol * tol;
    }

    // 기즈모 드래그 상태.
    int         m_dragAxis = 0;   // 0=없음, 1=X, 2=Y
    ecs::Entity m_dragEntity = ecs::Entity::Null();
    Vec3        m_dragStartPos{};
    Vec2        m_dragStartWorld{};
    ValueBlob   m_dragBeforeBlob{};
};

// -----------------------------------------------------------------------------
class ViewportPanelFactory final : public IEditorPanelFactory {
public:
    const PanelDesc& Desc() const override { return kViewportDesc; }
    std::unique_ptr<IEditorPanel> Create() override {
        return std::make_unique<ViewportPanel>();
    }
};

std::unique_ptr<IEditorPanelFactory> MakeViewportPanelFactory() {
    return std::make_unique<ViewportPanelFactory>();
}

} // namespace mye::editor
