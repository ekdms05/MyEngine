// mye/text/RichText.h — 리치 텍스트 태그 파서 (docs/06 §2 리치 텍스트 태그)
//
// 소유: 06 (M5-A). 태그 문법의 정본. UI·채팅·툴팁이 공유한다.
//   {color=#RRGGBB}...{/color}, {outline=#RRGGBB}...{/outline}, {shadow}...{/shadow},
//   {icon=name}(인라인 스프라이트 자리), {link=payload}...{/link}(클릭 범위).
//   여는 태그는 스타일 스택에 push, 닫는 태그는 pop. 이스케이프: `{{` = 리터럴 '{'.
//
// 파서는 문자열을 순회하며 "런(run)" 시퀀스를 생성한다. 각 런은 (텍스트 조각 or 아이콘 or
//   링크 경계)과 현재 유효 스타일 스냅샷을 갖는다. TextLayout 이 이 런들을 소비해 배치한다.
#pragma once

#include "mye/text/TextTypes.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mye::text {

// 파싱된 런 종류.
enum class RunKind : uint8_t {
    Text,       // 일반 텍스트 조각(utf8Text 유효)
    Icon,       // 인라인 아이콘 자리(iconName 유효)
    LinkBegin,  // 링크 범위 시작(linkPayload 유효) — 이후 Text 런들이 링크에 속함
    LinkEnd,    // 링크 범위 끝
};

// 한 런. 스타일은 현재 스택의 스냅샷(color/outline/shadow 오버라이드 반영).
struct TextRun {
    RunKind  kind = RunKind::Text;
    std::string_view utf8Text;    // Text: 조각(원본 문자열 슬라이스 — 수명은 원본 소유)
    std::string_view iconName;    // Icon: 아이콘 이름
    std::string_view linkPayload; // LinkBegin: 링크 페이로드(예: "item:1234")
    // 유효 스타일(베이스 + 태그 오버라이드).
    Color color = Color::White();
    std::optional<Color>      outline;
    std::optional<ShadowDesc> shadow;
};

// 파싱 결과. 런 목록 + 파싱 경고(닫히지 않은 태그 등 — 무해 폴백).
struct RichTextParse {
    std::vector<TextRun> runs;
    bool hadError = false;        // 문법 오류가 있었으나 폴백 처리됨
};

// 리치 텍스트 파서. 무상태(스레드 안전) — base 스타일로 태그 오버라이드를 해석.
class RichTextParser {
public:
    // richText(UTF-8) 를 런 시퀀스로 파싱. base 는 태그 없는 텍스트의 기본 스타일 색·아웃라인.
    //   반환 런의 string_view 들은 richText 원본을 가리킨다 — richText 수명이 결과보다 길어야 함.
    static RichTextParse Parse(std::string_view richText, const TextStyle& base);

    // 태그를 전부 제거한 순수 텍스트(측정·폴백·검색용). 별도 버퍼에 기록.
    static std::string StripTags(std::string_view richText);
};

// 색 헬퍼: "#RRGGBB" / "#RRGGBBAA" → Color. 실패 시 fallback.
Color ParseHexColor(std::string_view hex, Color fallback = Color::White());

} // namespace mye::text
