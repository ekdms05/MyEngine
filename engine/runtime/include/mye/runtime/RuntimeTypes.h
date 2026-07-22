// mye/runtime/RuntimeTypes.h — 런타임 시스템 공용 값 타입 (docs/06 §7, M6-A)
//
// 소유: 06 (M6-A). 대화·컷신·씬전환·세이브·로컬라이제이션이 공유하는 무의존(코어만) 값 타입.
//   sol.hpp·ui·audio 를 끌어오지 않는 순수 계약 표면 — 헤더 전파 최소화.
//
// 규약(확정): C++20 / mye 네임스페이스 / UTF-8(std::string=UTF-8 바이트) / 예외 불사용(Expected)
//   / dynamic_cast 금지. 좌표 왼손·+Y업·PPU48·960×540. Lua 는 메인 스레드 전용.
//   시뮬레이션(고정틱)/표현(가변) 분리 유지(M2 세이브·네트워크 대비).
#pragma once

#include "mye/core/Base.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mye::runtime {

// ---------------------------------------------------------------------------
// 서브시스템 에러 코드(Expected<T, Error>.code 로 실린다). 0 = 미지정(core 규약).
//   런타임 서브시스템별 진단 분류 — 로그·테스트가 코드로 케이스 구분.
// ---------------------------------------------------------------------------
enum class RuntimeError : int32_t {
    None            = 0,
    NotFound        = 1,    // 대사 라인·문자열 키·세이브 슬롯 없음
    ParseFailed     = 2,    // 에셋/세이브 JSON 파싱 실패
    VersionMismatch = 3,    // 세이브 헤더 버전 > 코드 최신(마이그레이션 불가)
    IoFailed        = 4,    // user:// 원자적 쓰기·읽기 실패
    Busy            = 5,    // 이미 전환/대화 진행 중(재진입 거부)
    InvalidArg      = 6,    // 빈 슬롯 id·음수 등
    NotRegistered   = 7,    // ISaveParticipant/StringTable 미등록
};

// core::Error 로 승격하는 헬퍼(메시지 + 코드). 구현 .cpp 가 공용 사용.
Error MakeError(RuntimeError code, std::string message);

// ---------------------------------------------------------------------------
// 로컬라이제이션 로케일. 폴백 체인 ko → en → key(docs/06 §7).
// ---------------------------------------------------------------------------
enum class Locale : uint8_t { Korean = 0, English = 1 };

const char* LocaleTag(Locale loc);              // "ko" / "en"
Locale      LocaleFromTag(std::string_view tag); // 미지 → Korean

// ---------------------------------------------------------------------------
// 세이브 슬롯 식별. settings·quicksave 는 예약 인덱스, 게임 슬롯은 0..N.
// ---------------------------------------------------------------------------
struct SlotId {
    int32_t index = 0;   // 0..N 게임 슬롯. 예약: kSettings/kQuicksave 아래 상수.
    constexpr bool operator==(const SlotId&) const = default;
};
inline constexpr SlotId kSettingsSlot { -1 };   // settings(볼륨·키바인딩·언어) 전용
inline constexpr SlotId kQuicksaveSlot{ -2 };   // 퀵세이브 전용

// ---------------------------------------------------------------------------
// 씬 전환 서술(docs/06 §7 씬 전환). 페이드아웃 → 로딩 → 로드 → 페이드인.
// ---------------------------------------------------------------------------
struct TransitionDesc {
    float       fadeOutSec = 0.3f;         // 페이드아웃 시간
    float       fadeInSec  = 0.3f;         // 페이드인 시간
    bool        showLoadingScreen = true;  // 로딩 UiDocument 표시 여부(fast-path 는 false)
    std::string loadingDocVpath;           // 로딩 화면 UiDocument vpath(빈값=기본/없음)
    uint32_t    fadeColorRGBA = 0x000000FFu;// 페이드 색(기본 검정)
};

// 씬 참조(런타임 씬 로더 입력). vpath 로 SceneSerializer/런타임 로더가 해석.
struct SceneRef {
    std::string vpath;   // "assets://scenes/village.scene" 등
    bool IsValid() const { return !vpath.empty(); }
};

} // namespace mye::runtime
