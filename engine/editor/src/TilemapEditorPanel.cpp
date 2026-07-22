// TilemapEditorPanel.cpp — 타일맵 인플레이스 편집 패널 + 팔레트 패널 (docs/07 §타일맵)
//
// 07: 타일맵 편집은 씬 뷰포트 인플레이스 편집 모드로 동작. 이 파일은 두 패널을 제공한다:
//   - TilemapEditorPanel(mye.tilemap): 편집 모드 토글·도구/모드 선택·스트로크 커밋(TilePaintCommand)·
//     레이어 관리·다리 편집·오토타일 브러시. 실제 뷰포트 상 드래그 입력은 ViewportPanel 훅이
//     생기면 그쪽에서 셀 좌표를 받아 여기 TileEditSession으로 위임한다(현재는 좌표 입력 UI 제공).
//   - TilePalettePanel(mye.tilemap.palette): 타일셋 시트에서 타일 선택·오토타일 룰셋 선택.
//
// 모든 셀 편집은 TileEditSession이 모아 TilePaintCommand 하나로 커밋한다(스트로크=Undo 1회).
// 타일맵 데이터 변경은 03 TilemapWorld API로만(에디터는 UI·커맨드만). 내장=1급 플러그인 규약.
#include "mye/editor/Panel.h"
#include "mye/editor/EditorContext.h"
#include "mye/editor/Selection.h"
#include "mye/editor/Command.h"
#include "mye/editor/CommandStack.h"
#include "mye/editor/PlayMode.h"
#include "mye/editor/TileEditing.h"
#include "mye/editor/BuiltinPanels.h"

#include "mye/ecs/World.h"
#include "mye/scene/Renderable.h"

#include "imgui.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace mye::editor {

namespace tm = mye::tilemap;

// ===========================================================================
// TileEditSession — 두 패널이 공유하는 편집 상태(도구·모드·활성 타일·룰셋·스트로크 버퍼).
//   ConsolePanel의 공유 로그 저장소처럼 프로세스 전역 1개(에디터 단일 문서 편집 가정).
// ===========================================================================
struct TileEditSession {
    bool         active = false;          // 인플레이스 편집 모드 on/off
    TileTool     tool = TileTool::Brush;
    TileEditMode mode = TileEditMode::Paint;
    uint8_t      activeLayer = 0;
    tm::TileId   activeTile = 1;          // 팔레트 선택 타일
    int          brushHeight = 1;         // Height 모드 증감량
    tm::SlopeType brushSlope = tm::SlopeType::GentleN;
    uint8_t      brushFlags = tm::ColumnFlags::Solid;  // Collision 모드 토글 플래그
    bool         bridgeMode = false;      // true=편집이 다리(추가 컬럼)를 대상으로

    std::vector<AutotileRuleSet> rulesets; // 오토타일 룰셋(TilesetAsset 미존재 시 세션 보유)
    int          activeRuleset = -1;       // -1=오토타일 미사용

    // 진행 중 스트로크의 셀 편집 누적(같은 셀 재편집은 첫 before 유지).
    std::vector<TileCellEdit> strokeEdits;
    bool                      stroking = false;

    static TileEditSession& Get() {
        static TileEditSession s;
        return s;
    }

    void BeginStroke() { strokeEdits.clear(); stroking = true; }

    // 스트로크 중 셀 편집 기록. before는 첫 기록만 캡처(같은 셀 반복 페인트 대비).
    TileCellEdit& TouchCell(const tm::TilemapWorld& map, uint8_t layer, Vec2i cell) {
        for (TileCellEdit& e : strokeEdits)
            if (e.layer == layer && e.cell == cell) return e;
        TileCellEdit e;
        e.layer = layer;
        e.cell = cell;
        e.before = SnapshotCell(map, layer, cell);
        e.after = e.before;
        strokeEdits.push_back(std::move(e));
        return strokeEdits.back();
    }
};

// 활성 룰셋 포인터(없으면 nullptr).
static const AutotileRuleSet* ActiveRuleSet(const TileEditSession& s) {
    if (s.activeRuleset < 0 || s.activeRuleset >= static_cast<int>(s.rulesets.size()))
        return nullptr;
    return &s.rulesets[static_cast<size_t>(s.activeRuleset)];
}

// ---------------------------------------------------------------------------
// 편집 프리미티브 — 세션 스트로크 버퍼에 한 셀의 after를 계산해 반영(모드별). 커밋은 별도.
// ---------------------------------------------------------------------------
namespace tile_ops {

// Paint: 활성 타일로 base 컬럼 교체(기존 height/slope/flags 보존은 하지 않고 새 타일 배치).
void PaintCell(TileEditSession& s, tm::TilemapWorld& map, uint8_t layer, Vec2i cell) {
    TileCellEdit& e = s.TouchCell(map, layer, cell);
    tm::TileColumn col;
    col.tile = s.activeTile;
    col.layer = layer;
    if (s.bridgeMode) {
        col.flags |= tm::ColumnFlags::Bridge;
        e.after.push_back(col);   // 다리는 추가 컬럼
    } else {
        // 지면 base 컬럼(첫 컬럼)만 교체, 나머지(다리) 보존.
        if (e.after.empty()) e.after.push_back(col);
        else e.after[0] = col;
    }
}

void EraseCell(TileEditSession& s, tm::TilemapWorld& map, uint8_t layer, Vec2i cell) {
    TileCellEdit& e = s.TouchCell(map, layer, cell);
    if (s.bridgeMode && e.after.size() > 1) e.after.pop_back();   // 다리 컬럼만 제거
    else e.after.clear();
}

void HeightCell(TileEditSession& s, tm::TilemapWorld& map, uint8_t layer, Vec2i cell) {
    TileCellEdit& e = s.TouchCell(map, layer, cell);
    if (e.after.empty()) return;
    e.after[0].baseHeightLevel += s.brushHeight;
}

void SlopeCell(TileEditSession& s, tm::TilemapWorld& map, uint8_t layer, Vec2i cell) {
    TileCellEdit& e = s.TouchCell(map, layer, cell);
    if (e.after.empty()) return;
    e.after[0].slope = s.brushSlope;
}

void CollisionCell(TileEditSession& s, tm::TilemapWorld& map, uint8_t layer, Vec2i cell) {
    TileCellEdit& e = s.TouchCell(map, layer, cell);
    if (e.after.empty()) return;
    e.after[0].flags ^= s.brushFlags;   // 토글
}

// 오토타일: 코어 타일을 놓고, 셀+8이웃의 마스크를 재평가해 변형 타일로 갱신.
void AutotileCell(TileEditSession& s, tm::TilemapWorld& map, uint8_t layer, Vec2i cell) {
    const AutotileRuleSet* rs = ActiveRuleSet(s);
    if (!rs) { PaintCell(s, map, layer, cell); return; }

    // "같은 터레인" 판정 = base 컬럼 tile 이 terrainTile 또는 룰이 배치하는 타일 집합.
    // 간이 판정: 코어를 먼저 놓고, 대상 셀 + 8이웃을 재평가.
    auto placeCore = [&](Vec2i c) {
        TileCellEdit& e = s.TouchCell(map, layer, c);
        if (e.after.empty()) { tm::TileColumn col; col.layer = layer; e.after.push_back(col); }
        e.after[0].tile = rs->terrainTile;
    };
    placeCore(cell);

    auto isSameTerrain = [&](Vec2i c) -> bool {
        // 스트로크 버퍼 우선(이번에 놓은 것), 없으면 맵 조회.
        for (const TileCellEdit& e : s.strokeEdits)
            if (e.layer == layer && e.cell == c)
                return !e.after.empty() && e.after[0].tile != tm::kNoTile;
        std::span<const tm::TileColumn> cols = map.Layer(layer) ? map.Layer(layer)->ColumnsAt(c)
                                                                : std::span<const tm::TileColumn>{};
        return !cols.empty() && cols[0].tile != tm::kNoTile;
    };

    auto reeval = [&](Vec2i c) {
        if (!isSameTerrain(c)) return;
        const uint8_t mask = AutotileRuleSet::BuildMask(
            [&](int dx, int dy) { return isSameTerrain(Vec2i{c.x + dx, c.y + dy}); });
        const tm::TileId t = rs->Evaluate(mask);
        TileCellEdit& e = s.TouchCell(map, layer, c);
        if (e.after.empty()) { tm::TileColumn col; col.layer = layer; e.after.push_back(col); }
        e.after[0].tile = t;
    };
    // 중심 + 8이웃 재평가(인접 셀 갱신).
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            reeval(Vec2i{cell.x + dx, cell.y + dy});
}

// 모드/도구 디스패치.
void ApplyAt(TileEditSession& s, tm::TilemapWorld& map, Vec2i cell) {
    const uint8_t layer = s.activeLayer;
    if (s.tool == TileTool::Eraser) { EraseCell(s, map, layer, cell); return; }
    if (s.tool == TileTool::Autotile) { AutotileCell(s, map, layer, cell); return; }
    switch (s.mode) {
        case TileEditMode::Paint:     PaintCell(s, map, layer, cell); break;
        case TileEditMode::Height:    HeightCell(s, map, layer, cell); break;
        case TileEditMode::Slope:     SlopeCell(s, map, layer, cell); break;
        case TileEditMode::Collision: CollisionCell(s, map, layer, cell); break;
    }
}

} // namespace tile_ops

// TileColumn 값 동등(scene 모듈에 operator== 없어 에디터 로컬 비교 — 데이터 필드 전체 비교).
static bool ColumnEqual(const tm::TileColumn& a, const tm::TileColumn& b) {
    return a.tile == b.tile && a.baseHeightLevel == b.baseHeightLevel &&
           a.slope == b.slope && a.flags == b.flags && a.layer == b.layer;
}
static bool CellsEqual(const std::vector<tm::TileColumn>& a,
                       const std::vector<tm::TileColumn>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (!ColumnEqual(a[i], b[i])) return false;
    return true;
}

// 스트로크를 TilePaintCommand로 커밋(변경 없는 셀 제외).
static void CommitStroke(EditorContext& ctx, TileEditSession& s, tm::TilemapWorld* map) {
    s.stroking = false;
    if (!map || s.strokeEdits.empty()) { s.strokeEdits.clear(); return; }
    std::vector<TileCellEdit> real;
    for (TileCellEdit& e : s.strokeEdits)
        if (!CellsEqual(e.before, e.after)) real.push_back(std::move(e));
    s.strokeEdits.clear();
    if (real.empty()) return;

    CommandStack* stack = (ctx.playMode && ctx.playMode->IsPlaying())
                              ? ctx.playMode->PlayCommandStack()
                              : ctx.commands;
    if (!stack) return;
    stack->Push(std::make_unique<TilePaintCommand>(map, std::move(real)));
}

namespace {

const PanelDesc kTilemapDesc{
    /*id*/ "mye.tilemap",
    /*title*/ "타일맵 편집",
    /*allowMultiple*/ false,
    /*defaultDock*/ DockSlot::Right,
};
const PanelDesc kPaletteDesc{
    /*id*/ "mye.tilemap.palette",
    /*title*/ "타일 팔레트",
    /*allowMultiple*/ false,
    /*defaultDock*/ DockSlot::Right,
};

// 선택된 엔티티의 TilemapRenderer 런타임 인스턴스(편집 대상). 없으면 nullptr.
tm::TilemapWorld* ActiveTilemap(EditorContext& ctx) {
    ecs::World* world = ctx.activeWorld();
    if (!world || !ctx.selection) return nullptr;
    for (const SelectableRef& r : ctx.selection->Current()) {
        if (!r.IsEntity()) continue;
        ecs::Entity e = r.AsEntity();
        if (!world->Valid(e)) continue;
        if (auto* tr = world->TryGet<scene::TilemapRenderer>(e))
            if (tr->runtime) return tr->runtime;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// TilemapEditorPanel
// ---------------------------------------------------------------------------
class TilemapEditorPanel final : public IEditorPanel {
public:
    const PanelDesc& Desc() const override { return kTilemapDesc; }

    void OnGui(EditorContext& ctx) override {
        if (!ImGui::Begin("타일맵 편집")) { ImGui::End(); return; }
        TileEditSession& s = TileEditSession::Get();
        tm::TilemapWorld* map = ActiveTilemap(ctx);

        if (!map) {
            ImGui::TextDisabled("타일맵 노드를 선택하세요");
            ImGui::End();
            return;
        }

        ImGui::Checkbox("편집 모드", &s.active);
        ImGui::SameLine();
        if (ImGui::SmallButton("완료 (Esc)")) s.active = false;

        ImGui::Separator();
        DrawTools(s);
        ImGui::Separator();
        DrawModes(s);
        ImGui::Separator();
        DrawLayers(s, *map);
        ImGui::Separator();
        DrawManualPaint(ctx, s, map);

        ImGui::End();
    }

    void SerializeState(json::Value& out) const override {
        json::Value::Object o;
        o["type"] = json::Value(std::string("mye.tilemap"));
        out = json::Value(std::move(o));
    }

private:
    static void DrawTools(TileEditSession& s) {
        ImGui::TextUnformatted("도구:");
        struct T { TileTool tool; const char* label; };
        static const T kTools[] = {
            {TileTool::Brush, "브러시"}, {TileTool::Rect, "사각형"},
            {TileTool::Fill, "채우기"}, {TileTool::Eraser, "지우개"},
            {TileTool::Eyedropper, "스포이드"}, {TileTool::Autotile, "오토타일"},
        };
        for (const T& t : kTools) {
            const bool sel = s.tool == t.tool;
            if (ImGui::RadioButton(t.label, sel)) s.tool = t.tool;
            ImGui::SameLine();
        }
        ImGui::NewLine();
    }

    static void DrawModes(TileEditSession& s) {
        ImGui::TextUnformatted("모드:");
        struct M { TileEditMode mode; const char* label; };
        static const M kModes[] = {
            {TileEditMode::Paint, "Paint"}, {TileEditMode::Height, "Height"},
            {TileEditMode::Slope, "Slope"}, {TileEditMode::Collision, "Collision"},
        };
        for (const M& m : kModes) {
            if (ImGui::RadioButton(m.label, s.mode == m.mode)) s.mode = m.mode;
            ImGui::SameLine();
        }
        ImGui::NewLine();

        if (s.mode == TileEditMode::Height)
            ImGui::InputInt("높이 증감", &s.brushHeight);
        if (s.mode == TileEditMode::Collision) {
            int flags = s.brushFlags;
            ImGui::CheckboxFlags("Solid", &flags, tm::ColumnFlags::Solid);
            ImGui::CheckboxFlags("Trigger", &flags, tm::ColumnFlags::EventTrigger);
            ImGui::CheckboxFlags("OneWayBridge", &flags, tm::ColumnFlags::OneWayBridge);
            s.brushFlags = static_cast<uint8_t>(flags);
        }
        ImGui::Checkbox("다리 편집(추가 컬럼)", &s.bridgeMode);
    }

    static void DrawLayers(TileEditSession& s, tm::TilemapWorld& map) {
        ImGui::Text("레이어: %d (총 %zu)", static_cast<int>(s.activeLayer), map.LayerCount());
        int lyr = static_cast<int>(s.activeLayer);
        if (ImGui::InputInt("활성 레이어", &lyr)) {
            if (lyr < 0) lyr = 0;
            if (lyr > 255) lyr = 255;
            s.activeLayer = static_cast<uint8_t>(lyr);
        }
        if (ImGui::SmallButton("+ 레이어")) map.GetOrCreateLayer(s.activeLayer);
    }

    // 뷰포트 인플레이스 드래그 훅이 없는 동안의 대체 입력: 셀 좌표를 직접 입력해 스트로크 커밋.
    //   ViewportPanel 훅이 추가되면 이 블록 대신 그쪽에서 tile_ops::ApplyAt를 호출한다(notes 참조).
    static void DrawManualPaint(EditorContext& ctx, TileEditSession& s, tm::TilemapWorld* map) {
        ImGui::TextUnformatted("좌표 편집(뷰포트 훅 대체):");
        static int cx = 0, cy = 0, w = 1, h = 1;
        ImGui::InputInt("cellX", &cx);
        ImGui::InputInt("cellY", &cy);
        ImGui::InputInt("width", &w);
        ImGui::InputInt("height", &h);
        if (ImGui::Button("영역 적용(스트로크)")) {
            s.BeginStroke();
            for (int dy = 0; dy < std::max(1, h); ++dy)
                for (int dx = 0; dx < std::max(1, w); ++dx)
                    tile_ops::ApplyAt(s, *map, Vec2i{cx + dx, cy + dy});
            CommitStroke(ctx, s, map);
        }
    }
};

// ---------------------------------------------------------------------------
// TilePalettePanel
// ---------------------------------------------------------------------------
class TilePalettePanel final : public IEditorPanel {
public:
    const PanelDesc& Desc() const override { return kPaletteDesc; }

    void OnGui(EditorContext&) override {
        if (!ImGui::Begin("타일 팔레트")) { ImGui::End(); return; }
        TileEditSession& s = TileEditSession::Get();

        int tile = static_cast<int>(s.activeTile);
        if (ImGui::InputInt("활성 타일 id", &tile)) {
            if (tile < 0) tile = 0;
            s.activeTile = static_cast<tm::TileId>(tile);
        }

        ImGui::Separator();
        ImGui::TextUnformatted("오토타일 룰셋:");
        if (ImGui::SmallButton("+ 룰셋 추가")) {
            AutotileRuleSet rs;
            rs.name = "terrain_" + std::to_string(s.rulesets.size());
            rs.terrainTile = s.activeTile;
            s.rulesets.push_back(std::move(rs));
        }
        for (int i = 0; i < static_cast<int>(s.rulesets.size()); ++i) {
            ImGui::PushID(i);
            const bool sel = s.activeRuleset == i;
            if (ImGui::RadioButton(s.rulesets[static_cast<size_t>(i)].name.c_str(), sel))
                s.activeRuleset = sel ? -1 : i;
            ImGui::PopID();
        }
        if (s.rulesets.empty())
            ImGui::TextDisabled("(룰셋 없음 — 오토타일은 활성 타일 페인트로 폴백)");

        // 간이 시트 프리뷰(타일 id 격자 버튼) — 실제 텍스처 프리뷰는 팔레트 이미지 훅 후속.
        ImGui::Separator();
        ImGui::TextUnformatted("타일 시트(간이):");
        for (int i = 1; i <= 32; ++i) {
            ImGui::PushID(i);
            const bool sel = s.activeTile == static_cast<tm::TileId>(i);
            if (ImGui::Selectable(std::to_string(i).c_str(), sel, 0, ImVec2(28, 28)))
                s.activeTile = static_cast<tm::TileId>(i);
            ImGui::PopID();
            if (i % 8 != 0) ImGui::SameLine();
        }

        ImGui::End();
    }

    void SerializeState(json::Value& out) const override {
        json::Value::Object o;
        o["type"] = json::Value(std::string("mye.tilemap.palette"));
        out = json::Value(std::move(o));
    }
};

template <class P>
class PanelFactory final : public IEditorPanelFactory {
public:
    explicit PanelFactory(const PanelDesc& d) : m_desc(d) {}
    const PanelDesc& Desc() const override { return m_desc; }
    std::unique_ptr<IEditorPanel> Create() override { return std::make_unique<P>(); }
private:
    const PanelDesc& m_desc;
};

} // namespace

std::unique_ptr<IEditorPanelFactory> MakeTilemapEditorPanelFactory() {
    return std::make_unique<PanelFactory<TilemapEditorPanel>>(kTilemapDesc);
}
std::unique_ptr<IEditorPanelFactory> MakeTilePalettePanelFactory() {
    return std::make_unique<PanelFactory<TilePalettePanel>>(kPaletteDesc);
}

} // namespace mye::editor
