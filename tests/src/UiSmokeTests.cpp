// UiSmokeTests.cpp — M5-A 인게임 UI + 텍스트 골격 계약 스모크 (헤드리스: rhi 없이)
//
// 목적: mye_ui 의 계약 표면(FontRegistry blob 등록·폴백, RichText 색·태그 파싱, 금칙/CJK 판정,
//   위젯 트리 소유·검색, UiDocument 인스턴스화, 스킨 조회)이 링크·동작하는지 최소 검증.
//   rhi/GPU 가 필요한 경로(GlyphAtlas 업로드·UiRenderer)는 여기서 제외(별도 GPU 테스트 M5-A 후속).
#include "TestFramework.h"

#include "mye/text/RichText.h"
#include "mye/text/TextLayout.h"
#include "mye/text/TextRenderer.h"
#include "mye/ui/UiTypes.h"
#include "mye/ui/Widgets.h"
#include "mye/ui/UiDocument.h"
#include "mye/ui/UiSkin.h"

using namespace mye;

MYE_TEST(UiHexColorParse) {
    const Color c = text::ParseHexColor("#FF8000");
    MYE_EXPECT_NEAR(c.r, 1.0f, 0.01f);
    MYE_EXPECT_NEAR(c.g, 0.5f, 0.01f);
    MYE_EXPECT_NEAR(c.b, 0.0f, 0.01f);
    // 잘못된 입력 → fallback.
    const Color f = text::ParseHexColor("nope", Color::Black());
    MYE_EXPECT(f == Color::Black());
}

MYE_TEST(UiRichTextSingleRun) {
    text::TextStyle base;
    base.color = Color::White();
    text::RichTextParse p = text::RichTextParser::Parse("안녕하세요", base);
    MYE_EXPECT(p.runs.size() >= 1);
    MYE_EXPECT(p.runs[0].kind == text::RunKind::Text);
}

MYE_TEST(UiKinsokuRules) {
    // 행두 금칙: 닫는 괄호·구두점은 줄 첫머리 불가.
    MYE_EXPECT(text::IsLineStartForbidden(U')'));
    MYE_EXPECT(text::IsLineStartForbidden(U'.'));
    MYE_EXPECT(!text::IsLineStartForbidden(U'가'));
    // 행말 금칙: 여는 괄호는 줄 끝 불가.
    MYE_EXPECT(text::IsLineEndForbidden(U'('));
    MYE_EXPECT(!text::IsLineEndForbidden(U')'));
    // 한글 음절은 char-wrap 대상.
    MYE_EXPECT(text::IsCjkBreakable(U'한'));
    MYE_EXPECT(!text::IsCjkBreakable(U'A'));
}

MYE_TEST(UiFontRegistryRejectsEmptyBlob) {
    text::FontRegistry reg;
    std::span<const std::byte> empty;
    auto res = reg.RegisterTtf(empty);
    MYE_EXPECT(!res.HasValue());   // 빈 blob 은 실패
}

MYE_TEST(UiWidgetTreeOwnership) {
    auto panel = std::make_unique<ui::Panel>();
    panel->name = "root";
    ui::Label* lbl = static_cast<ui::Label*>(
        panel->addChild(std::make_unique<ui::Label>()));
    lbl->name = "caption";
    lbl->setText("대화");
    // 이름 검색.
    MYE_EXPECT(panel->findByName("caption") == lbl);
    MYE_EXPECT(panel->findByName("root") == panel.get());
    MYE_EXPECT(panel->findByName("missing") == nullptr);
    // As<T>() 타입 식별(dynamic_cast 금지).
    MYE_EXPECT(lbl->As<ui::Label>() != nullptr);
    MYE_EXPECT(lbl->As<ui::Button>() == nullptr);
    // 자식 분리.
    ui::WidgetPtr detached = panel->removeChild(lbl);
    MYE_EXPECT(detached != nullptr);
    MYE_EXPECT(panel->children().size() == 0);
}

MYE_TEST(UiWidgetFactoryBuiltins) {
    ui::WidgetFactory factory;
    MYE_EXPECT(factory.Create("Panel") != nullptr);
    MYE_EXPECT(factory.Create("Label") != nullptr);
    MYE_EXPECT(factory.Create("Button") != nullptr);
    MYE_EXPECT(factory.Create("Window") != nullptr);
    MYE_EXPECT(factory.Create("StackLayout") != nullptr);
    MYE_EXPECT(factory.Create("GridLayout") != nullptr);
    MYE_EXPECT(factory.Create("NoSuchWidget") == nullptr);
}

MYE_TEST(UiDocumentInstantiate) {
    ui::UiDocument doc;
    doc.root.typeName = "Window";
    doc.root.name = "dialog";
    doc.root.properties.push_back({"title", "NPC"});
    ui::UiNodeDesc labelDesc;
    labelDesc.typeName = "Label";
    labelDesc.name = "body";
    labelDesc.properties.push_back({"text", "안녕하세요, 모험가님."});
    doc.root.children.push_back(labelDesc);

    ui::WidgetFactory factory;
    auto tree = doc.Instantiate(factory);
    MYE_EXPECT(tree.HasValue());
    if (tree.HasValue()) {
        ui::Widget* root = tree.Value().get();
        MYE_EXPECT(root->name == "dialog");
        ui::Window* win = root->As<ui::Window>();
        MYE_EXPECT(win != nullptr);
        if (win) MYE_EXPECT(win->title == "NPC");
        ui::Widget* body = root->findByName("body");
        MYE_EXPECT(body != nullptr);
        if (body) {
            ui::Label* lbl = body->As<ui::Label>();
            MYE_EXPECT(lbl != nullptr);
            if (lbl) MYE_EXPECT(lbl->text == "안녕하세요, 모험가님.");
        }
    }
    // 알 수 없는 타입 → 실패.
    ui::UiDocument bad;
    bad.root.typeName = "Bogus";
    auto badTree = bad.Instantiate(factory);
    MYE_EXPECT(!badTree.HasValue());
}

MYE_TEST(UiSkinLookup) {
    ui::UiSkin skin;
    ui::UiStyle style;
    style.tint = Color{0.2f, 0.4f, 0.6f, 1.0f};
    skin.Set("window.frame", style);
    const ui::UiStyle* found = skin.Find("window.frame");
    MYE_EXPECT(found != nullptr);
    if (found) MYE_EXPECT(found->tint == (Color{0.2f, 0.4f, 0.6f, 1.0f}));
    MYE_EXPECT(skin.Find("missing.key") == nullptr);
    // StyleClassId 해시 일관성.
    MYE_EXPECT(ui::MakeStyleClass("window.frame") ==
               ui::MakeStyleClass("window.frame"));
}
