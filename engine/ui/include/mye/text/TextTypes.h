// mye/text/TextTypes.h — 텍스트 서브시스템 공용 값 타입 (docs/06 §2 텍스트·한글 렌더링)
//
// 소유: 06 (M5-A). 글리프·스타일·메트릭·리치텍스트 결과의 무의존 데이터 타입만 모은다.
// 여기에는 로직이 없다 — FontFace/GlyphAtlas/TextLayout/TextRenderer 가 이 타입들을 생산·소비한다.
//
// 좌표·색 규약: mye::Vec2/Color/Rect(core/Math) 사용. 픽셀 좌표는 정수 스냅이 원칙이나
//   레이아웃 중간 계산은 float 로 유지하고 렌더 제출 직전 스냅한다(정수 픽셀 스냅 정책 — 02).
#pragma once

#include "mye/core/Math.h"
#include "mye/rhi/RhiTypes.h"

#include <cstdint>
#include <optional>

namespace mye::text {

// ---------------------------------------------------------------------------
// 폰트 식별·스타일
// ---------------------------------------------------------------------------

// 글리프 래스터 변형 플래그. (fontId, pixelSize, GlyphStyle) 3튜플이 아틀라스 캐시 키.
enum class GlyphStyle : uint8_t {
    Normal  = 0,
    Outline = 1 << 0,   // FT_Stroker 로 별도 아웃라인 글리프 (본체와 다른 슬롯에 캐시)
    // Bold/Italic 등 합성 스타일은 후속(M5-B). MVP 는 폰트별 실제 웨이트 파일을 쓴다.
};
constexpr GlyphStyle operator|(GlyphStyle a, GlyphStyle b) {
    return static_cast<GlyphStyle>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
constexpr bool HasFlag(GlyphStyle v, GlyphStyle f) {
    return (static_cast<uint8_t>(v) & static_cast<uint8_t>(f)) != 0;
}

// 힌팅·렌더 모드. 도트 폰트는 Mono, 일반 TTF 는 Normal(그레이스케일 AA).
//   SDF 는 채택하지 않는다(docs/06 §2 — 도트 픽셀 정합 우선).
enum class HintMode : uint8_t {
    Normal,   // FT_LOAD_TARGET_NORMAL — 그레이스케일 AA (채팅·툴팁 가독성)
    Light,    // FT_LOAD_TARGET_LIGHT  — 수직 힌팅만 (약한 스냅)
    Mono,     // FT_LOAD_TARGET_MONO   — 1bpp → R8 확장 (도트 미학, 정수 정합)
};

// 폰트 인스턴스 식별자. FontFace 등록 시 발급(런타임 안정, 직렬화 비대상 — 에셋 참조는 vpath).
struct FontId {
    uint32_t v = 0xFFFF'FFFFu;
    constexpr bool IsValid() const { return v != 0xFFFF'FFFFu; }
    constexpr bool operator==(const FontId&) const = default;
};

// ---------------------------------------------------------------------------
// 메트릭
// ---------------------------------------------------------------------------

// 특정 pixelSize 에서의 폰트 수직 메트릭 (26.6 → 정수 픽셀로 반올림된 값).
struct FontMetrics {
    int16_t ascent   = 0;   // 베이스라인 위 (양수)
    int16_t descent  = 0;   // 베이스라인 아래 (양수 크기; 실제 y 는 음수 방향)
    int16_t lineGap  = 0;   // 줄 간 추가 간격
    int16_t lineHeight = 0; // ascent + descent + lineGap (권장 줄 advance)
};

// ---------------------------------------------------------------------------
// 글리프 (아틀라스 상주 결과)
// ---------------------------------------------------------------------------

// GlyphAtlas 가 반환하는 래스터된 글리프 슬롯. 픽셀 좌표·advance 는 정수.
//   uv 는 정규화(0..1) 아틀라스 서브렉트(+V 아래 — SpriteBatch UV 규약과 동일).
struct Glyph {
    rhi::TextureHandle atlasTexture{};  // 이 글리프가 상주하는 아틀라스 페이지 텍스처
    Rect     uv{};              // 정규화 소스 사각형(아틀라스 페이지 내)
    Vec2i    size{};            // 비트맵 픽셀 크기 (w,h)
    Vec2i    bearing{};         // 좌상단 베어링 (bearingX, bearingY=베이스라인 위 높이)
    int16_t  advance = 0;       // 수평 advance (정수 픽셀; 26.6 반올림)
    bool     valid = false;     // false = 캐시 미스/래스터 실패(공백 취급)
};

// ---------------------------------------------------------------------------
// 스타일·레이아웃 파라미터
// ---------------------------------------------------------------------------

struct ShadowDesc {
    Vec2i offset{1, 1};             // 픽셀 오프셋(도트: 보통 1,1)
    Color color = Color::Black();
};

// 텍스트 스타일. Label/RichText 의 베이스 스타일. 리치 태그가 이 값을 파생·오버라이드한다.
struct TextStyle {
    FontId  font{};
    uint16_t size = 16;             // 픽셀 크기
    Color    color = Color::White();
    HintMode hint = HintMode::Normal;
    std::optional<Color>      outline;   // 있으면 아웃라인 글리프 1패스 추가
    std::optional<ShadowDesc> shadow;    // 있으면 그림자 1패스 추가(본체 앞)
};

// 수평 정렬. 세로 정렬·양끝맞춤은 후속.
enum class TextAlign : uint8_t { Left, Center, Right };

// 줄바꿈 정책. 한글은 음절 char-wrap, 라틴 단어는 word-wrap(혼합) — docs/06 §2.
enum class WrapMode : uint8_t {
    None,        // 줄바꿈 없음(한 줄, 오버플로우 허용)
    Char,        // 문자 단위(폭 초과 시 아무 데서나) — CJK
    WordChar,    // 라틴 단어 유지 + CJK 음절 단위 (기본, 금칙 처리 포함)
};

struct LayoutParams {
    float     maxWidth = 0.0f;      // 0 = 무제한(WrapMode::None 강제). 픽셀.
    TextAlign align = TextAlign::Left;
    WrapMode  wrap  = WrapMode::WordChar;
    int16_t   lineSpacing = 0;      // lineHeight 에 더하는 추가 픽셀(음수 허용)
    int16_t   tabWidth = 4;         // 탭 = N 스페이스 advance
};

// ---------------------------------------------------------------------------
// 레이아웃 결과
// ---------------------------------------------------------------------------

// 배치된 글리프 하나(렌더 제출 단위). pos 는 좌상단 픽셀(정수 스냅 완료).
struct PositionedGlyph {
    char32_t codepoint = 0;
    Vec2i    pos{};                 // 좌상단 픽셀(레이아웃 원점 기준 로컬)
    Vec2i    size{};                // 글리프 픽셀 크기
    Rect     uv{};                  // 아틀라스 서브렉트(정규화)
    rhi::TextureHandle atlasTexture{};
    Color    color = Color::White();// 리치 태그로 파생된 최종 색
    uint16_t pixelSize = 0;         // 이 글리프의 pixelSize (아웃라인/그림자 재조회용)
    GlyphStyle style = GlyphStyle::Normal;
    uint16_t lineIndex = 0;         // 소속 줄(정렬·커서 계산용)
};

// 클릭 가능한 {link=...} 범위. 히트 렉트(레이아웃 로컬 픽셀)+링크 페이로드 문자열.
struct LinkRegion {
    Rect         rect{};            // 로컬 픽셀 히트 사각형
    uint32_t     payloadOffset = 0; // TextLayout 내부 문자열 풀 오프셋
    uint32_t     payloadLength = 0;
};

// 인라인 {icon=...} 스프라이트 자리. 아이콘 해석(스프라이트 조회)은 UI/채팅 레벨이 한다.
struct InlineIcon {
    Vec2i        pos{};             // 좌상단 로컬 픽셀
    Vec2i        size{};
    uint32_t     nameOffset = 0;    // 아이콘 이름 문자열 풀 오프셋
    uint32_t     nameLength = 0;
};

} // namespace mye::text
