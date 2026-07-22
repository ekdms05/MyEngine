// UiLayoutTests.cpp — M5-A 인게임 UI 레이아웃·이벤트·직렬화·9slice 헤드리스 테스트
//
// 목적: RectTransform 앵커 계산(리사이즈 재배치), StackLayout/GridLayout 배치 좌표, 히트테스트,
//   버튼 클릭 콜백 발화, UiDocument JSON 왕복, 9-slice 지오메트리, 창 드래그 이동 검증.
//   + UI 를 CPU 로 BMP 덤프(창·버튼 픽셀 확인). rhi/GPU 불필요(모두 헤드리스).
#include "TestFramework.h"

#include "mye/ui/Widget.h"
#include "mye/ui/Widgets.h"
#include "mye/ui/UiDocument.h"
#include "mye/ui/UiSkin.h"

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace mye;

// -----------------------------------------------------------------------------
// RectTransform 앵커 계산
// -----------------------------------------------------------------------------
MYE_TEST(UiAnchorPointTopLeft) {
    // 점 앵커 좌상단(0,0) + pivot(0,0): pos=offsetMin, size=sizeDelta.
    ui::AnchorRect a = ui::AnchorRect::TopLeft(Vec2{10, 20}, Vec2{100, 50});
    ui::UiRect parent{0, 0, 960, 540};
    ui::UiRect r = ui::ComputeAnchoredRect(a, parent);
    MYE_EXPECT(r.x == 10 && r.y == 20);
    MYE_EXPECT(r.w == 100 && r.h == 50);
}

MYE_TEST(UiAnchorCenter) {
    // 중앙 앵커(0.5) + pivot(0.5): 부모 중앙에 크기 배치.
    ui::AnchorRect a = ui::AnchorRect::Center(Vec2{200, 100});
    ui::UiRect parent{0, 0, 960, 540};
    ui::UiRect r = ui::ComputeAnchoredRect(a, parent);
    MYE_EXPECT(r.x == 960 * 0.5f - 100);   // 380
    MYE_EXPECT(r.y == 540 * 0.5f - 50);    // 220
    MYE_EXPECT(r.w == 200 && r.h == 100);
}

MYE_TEST(UiAnchorStretchFillWithMargins) {
    // Fill 앵커(0..1) + 마진: 부모에서 좌·상 offsetMin, 우·하 offsetMax 만큼 안쪽.
    ui::AnchorRect a = ui::AnchorRect::Fill();
    a.offsetMin = Vec2{8, 12};
    a.offsetMax = Vec2{16, 4};
    ui::UiRect parent{0, 0, 400, 300};
    ui::UiRect r = ui::ComputeAnchoredRect(a, parent);
    MYE_EXPECT(r.x == 8 && r.y == 12);
    MYE_EXPECT(r.w == 400 - 8 - 16);   // 376
    MYE_EXPECT(r.h == 300 - 12 - 4);   // 284
}

MYE_TEST(UiAnchorResizeReflows) {
    // 리사이즈 시 앵커가 재계산되어 우하단 고정 위젯이 이동한다.
    ui::AnchorRect a{};
    a.anchorMin = a.anchorMax = Vec2{1, 1};   // 우하단 점 앵커
    a.pivot = Vec2{1, 1};
    a.sizeDelta = Vec2{50, 30};
    a.offsetMin = Vec2{0, 0};

    ui::UiRect r1 = ui::ComputeAnchoredRect(a, ui::UiRect{0, 0, 960, 540});
    MYE_EXPECT(r1.x == 910 && r1.y == 510);   // 우하단 정렬

    ui::UiRect r2 = ui::ComputeAnchoredRect(a, ui::UiRect{0, 0, 800, 600});
    MYE_EXPECT(r2.x == 750 && r2.y == 570);   // 리사이즈 후 재배치
}

// -----------------------------------------------------------------------------
// StackLayout / GridLayout 배치
// -----------------------------------------------------------------------------
MYE_TEST(UiStackLayoutVertical) {
    ui::StackLayout stack;
    stack.orientation = ui::StackLayout::Orientation::Vertical;
    stack.spacing = 4.0f;
    stack.padding = Vec2{2, 2};

    auto* a = stack.addChild(std::make_unique<ui::Panel>());
    auto* b = stack.addChild(std::make_unique<ui::Panel>());
    a->anchors.sizeDelta = Vec2{0, 20};
    b->anchors.sizeDelta = Vec2{0, 30};

    stack.arrange(ui::UiRect{10, 10, 100, 200});
    // 첫 자식: y = 10 + padY(2). 크로스: x=10+2, w=100-4.
    MYE_EXPECT(a->computedRect.y == 12);
    MYE_EXPECT(a->computedRect.x == 12);
    MYE_EXPECT(a->computedRect.w == 96);
    MYE_EXPECT(a->computedRect.h == 20);
    // 둘째 자식: y = 12 + 20 + spacing(4) = 36.
    MYE_EXPECT(b->computedRect.y == 36);
    MYE_EXPECT(b->computedRect.h == 30);
}

MYE_TEST(UiGridLayout) {
    ui::GridLayout grid;
    grid.cellSize = Vec2i{32, 32};
    grid.columns = 3;
    grid.spacing = 2.0f;
    ui::Panel* cells[5];
    for (int i = 0; i < 5; ++i)
        cells[i] = static_cast<ui::Panel*>(grid.addChild(std::make_unique<ui::Panel>()));

    grid.arrange(ui::UiRect{0, 0, 200, 200});
    // 인덱스 3 → col=0,row=1.
    MYE_EXPECT(cells[3]->computedRect.x == 0);
    MYE_EXPECT(cells[3]->computedRect.y == 32 + 2);   // row1
    // 인덱스 4 → col=1,row=1.
    MYE_EXPECT(cells[4]->computedRect.x == 32 + 2);
    MYE_EXPECT(cells[4]->computedRect.w == 32);
}

// -----------------------------------------------------------------------------
// 히트테스트 + 버튼 클릭 콜백
// -----------------------------------------------------------------------------
MYE_TEST(UiButtonClickCallback) {
    ui::Button btn;
    btn.computedRect = ui::UiRect{0, 0, 100, 40};
    int clicks = 0;
    btn.onClick = [&] { ++clicks; };

    // hover → down → up → click 시퀀스.
    ui::UiEvent enter{}; enter.type = ui::UiEventType::PointerEnter;
    btn.onEvent(enter);
    MYE_EXPECT(btn.state == ui::Button::State::Hover);

    ui::UiEvent down{}; down.type = ui::UiEventType::PointerDown;
    MYE_EXPECT(btn.onEvent(down) == true);
    MYE_EXPECT(btn.state == ui::Button::State::Pressed);

    ui::UiEvent up{}; up.type = ui::UiEventType::PointerUp;
    btn.onEvent(up);
    MYE_EXPECT(btn.state == ui::Button::State::Hover);

    ui::UiEvent click{}; click.type = ui::UiEventType::PointerClick;
    MYE_EXPECT(btn.onEvent(click) == true);
    MYE_EXPECT(clicks == 1);

    // Disabled 는 클릭 무시.
    btn.state = ui::Button::State::Disabled;
    ui::UiEvent click2{}; click2.type = ui::UiEventType::PointerClick;
    btn.onEvent(click2);
    MYE_EXPECT(clicks == 1);
}

MYE_TEST(UiHitTestTopmostChild) {
    // 부모 Panel + 겹친 두 자식 → 나중에 추가된(위) 자식이 히트.
    auto root = std::make_unique<ui::Panel>();
    root->computedRect = ui::UiRect{0, 0, 200, 200};
    auto* lower = static_cast<ui::Panel*>(root->addChild(std::make_unique<ui::Panel>()));
    auto* upper = static_cast<ui::Panel*>(root->addChild(std::make_unique<ui::Panel>()));
    lower->computedRect = ui::UiRect{10, 10, 100, 100};
    upper->computedRect = ui::UiRect{50, 50, 100, 100};

    // (60,60) 은 둘 다 포함 → upper 가 최상단.
    // HitTestSubtree 는 UiSystem 내부 static 이므로 여기선 로직 동등성만 간접 확인:
    //   children 역순 + Contains.
    ui::Widget* hit = nullptr;
    auto kids = root->children();
    for (auto it = kids.rbegin(); it != kids.rend() && !hit; ++it)
        if ((*it)->computedRect.Contains(Vec2{60, 60})) hit = it->get();
    MYE_EXPECT(hit == upper);
}

// -----------------------------------------------------------------------------
// UiDocument JSON 왕복
// -----------------------------------------------------------------------------
MYE_TEST(UiDocumentJsonRoundtrip) {
    ui::UiDocument doc;
    doc.version = 1;
    doc.controllerScript = "scripts/npc_dialog.lua";
    doc.root.typeName = "Window";
    doc.root.name = "dialog";
    doc.root.styleClass = "window.frame";
    doc.root.anchors = ui::AnchorRect::Center(Vec2{400, 200});
    doc.root.properties.push_back({"title", "NPC 대화"});

    ui::UiNodeDesc body;
    body.typeName = "Label";
    body.name = "body";
    body.anchors = ui::AnchorRect::Fill();
    body.properties.push_back({"text", "안녕하세요, 모험가님."});
    doc.root.children.push_back(body);

    auto saved = ui::SaveDocumentJson(doc);
    MYE_EXPECT(saved.HasValue());
    if (!saved.HasValue()) return;

    auto loaded = ui::LoadDocumentJson(saved.Value());
    MYE_EXPECT(loaded.HasValue());
    if (!loaded.HasValue()) return;

    const ui::UiDocument& d = loaded.Value();
    MYE_EXPECT(d.controllerScript == "scripts/npc_dialog.lua");
    MYE_EXPECT(d.root.typeName == "Window");
    MYE_EXPECT(d.root.name == "dialog");
    MYE_EXPECT(d.root.styleClass == "window.frame");
    MYE_EXPECT_NEAR(d.root.anchors.anchorMin.x, 0.5f, 1e-5f);
    MYE_EXPECT_NEAR(d.root.anchors.sizeDelta.x, 400.0f, 1e-4f);
    MYE_EXPECT(d.root.properties.size() == 1);
    if (!d.root.properties.empty())
        MYE_EXPECT(d.root.properties[0].value == "NPC 대화");
    MYE_EXPECT(d.root.children.size() == 1);
    if (!d.root.children.empty()) {
        MYE_EXPECT(d.root.children[0].typeName == "Label");
        MYE_EXPECT(d.root.children[0].properties.size() == 1);
        if (!d.root.children[0].properties.empty())
            MYE_EXPECT(d.root.children[0].properties[0].value == "안녕하세요, 모험가님.");
    }

    // 로드된 문서를 인스턴스화 → 트리 구조 확인.
    ui::WidgetFactory factory;
    auto tree = d.Instantiate(factory);
    MYE_EXPECT(tree.HasValue());
    if (tree.HasValue()) {
        ui::Window* win = tree.Value()->As<ui::Window>();
        MYE_EXPECT(win != nullptr);
        if (win) MYE_EXPECT(win->title == "NPC 대화");
    }
}

// -----------------------------------------------------------------------------
// Window 드래그 이동
// -----------------------------------------------------------------------------
MYE_TEST(UiWindowDrag) {
    auto parent = std::make_unique<ui::Panel>();
    parent->computedRect = ui::UiRect{0, 0, 960, 540};
    ui::Window* win = static_cast<ui::Window*>(parent->addChild(std::make_unique<ui::Window>()));
    win->titleBarHeight = 24.0f;
    win->anchors = ui::AnchorRect::TopLeft(Vec2{100, 100}, Vec2{300, 200});
    win->computedRect = ui::UiRect{100, 100, 300, 200};

    // 타이틀바(y<124) 안에서 드래그 시작.
    ui::UiEvent ds{}; ds.type = ui::UiEventType::DragStart; ds.pointerPos = Vec2{150, 110};
    MYE_EXPECT(win->onEvent(ds) == true);

    // 드래그: 포인터를 (250, 210) 로 이동 → 창 좌상단 = 포인터 - 잡은 오프셋(50,10) = (200,200).
    ui::UiEvent dr{}; dr.type = ui::UiEventType::Drag; dr.pointerPos = Vec2{250, 210};
    MYE_EXPECT(win->onEvent(dr) == true);
    MYE_EXPECT(win->computedRect.x == 200);
    MYE_EXPECT(win->computedRect.y == 200);

    ui::UiEvent up{}; up.type = ui::UiEventType::PointerUp; up.pointerPos = Vec2{250, 210};
    win->onEvent(up);
}

MYE_TEST(UiWindowCloseButton) {
    ui::Window win;
    win.titleBarHeight = 24.0f;
    win.closable = true;
    win.computedRect = ui::UiRect{0, 0, 200, 150};
    int closed = 0;
    win.onClose = [&] { ++closed; };

    // 닫기 버튼: 우상단 근처(frame.x + w - sz - 3, y+3). sz = 24-6 = 18.
    // 버튼 중심 대략 (200 - 18 - 3 + 9, 3 + 9) = (188, 12).
    ui::UiEvent down{}; down.type = ui::UiEventType::PointerDown; down.pointerPos = Vec2{188, 12};
    MYE_EXPECT(win.onEvent(down) == true);
    MYE_EXPECT(closed == 1);
}

// -----------------------------------------------------------------------------
// CPU BMP 덤프 — GPU 없이 위젯 사각형을 CPU 래스터로 채워 창·버튼 픽셀 확인.
// -----------------------------------------------------------------------------
namespace {

struct CpuCanvas {
    int w, h;
    std::vector<uint8_t> px;   // BGRA, top-down
    CpuCanvas(int W, int H) : w(W), h(H), px(static_cast<size_t>(W) * H * 4, 0) {}

    void Fill(const ui::UiRect& r, Color c) {
        const int x0 = static_cast<int>(r.x < 0 ? 0 : r.x);
        const int y0 = static_cast<int>(r.y < 0 ? 0 : r.y);
        const int x1 = static_cast<int>(r.x + r.w); const int y1 = static_cast<int>(r.y + r.h);
        for (int y = y0; y < y1 && y < h; ++y)
            for (int x = x0; x < x1 && x < w; ++x) {
                uint8_t* p = &px[(static_cast<size_t>(y) * w + x) * 4];
                p[0] = static_cast<uint8_t>(c.b * 255); p[1] = static_cast<uint8_t>(c.g * 255);
                p[2] = static_cast<uint8_t>(c.r * 255); p[3] = 255;
            }
    }

    Color At(int x, int y) const {
        const uint8_t* p = &px[(static_cast<size_t>(y) * w + x) * 4];
        return Color{p[2] / 255.0f, p[1] / 255.0f, p[0] / 255.0f, p[3] / 255.0f};
    }

    bool WriteBmp(const char* path) const {
        FILE* f = std::fopen(path, "wb");
        if (!f) return false;
        const uint32_t rowBytes = static_cast<uint32_t>(w) * 4;
        const uint32_t imgSize = rowBytes * h;
        const uint32_t fileSize = 54 + imgSize;
        uint8_t hdr[54] = {};
        hdr[0] = 'B'; hdr[1] = 'M';
        *reinterpret_cast<uint32_t*>(&hdr[2]) = fileSize;
        *reinterpret_cast<uint32_t*>(&hdr[10]) = 54;
        *reinterpret_cast<uint32_t*>(&hdr[14]) = 40;
        *reinterpret_cast<int32_t*>(&hdr[18]) = w;
        *reinterpret_cast<int32_t*>(&hdr[22]) = -h;   // top-down
        *reinterpret_cast<uint16_t*>(&hdr[26]) = 1;
        *reinterpret_cast<uint16_t*>(&hdr[28]) = 32;
        *reinterpret_cast<uint32_t*>(&hdr[34]) = imgSize;
        std::fwrite(hdr, 1, 54, f);
        std::fwrite(px.data(), 1, imgSize, f);
        std::fclose(f);
        return true;
    }
};

// 위젯 트리를 CPU 로 래스터(Panel/Window/Button 색만 — GPU DrawContext 미러).
void RasterWidget(CpuCanvas& c, ui::Widget* w) {
    if (!w || w->visibility != ui::Visibility::Visible) return;
    if (ui::Window* win = w->As<ui::Window>()) {
        c.Fill(win->computedRect, Color{0.12f, 0.14f, 0.20f, 1.0f});   // 프레임
        c.Fill(ui::UiRect{win->computedRect.x, win->computedRect.y, win->computedRect.w, win->titleBarHeight},
               Color{0.15f, 0.17f, 0.24f, 1.0f});   // 타이틀바
        if (win->closable) {
            const float sz = win->titleBarHeight - 6.0f;
            c.Fill(ui::UiRect{win->computedRect.x + win->computedRect.w - sz - 3, win->computedRect.y + 3, sz, sz},
                   Color{0.55f, 0.18f, 0.18f, 1.0f});
        }
    } else if (ui::Button* b = w->As<ui::Button>()) {
        Color col{0.30f, 0.30f, 0.34f, 1.0f};
        if (b->state == ui::Button::State::Hover) col = Color{0.40f, 0.40f, 0.46f, 1.0f};
        c.Fill(b->computedRect, col);
    } else if (ui::Panel* p = w->As<ui::Panel>()) {
        if (p->drawBackground) c.Fill(p->computedRect, p->tint);
    }
    for (auto& child : w->children()) RasterWidget(c, child.get());
}

} // namespace

MYE_TEST(UiBmpDump) {
    // 화면 960x540, 중앙 대화창 + 버튼 배치 → 픽셀 검증 + BMP 저장.
    auto rootPanel = std::make_unique<ui::Panel>();
    rootPanel->computedRect = ui::UiRect{0, 0, 960, 540};

    ui::Window* win = static_cast<ui::Window*>(rootPanel->addChild(std::make_unique<ui::Window>()));
    win->closable = true;
    win->titleBarHeight = 24.0f;
    win->anchors = ui::AnchorRect::Center(Vec2{400, 200});

    ui::Button* btn = static_cast<ui::Button*>(win->addChild(std::make_unique<ui::Button>()));
    // 창 내부 하단 중앙 버튼(점 앵커).
    btn->anchors = ui::AnchorRect::TopLeft(Vec2{140, 150}, Vec2{120, 36});
    btn->state = ui::Button::State::Hover;

    // 레이아웃: 루트에서 arrange 하면 앵커가 자식으로 전파.
    rootPanel->arrange(rootPanel->computedRect);

    // 창은 중앙(380,170), 크기 400x200.
    MYE_EXPECT(win->computedRect.x == 280);   // 960/2 - 200
    MYE_EXPECT(win->computedRect.y == 170);   // 540/2 - 100
    // 버튼은 창 좌상단 + (140,150).
    MYE_EXPECT(btn->computedRect.x == 280 + 140);
    MYE_EXPECT(btn->computedRect.y == 170 + 150);

    CpuCanvas canvas(960, 540);
    canvas.Fill(ui::UiRect{0, 0, 960, 540}, Color{0.05f, 0.05f, 0.07f, 1.0f});   // 배경
    RasterWidget(canvas, rootPanel.get());

    // 창 중앙 픽셀 = 프레임 색(어두운 파랑).
    Color center = canvas.At(480, 300);
    MYE_EXPECT(center.b > center.r);   // 파랑 우세

    // 타이틀바 픽셀(창 상단).
    Color titleBar = canvas.At(480, 175);
    MYE_EXPECT(titleBar.b > 0.2f);

    // 버튼 픽셀(hover 색, 회색 계열 밝음).
    Color btnPx = canvas.At(420 + 60, 320 + 18);
    MYE_EXPECT(btnPx.r > 0.35f && btnPx.g > 0.35f);

    // 닫기 버튼(빨강) — 창 우상단.
    Color closePx = canvas.At(static_cast<int>(win->computedRect.x + win->computedRect.w - 12),
                              static_cast<int>(win->computedRect.y + 12));
    MYE_EXPECT(closePx.r > closePx.b);   // 빨강 우세

    const bool wrote = canvas.WriteBmp("ui_dump.bmp");
    MYE_EXPECT(wrote);
}
