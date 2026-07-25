// mye/core/I18n.h — 경량 UI 다국어(i18n) 레이어 (ko/en/ja/zh)
//
// 에디터/툴 UI 문자열을 키로 조회해 현재 언어의 번역을 돌려준다. 게임 콘텐츠 로컬라이즈
// (engine/runtime Localization)와 별개의, 프로그램 UI 전용 경량 테이블이다.
//
// 사용:
//   ImGui::Begin(mye::i18n::T("panel.hierarchy"));
//   mye::i18n::SetLanguage(mye::i18n::Lang::En);
//
// 언어 전환 시 Version()이 증가한다 — ImGui 도킹 레이아웃처럼 언어에 따라 창 제목(=식별자)이
// 바뀌는 UI는 이 값을 관찰해 레이아웃을 재구성한다.
#pragma once

#include <cstdint>

namespace mye::i18n {

enum class Lang : std::uint8_t { Ko, En, Ja, Zh };

// 현재 언어 설정/조회. SetLanguage는 값이 바뀔 때만 Version()을 증가시킨다.
void SetLanguage(Lang lang);
Lang GetLanguage();

// 언어 전환 카운터(레이아웃 재빌드 트리거용). 초기 0, 전환마다 +1.
std::uint32_t Version();

// 키의 현재 언어 번역. 없으면 ko → 키 문자열 순으로 폴백(항상 유효 포인터).
const char* T(const char* key);

// 언어 표시 이름(자국어 표기): "한국어" / "English" / "日本語" / "中文".
const char* LangName(Lang lang);

} // namespace mye::i18n
