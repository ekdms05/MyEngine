// mye/refl/Attribute.h — 필드 UI 힌트·의미 어트리뷰트 (docs/04 §UI 힌트 어트리뷰트)
//
// 07 인스펙터가 소비하는 공식 집합: Range, Tooltip, Category, HideInInspector,
//   ReadOnly, Units, Multiline. 그 외 LuaHidden(05). 플러그인이 커스텀 Attribute 추가 가능.
//
// M3-A 범위: 어트리뷰트 값 표현(태그된 유니온)과 조회 헬퍼. 실제 인스펙터 소비는 07(후속).
// 어트리뷰트는 FieldInfo 에 부착되며 TypeBuilder::Attr(...) 로 직전 필드에 단다.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace mye::refl {

// 공식 어트리뷰트 종류. 플러그인 커스텀은 Custom + 이름 해시로 확장(후속).
enum class AttributeKind : std::uint16_t {
    Range,           // 수치 범위 [min,max] (+step) — 슬라이더/클램프
    Tooltip,         // 마우스오버 설명 문자열
    Category,        // 인스펙터 그룹핑 라벨
    HideInInspector, // 07에서 숨김(직렬화는 유지)
    ReadOnly,        // 07에서 편집 불가(표시만)
    Units,           // 단위 표기("px","deg","s")
    Multiline,       // 문자열 여러 줄 편집
    LuaHidden,       // 05 Lua 바인딩에서 제외
    Custom,          // 플러그인 정의(customName 사용)
};

// 어트리뷰트 값 — 종류별로 필요한 필드만 채운다(작은 값이라 태그된 평면 struct로 보관).
struct Attribute {
    AttributeKind kind = AttributeKind::Tooltip;

    // Range
    double rangeMin = 0.0;
    double rangeMax = 0.0;
    double rangeStep = 0.0;   // 0 = 미지정

    // Tooltip / Category / Units / Custom 이름 등 문자열 payload
    std::string text;

    // Custom 어트리뷰트 식별(kind==Custom 일 때 이름 해시)
    std::uint64_t customId = 0;

    // ---- 생성 헬퍼(TypeBuilder::Attr 인자로 사용) ----
    static Attribute MakeRange(double lo, double hi, double step = 0.0) {
        Attribute a; a.kind = AttributeKind::Range; a.rangeMin = lo; a.rangeMax = hi; a.rangeStep = step; return a;
    }
    static Attribute MakeTooltip(std::string_view s) {
        Attribute a; a.kind = AttributeKind::Tooltip; a.text = std::string(s); return a;
    }
    static Attribute MakeCategory(std::string_view s) {
        Attribute a; a.kind = AttributeKind::Category; a.text = std::string(s); return a;
    }
    static Attribute MakeUnits(std::string_view s) {
        Attribute a; a.kind = AttributeKind::Units; a.text = std::string(s); return a;
    }
    static Attribute MakeHideInInspector() { Attribute a; a.kind = AttributeKind::HideInInspector; return a; }
    static Attribute MakeReadOnly()        { Attribute a; a.kind = AttributeKind::ReadOnly; return a; }
    static Attribute MakeMultiline()       { Attribute a; a.kind = AttributeKind::Multiline; return a; }
    static Attribute MakeLuaHidden()       { Attribute a; a.kind = AttributeKind::LuaHidden; return a; }
};

} // namespace mye::refl
