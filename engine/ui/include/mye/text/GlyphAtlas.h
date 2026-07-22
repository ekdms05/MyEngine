// mye/text/GlyphAtlas.h — 온디맨드 동적 글리프 아틀라스 (docs/06 §2 동적 아틀라스)
//
// 소유: 06 (M5-A). (FontId, pixelSize, GlyphStyle, codepoint) → 아틀라스 상주 Glyph.
//   래스터를 요청 시점에 IFont 로 굽고 R8 페이지(기본 1024×1024)에 shelf-packing 으로 적재,
//   rhi::WriteTexture 로 부분 업로드한다. 페이지가 차면 새 페이지 추가.
//   프레임 카운터 기반 LRU 로 오래 안 쓴 슬롯 회수(MVP 는 페이지 추가만, LRU 는 후속 토글).
//
// rhi 사용: IDevice::CreateTexture(R8Unorm, Sampled|CopyDst) 로 페이지 생성,
//   ICommandContext::WriteTexture 로 글리프 서브렉트 업로드. 06→02 요구사항: R8 부분 업로드.
#pragma once

#include "mye/text/TextTypes.h"
#include "mye/text/FontFace.h"
#include "mye/core/Base.h"

#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mye::rhi { class IDevice; class ICommandContext; }

namespace mye::text {

// 아틀라스 캐시 키. 64bit 로 인코딩해 unordered_map 키로 사용.
struct GlyphKey {
    FontId     font{};
    char32_t   codepoint = 0;
    uint16_t   pixelSize = 0;
    GlyphStyle style = GlyphStyle::Normal;

    uint64_t Encode() const {
        // font(24) | size(12) | style(4) | codepoint(21) — 한글 BMP+보충 커버
        return (static_cast<uint64_t>(font.v & 0xFFFFFF) << 40) |
               (static_cast<uint64_t>(pixelSize & 0xFFF) << 28) |
               (static_cast<uint64_t>(static_cast<uint8_t>(style) & 0xF) << 24) |
               (static_cast<uint64_t>(codepoint) & 0x1FFFFF);
    }
    bool operator==(const GlyphKey&) const = default;
};

using PageId = uint32_t;

struct GlyphAtlasConfig {
    uint32_t pageSize = 1024;        // 정사각 페이지 한 변(픽셀). R8.
    uint32_t maxPages = 8;           // 페이지 상한(초과 시 LRU 회수 시도 → 실패면 미스)
    bool     enableLru = false;      // MVP=false(페이지 추가만). 후속에 프레임 LRU 회수.
    uint32_t padding = 1;            // 글리프 간 여백(픽셀) — 샘플 블리딩 방지
};

struct GlyphAtlasStats {
    uint32_t pageCount = 0;
    uint32_t glyphCount = 0;
    uint32_t uploadsThisFrame = 0;
    uint32_t evictionsThisFrame = 0;
};

// 동적 아틀라스. Init 에서 device 참조 보관, 페이지는 지연 생성.
class GlyphAtlas {
public:
    GlyphAtlas() = default;
    ~GlyphAtlas() = default;
    GlyphAtlas(const GlyphAtlas&) = delete;
    GlyphAtlas& operator=(const GlyphAtlas&) = delete;

    void Init(rhi::IDevice& device, const GlyphAtlasConfig& cfg = {});
    void Shutdown();

    // 프레임 경계: LRU 기준 프레임 인덱스 갱신·통계 리셋. 매 프레임 1회.
    void BeginFrame(uint64_t frameIndex);

    // 캐시 조회. 미스 시 font 로 즉시 래스터→적재(업로드는 ctx 필요 — GetOrRasterize 참조).
    //   여기서는 이미 상주한 것만 반환(레이아웃 measure 단계용 폴링). 미스 시 valid=false.
    const Glyph* Find(const GlyphKey& key) const;

    // 캐시 조회 + 미스 시 래스터·업로드. ctx 는 활성 프레임의 커맨드 컨텍스트(WriteTexture 발행).
    //   반환 Glyph 포인터는 다음 페이지 재해시 전까지 유효 — 즉시 소비 권장.
    const Glyph* GetOrRasterize(rhi::ICommandContext& ctx, IFont& font, const GlyphKey& key);

    // 워밍업: ASCII + KS X 1001 완성형(약 2,350자) 등 문자셋을 미리 굽는다(첫 프레임 스파이크 완화).
    void Warmup(rhi::ICommandContext& ctx, IFont& font, uint16_t pixelSize,
                GlyphStyle style, std::u32string_view charset);

    rhi::TextureHandle PageTexture(PageId page) const;
    uint32_t PageCount() const { return static_cast<uint32_t>(m_pages.size()); }
    const GlyphAtlasStats& Stats() const { return m_stats; }

private:
    // shelf-packing 한 줄(선반). 페이지 안에서 y 기준 수평 채움.
    struct Shelf { uint32_t y = 0, height = 0, xCursor = 0; };
    struct Page {
        rhi::TextureHandle texture{};
        std::vector<Shelf> shelves;
        uint32_t fillY = 0;          // 다음 새 선반 시작 y
    };
    struct Entry {
        Glyph    glyph{};
        uint64_t lastUsedFrame = 0;
        PageId   page = 0;
    };

    // page 에 (w,h) 슬롯 배치 시도. 실패면 false(새 페이지 필요).
    bool TryPack(Page& page, uint32_t w, uint32_t h, uint32_t& outX, uint32_t& outY);
    PageId AddPage();

    rhi::IDevice* m_device = nullptr;
    GlyphAtlasConfig m_cfg{};
    std::vector<Page> m_pages;
    std::unordered_map<uint64_t, Entry> m_entries;   // key.Encode() → Entry
    uint64_t m_frameIndex = 0;
    GlyphAtlasStats m_stats{};
};

// 워밍업 편의: KS X 1001 완성형 한글 2,350자 코드포인트 목록(정적). 구현 .cpp 제공.
std::u32string_view Ksx1001HangulCharset();
// ASCII 인쇄 가능 문자(0x20..0x7E).
std::u32string_view AsciiPrintableCharset();

} // namespace mye::text
