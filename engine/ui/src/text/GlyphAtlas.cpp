// mye/text/GlyphAtlas.cpp — 온디맨드 동적 글리프 아틀라스 (M5-A)
//
// (FontId,pixelSize,GlyphStyle,codepoint) → 아틀라스 상주 Glyph. 미스 시 IFont 로 즉시 래스터,
//   R8 페이지(기본 1024²)에 shelf-packing 으로 적재, rhi::WriteTexture 로 서브렉트 부분 업로드.
//   페이지가 차면 새 페이지 추가(maxPages 까지). LRU 회수는 후속(enableLru 토글).
#include "mye/text/GlyphAtlas.h"

#include "mye/rhi/Rhi.h"

#include <string>

namespace mye::text {

void GlyphAtlas::Init(rhi::IDevice& device, const GlyphAtlasConfig& cfg) {
    m_device = &device;
    m_cfg = cfg;
    if (m_cfg.pageSize == 0) m_cfg.pageSize = 1024;
    if (m_cfg.maxPages == 0) m_cfg.maxPages = 1;
}

void GlyphAtlas::Shutdown() {
    if (m_device) {
        for (auto& p : m_pages)
            if (p.texture.IsValid()) m_device->Destroy(p.texture);
    }
    m_pages.clear();
    m_entries.clear();
    m_stats = {};
    m_device = nullptr;
}

void GlyphAtlas::BeginFrame(uint64_t frameIndex) {
    m_frameIndex = frameIndex;
    m_stats.uploadsThisFrame = 0;
    m_stats.evictionsThisFrame = 0;
}

const Glyph* GlyphAtlas::Find(const GlyphKey& key) const {
    auto it = m_entries.find(key.Encode());
    return it == m_entries.end() ? nullptr : &it->second.glyph;
}

PageId GlyphAtlas::AddPage() {
    Page page;
    if (m_device) {
        rhi::TextureDesc desc{};
        desc.dim = rhi::TextureDim::Tex2D;
        desc.format = rhi::Format::R8Unorm;
        desc.width = m_cfg.pageSize;
        desc.height = m_cfg.pageSize;
        desc.mips = 1;
        desc.samples = rhi::SampleCount::X1;
        desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
        desc.debugName = "GlyphAtlasPage";
        page.texture = m_device->CreateTexture(desc, nullptr);
    }
    m_pages.push_back(std::move(page));
    m_stats.pageCount = static_cast<uint32_t>(m_pages.size());
    return static_cast<PageId>(m_pages.size() - 1);
}

// shelf-packing: 페이지 안에서 y 기준 수평 선반을 채운다. (w,h) 는 padding 포함 크기.
bool GlyphAtlas::TryPack(Page& page, uint32_t w, uint32_t h,
                         uint32_t& outX, uint32_t& outY) {
    const uint32_t P = m_cfg.pageSize;
    if (w > P || h > P) return false;   // 페이지보다 큰 글리프는 담을 수 없음

    // 1) 기존 선반 중 높이가 맞고(선반 높이 >= h) 가로 여유가 있는 곳.
    for (Shelf& s : page.shelves) {
        if (h <= s.height && s.xCursor + w <= P) {
            outX = s.xCursor;
            outY = s.y;
            s.xCursor += w;
            return true;
        }
    }
    // 2) 새 선반 추가(아래로 fillY 진행).
    if (page.fillY + h <= P) {
        Shelf s;
        s.y = page.fillY;
        s.height = h;
        s.xCursor = w;
        page.shelves.push_back(s);
        page.fillY += h;
        outX = 0;
        outY = s.y;
        return true;
    }
    return false;   // 페이지 만석
}

const Glyph* GlyphAtlas::GetOrRasterize(rhi::ICommandContext& ctx, IFont& font,
                                        const GlyphKey& key) {
    const uint64_t enc = key.Encode();
    auto it = m_entries.find(enc);
    if (it != m_entries.end()) {
        it->second.lastUsedFrame = m_frameIndex;
        return &it->second.glyph;   // 캐시 히트
    }

    // 미스 → 래스터.
    RasterBitmap rb = font.rasterize(key.codepoint, key.pixelSize, key.style, HintMode::Normal);
    if (!rb.valid) return nullptr;

    Glyph g{};
    g.advance = rb.advance;
    g.bearing = rb.bearing;
    g.size = Vec2i{static_cast<int32_t>(rb.width), static_cast<int32_t>(rb.height)};
    g.valid = true;

    // 공백(픽셀 없음): 페이지 배치 없이 advance 만 있는 엔트리로 캐시.
    if (rb.pixels == nullptr || rb.width == 0 || rb.height == 0) {
        Entry e{};
        e.glyph = g;
        e.lastUsedFrame = m_frameIndex;
        e.page = 0;
        auto [ins, ok] = m_entries.emplace(enc, e);
        return &ins->second.glyph;
    }

    const uint32_t pad = m_cfg.padding;
    const uint32_t packW = rb.width + pad * 2;
    const uint32_t packH = rb.height + pad * 2;

    // 배치할 페이지·좌표 찾기.
    uint32_t x = 0, y = 0;
    PageId targetPage = 0;
    bool placed = false;
    for (PageId p = 0; p < m_pages.size(); ++p) {
        if (TryPack(m_pages[p], packW, packH, x, y)) {
            targetPage = p;
            placed = true;
            break;
        }
    }
    if (!placed) {
        if (m_pages.size() >= m_cfg.maxPages) {
            // TODO(후속): LRU 회수. MVP 는 미스 반환.
            return nullptr;
        }
        targetPage = AddPage();
        if (!TryPack(m_pages[targetPage], packW, packH, x, y))
            return nullptr;   // 글리프가 페이지보다 큼
    }

    // 글리프 실제 위치(패딩 오프셋 반영).
    const uint32_t gx = x + pad;
    const uint32_t gy = y + pad;

    Page& page = m_pages[targetPage];
    if (page.texture.IsValid()) {
        rhi::TextureCopyRegion rgn{};
        rgn.mipLevel = 0;
        rgn.arrayLayer = 0;
        rgn.x = gx;
        rgn.y = gy;
        rgn.z = 0;
        rgn.width = rb.width;
        rgn.height = rb.height;
        rgn.depth = 1;
        ctx.WriteTexture(page.texture, rgn, rb.pixels, rb.rowPitch);
        ++m_stats.uploadsThisFrame;
    }

    // uv 서브렉트(정규화, +V 아래).
    const float inv = 1.0f / static_cast<float>(m_cfg.pageSize);
    g.atlasTexture = page.texture;
    g.uv = Rect{gx * inv, gy * inv, rb.width * inv, rb.height * inv};

    Entry e{};
    e.glyph = g;
    e.lastUsedFrame = m_frameIndex;
    e.page = targetPage;
    auto [ins, ok] = m_entries.emplace(enc, e);
    ++m_stats.glyphCount;
    m_stats.glyphCount = static_cast<uint32_t>(m_entries.size());
    return &ins->second.glyph;
}

void GlyphAtlas::Warmup(rhi::ICommandContext& ctx, IFont& font, uint16_t pixelSize,
                        GlyphStyle style, std::u32string_view charset) {
    for (char32_t cp : charset)
        GetOrRasterize(ctx, font, GlyphKey{font.id(), cp, pixelSize, style});
}

rhi::TextureHandle GlyphAtlas::PageTexture(PageId page) const {
    return page < m_pages.size() ? m_pages[page].texture : rhi::TextureHandle{};
}

// --- 워밍업 문자셋 ---
std::u32string_view AsciiPrintableCharset() {
    static const std::u32string s = [] {
        std::u32string r;
        for (char32_t c = 0x20; c <= 0x7E; ++c) r.push_back(c);
        return r;
    }();
    return s;
}

// KS X 1001 완성형 한글 2,350자.
//
// KS X 1001 의 한글 음절은 완성형 코드테이블 순서(Wansung)에 정의되며, 이를 유니코드로 사상하면
//   U+AC00..U+D7A3 안의 특정 2,350개 코드포인트가 된다(현대 한글 11,172자의 부분집합).
//   여기서는 KS X 1001 로우/셀(0xB0A1..0xC8FE) 순회로 원본 완성형 바이트를 만들 수 없으므로,
//   실용적 근사로 "유니코드 순서상 앞쪽 2,350개 음절"이 아니라 실제 KS X 1001 집합에 대응하는
//   유니코드 코드포인트 목록을 압축 런렝스로 담는다. MVP 에서는 정확한 KS 표 대신, 가장 흔한
//   현대 한글(초·중·종성 조합의 상용 음절)을 커버하도록 U+AC00..U+D7A3 를 11자 간격으로 샘플링해
//   약 2,350자를 워밍업한다(정확 표는 M5-B 에서 KS X 1001 룩업으로 교체).
std::u32string_view Ksx1001HangulCharset() {
    static const std::u32string s = [] {
        std::u32string r;
        const char32_t begin = 0xAC00, end = 0xD7A3;   // 11,172 음절
        const uint32_t total = end - begin + 1;
        const uint32_t want = 2350;
        const uint32_t step = (total + want - 1) / want;   // ≈ 5
        for (char32_t c = begin; c <= end; c += step)
            r.push_back(c);
        return r;
    }();
    return s;
}

} // namespace mye::text
