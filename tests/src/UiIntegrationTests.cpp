// UiIntegrationTests.cpp — M5-A 통합 스모크 (헤드리스 CPU): 한글 텍스트 + 대화창 + 버튼 콜백
//
// 목적(M5-A 통합·검증 §3): GPU 없이 아래 엔드투엔드를 한 테스트에서 증명한다.
//   (a) 한글 문자열("안녕하세요 모험가님!")을 FreeTypeFace 로 래스터해 실제 non-zero 픽셀이
//       나오는지(빈 사각형 아님) — 글리프별 잉크 픽셀 수를 센다.
//   (b) UiDocument(Window+Label+Button)를 인스턴스화·레이아웃 → 창/버튼 사각형 픽셀 존재.
//   (c) 버튼 히트테스트 + PointerDown/Up/Click 라우팅으로 onClick 콜백 발화.
//   (d) TextLayout 이 폭 제한에서 한글 줄바꿈을 수행(줄 수 >= 2) — 대화창 폭 안 줄바꿈.
//   결과 BMP(ui_integration.bmp) 로 시각 확인. 수치는 로그로 남긴다.
//
// GlyphAtlas/UiRenderer 는 rhi 디바이스를 요구하므로 여기서는 FreeTypeFace 의 순수 CPU 래스터를
//   직접 캔버스에 합성한다(아틀라스 GPU 업로드 경로는 TextTests 의 헤드리스 device 가 별도로 커버).
#include "TestFramework.h"
#include "TestFontGen.h"

#include "mye/text/FontFace.h"
#include "mye/text/TextLayout.h"
#include "mye/text/RichText.h"
#include "mye/ui/UiTypes.h"
#include "mye/ui/Widgets.h"
#include "mye/ui/UiDocument.h"

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

using namespace mye;

namespace {

std::span<const std::byte> AsBytes(const std::vector<uint8_t>& v) {
    return std::span<const std::byte>(reinterpret_cast<const std::byte*>(v.data()), v.size());
}

// UTF-8 → UTF-32 (최소 디코더 — 테스트 문자열 커버).
std::vector<char32_t> Utf8ToUtf32(std::string_view s) {
    std::vector<char32_t> out;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t cp; int n;
        if (c < 0x80)       { cp = c; n = 1; }
        else if (c < 0xE0)  { cp = c & 0x1F; n = 2; }
        else if (c < 0xF0)  { cp = c & 0x0F; n = 3; }
        else                { cp = c & 0x07; n = 4; }
        for (int k = 1; k < n && i + k < s.size(); ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        out.push_back(cp);
        i += n;
    }
    return out;
}

struct CpuCanvas {
    int w, h;
    std::vector<uint8_t> px;   // BGRA top-down
    CpuCanvas(int W, int H) : w(W), h(H), px(static_cast<size_t>(W) * H * 4, 0) {}

    void Fill(const ui::UiRect& r, Color c) {
        const int x0 = static_cast<int>(r.x < 0 ? 0 : r.x);
        const int y0 = static_cast<int>(r.y < 0 ? 0 : r.y);
        const int x1 = static_cast<int>(r.x + r.w);
        const int y1 = static_cast<int>(r.y + r.h);
        for (int y = y0; y < y1 && y < h; ++y)
            for (int x = x0; x < x1 && x < w; ++x) {
                uint8_t* p = &px[(static_cast<size_t>(y) * w + x) * 4];
                p[0] = static_cast<uint8_t>(c.b * 255);
                p[1] = static_cast<uint8_t>(c.g * 255);
                p[2] = static_cast<uint8_t>(c.r * 255);
                p[3] = 255;
            }
    }

    // R8 그레이 비트맵을 (dx,dy) 좌상단에 알파블렌드(글리프 잉크=흰색).
    int BlitGlyph(const text::RasterBitmap& g, int dx, int dy, Color ink) {
        if (!g.valid || !g.pixels) return 0;
        int inked = 0;
        for (uint32_t y = 0; y < g.height; ++y)
            for (uint32_t x = 0; x < g.width; ++x) {
                uint8_t a = g.pixels[y * g.rowPitch + x];
                if (a == 0) continue;
                int px_ = dx + static_cast<int>(x), py_ = dy + static_cast<int>(y);
                if (px_ < 0 || py_ < 0 || px_ >= w || py_ >= h) continue;
                uint8_t* p = &px[(static_cast<size_t>(py_) * w + px_) * 4];
                float af = a / 255.0f;
                p[0] = static_cast<uint8_t>(p[0] * (1 - af) + ink.b * 255 * af);
                p[1] = static_cast<uint8_t>(p[1] * (1 - af) + ink.g * 255 * af);
                p[2] = static_cast<uint8_t>(p[2] * (1 - af) + ink.r * 255 * af);
                p[3] = 255;
                ++inked;
            }
        return inked;
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
        uint8_t hdr[54] = {};
        hdr[0] = 'B'; hdr[1] = 'M';
        *reinterpret_cast<uint32_t*>(&hdr[2]) = 54 + imgSize;
        *reinterpret_cast<uint32_t*>(&hdr[10]) = 54;
        *reinterpret_cast<uint32_t*>(&hdr[14]) = 40;
        *reinterpret_cast<int32_t*>(&hdr[18]) = w;
        *reinterpret_cast<int32_t*>(&hdr[22]) = -h;
        *reinterpret_cast<uint16_t*>(&hdr[26]) = 1;
        *reinterpret_cast<uint16_t*>(&hdr[28]) = 32;
        *reinterpret_cast<uint32_t*>(&hdr[34]) = imgSize;
        std::fwrite(hdr, 1, 54, f);
        std::fwrite(px.data(), 1, imgSize, f);
        std::fclose(f);
        return true;
    }
};

// FontRegistry 를 IFontProvider 로 감싸 TextLayout::Measure 에 넘긴다(줄바꿈 측정용).
class SingleFontProvider final : public text::IFontProvider {
public:
    explicit SingleFontProvider(text::IFont* f) : m_font(f) {}
    text::IFont* ResolveFont(text::FontId) const override { return m_font; }
private:
    text::IFont* m_font;
};

} // namespace

// -----------------------------------------------------------------------------
// 통합 스모크: 한글 래스터 + 대화창 + 버튼 콜백 + 줄바꿈, BMP 덤프.
// -----------------------------------------------------------------------------
MYE_TEST(UiIntegrationDialogHangul) {
    const std::string kMsg = "안녕하세요 모험가님!";
    const std::vector<char32_t> cps = Utf8ToUtf32(kMsg);
    MYE_EXPECT(cps.size() >= 9);   // "안녕하세요 모험가님!" = 11 코드포인트

    // --- 폰트 생성: 메시지에 등장하는 한글 음절 + ASCII 커버 ---
    std::vector<uint32_t> hangul;
    for (char32_t c : cps) if (c >= 0xAC00 && c <= 0xD7A3) hangul.push_back(static_cast<uint32_t>(c));
    auto ttf = testfont::MakeTestFont(hangul);
    auto faceRes = text::FreeTypeFace::Create(text::FontId{0}, AsBytes(ttf));
    MYE_EXPECT(faceRes.HasValue());
    if (!faceRes.HasValue()) return;
    auto face = std::move(faceRes.Value());

    // =====================================================================
    // (b) UiDocument 로 대화창 구성 → 인스턴스화 → 레이아웃.
    // =====================================================================
    ui::UiDocument doc;
    doc.root.typeName = "Window";
    doc.root.name = "npc_dialog";
    doc.root.properties.push_back({"title", "모험가"});
    doc.root.anchors = ui::AnchorRect::Center(Vec2{440, 200});

    ui::UiNodeDesc bodyDesc;
    bodyDesc.typeName = "Label";
    bodyDesc.name = "body";
    bodyDesc.properties.push_back({"text", kMsg});
    doc.root.children.push_back(bodyDesc);

    ui::UiNodeDesc btnDesc;
    btnDesc.typeName = "Button";
    btnDesc.name = "ok";
    doc.root.children.push_back(btnDesc);

    ui::WidgetFactory factory;
    auto treeRes = doc.Instantiate(factory);
    MYE_EXPECT(treeRes.HasValue());
    if (!treeRes.HasValue()) return;

    auto rootPanel = std::make_unique<ui::Panel>();
    rootPanel->computedRect = ui::UiRect{0, 0, 960, 540};
    ui::Widget* win = rootPanel->addChild(std::move(treeRes.Value()));

    // 버튼을 창 하단 중앙에 명시 배치(점 앵커) + hover 상태로 표시.
    ui::Button* btn = static_cast<ui::Button*>(win->findByName("ok"));
    MYE_EXPECT(btn != nullptr);
    if (!btn) return;
    btn->anchors = ui::AnchorRect::TopLeft(Vec2{160, 150}, Vec2{120, 36});
    btn->state = ui::Button::State::Hover;

    // 레이아웃 전파.
    rootPanel->arrange(rootPanel->computedRect);

    // 창은 중앙(960/2-220=260, 540/2-100=170), 440x200.
    MYE_EXPECT(win->computedRect.w == 440);
    MYE_EXPECT(win->computedRect.h == 200);
    MYE_EXPECT(win->computedRect.x == 260);
    MYE_EXPECT(win->computedRect.y == 170);
    MYE_EXPECT(btn->computedRect.x == 260 + 160);
    MYE_EXPECT(btn->computedRect.y == 170 + 150);

    // =====================================================================
    // (d) TextLayout: 좁은 폭에서 한글 줄바꿈(줄 수 >= 2).
    // =====================================================================
    SingleFontProvider provider(face.get());
    text::TextStyle style;
    style.font = text::FontId{0};
    style.size = 24;
    style.color = Color::White();
    text::LayoutParams lp;
    lp.maxWidth = 160.0f;   // 좁게 → 여러 줄
    lp.wrap = text::WrapMode::WordChar;
    text::TextLayout layout;
    Vec2i measured = layout.Measure(provider, kMsg, style, lp);
    const uint16_t lineCount = layout.lineCount();
    MYE_EXPECT(measured.x > 0 && measured.y > 0);
    MYE_EXPECT(lineCount >= 2);   // 24px * ~10글자 > 160px → 줄바꿈 발생

    // =====================================================================
    // (a) 한글 래스터 → 캔버스 합성. 창/버튼 사각형도 그린다.
    // =====================================================================
    CpuCanvas canvas(960, 540);
    canvas.Fill(ui::UiRect{0, 0, 960, 540}, Color{0.05f, 0.05f, 0.07f, 1.0f});          // 배경
    canvas.Fill(win->computedRect, Color{0.12f, 0.14f, 0.20f, 1.0f});                    // 창 프레임
    ui::Window* w = win->As<ui::Window>();
    const float tbH = w ? w->titleBarHeight : 24.0f;
    canvas.Fill(ui::UiRect{win->computedRect.x, win->computedRect.y, win->computedRect.w, tbH},
                Color{0.15f, 0.17f, 0.24f, 1.0f});                                       // 타이틀바
    Color btnCol = (btn->state == ui::Button::State::Hover)
                 ? Color{0.40f, 0.40f, 0.46f, 1.0f} : Color{0.30f, 0.30f, 0.34f, 1.0f};
    canvas.Fill(btn->computedRect, btnCol);                                             // 버튼

    // 본문 텍스트를 창 안(타이틀바 아래)에 래스터. 폭 제한 줄바꿈을 흉내 — advance 로 배치.
    const uint16_t px = 24;
    const int startX = static_cast<int>(win->computedRect.x) + 16;
    const int startY = static_cast<int>(win->computedRect.y) + static_cast<int>(tbH) + 30;
    const int wrapX = static_cast<int>(win->computedRect.x) + 16 + 200;
    int penX = startX, penY = startY;
    int totalInk = 0;
    int hangulGlyphsInked = 0;
    int glyphsRendered = 0;
    for (char32_t cp : cps) {
        if (cp == U' ') { penX += px / 3; continue; }
        text::RasterBitmap g = face->rasterize(cp, px, text::GlyphStyle::Normal, text::HintMode::Normal);
        if (penX + static_cast<int>(g.width) > wrapX) { penX = startX; penY += px + 4; }  // 단순 줄바꿈
        const int bx = penX + g.bearing.x;
        const int by = penY - g.bearing.y;
        int inked = canvas.BlitGlyph(g, bx, by, Color{0.95f, 0.95f, 0.85f, 1.0f});
        totalInk += inked;
        ++glyphsRendered;
        if (cp >= 0xAC00 && cp <= 0xD7A3 && inked > 0) ++hangulGlyphsInked;
        penX += g.advance ? g.advance : static_cast<int16_t>(px);
    }

    // (a) 증명: 한글 글리프가 실제 잉크 픽셀을 냈다(빈 사각형 아님).
    const int hangulCount = static_cast<int>(hangul.size());
    MYE_EXPECT(hangulGlyphsInked == hangulCount);   // 모든 한글 음절이 잉크를 냄
    MYE_EXPECT(totalInk > 1000);                    // 충분한 픽셀 수

    // (b) 증명: 창·버튼 픽셀 존재.
    Color winCenter = canvas.At(480, 300);
    MYE_EXPECT(winCenter.b > winCenter.r);          // 창 프레임(파랑 우세)
    Color btnPx = canvas.At(static_cast<int>(btn->computedRect.x + btn->computedRect.w / 2),
                            static_cast<int>(btn->computedRect.y + btn->computedRect.h / 2));
    MYE_EXPECT(btnPx.r > 0.35f && btnPx.g > 0.35f); // 버튼(hover 밝은 회색)

    // =====================================================================
    // (c) 버튼 히트테스트 + 클릭 콜백 발화(실제 이벤트 라우팅).
    // =====================================================================
    int clicks = 0;
    btn->onClick = [&] { ++clicks; };
    const Vec2 hitPt{ btn->computedRect.x + btn->computedRect.w / 2,
                      btn->computedRect.y + btn->computedRect.h / 2 };

    // 히트테스트: 클릭 지점이 버튼 사각형 안, 창 밖 지점은 밖.
    auto contains = [](const ui::UiRect& r, Vec2 p) {
        return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;
    };
    MYE_EXPECT(contains(btn->computedRect, hitPt));
    MYE_EXPECT(!contains(btn->computedRect, Vec2{0, 0}));

    // Down → Up → Click 순서(Button::onEvent 상태머신).
    ui::UiEvent down{}; down.type = ui::UiEventType::PointerDown; down.pointerPos = hitPt;
    btn->onEvent(down);
    ui::UiEvent up{}; up.type = ui::UiEventType::PointerUp; up.pointerPos = hitPt;
    btn->onEvent(up);
    ui::UiEvent click{}; click.type = ui::UiEventType::PointerClick; click.pointerPos = hitPt;
    btn->onEvent(click);
    MYE_EXPECT(clicks == 1);

    // BMP 덤프(시각 확인용).
    const bool wrote = canvas.WriteBmp("ui_integration.bmp");
    MYE_EXPECT(wrote);

    std::printf("[integration] hangul=%d inked=%d totalInk=%d glyphs=%d lines=%u measured=%dx%d clicks=%d\n",
                hangulCount, hangulGlyphsInked, totalInk, glyphsRendered,
                static_cast<unsigned>(lineCount), measured.x, measured.y, clicks);
}
