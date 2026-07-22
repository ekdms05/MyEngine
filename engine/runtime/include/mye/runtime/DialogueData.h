// mye/runtime/DialogueData.h — 대사 데이터 에셋(라인·화자·선택지·분기) (docs/06 §7, M6-A)
//
// 소유: 06 (M6-A). 대화를 "데이터로 정의"한다(엔진 공통 철학). 대사 스크립트 에셋 =
//   { 라인 목록: 화자·본문(로컬라이즈 키)·선택지·분기 }. refl/ser JsonArchive 로 왕복.
//   런타임 로직(진행·선택 대기)은 Lua 컷신 코루틴(say/choose)이 소비 — 여기선 데이터 표면만.
//
// 본문은 LocalizedText(키) — 실제 표시 문자열은 LocalizationSystem 이 로케일별 해석.
//   본문 문자열은 mye::text 리치텍스트 태그({color}/{icon}/{link})를 그대로 담을 수 있다(UI 파싱).
#pragma once

#include "mye/runtime/RuntimeTypes.h"
#include "mye/runtime/Localization.h"
#include "mye/core/Base.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mye::runtime {

// 선택지 하나. 표시 텍스트 + 선택 시 점프할 라인 id(빈값=다음 라인 진행).
struct DialogueChoice {
    LocalizedText text;      // 선택지 표시 문자열(로컬라이즈 키)
    std::string   gotoId;    // 점프 대상 DialogueLine.id(빈값=순차 진행)
    std::string   conditionKey;  // 조건 플래그 키(빈값=항상 표시) — 게임/Lua 가 평가(확장점)
};

// 대사 한 줄. 화자·본문·선택지·다음 라인.
struct DialogueLine {
    std::string   id;         // 라인 식별자(분기 goto 타깃). 비면 순차 인덱스로만 접근.
    LocalizedText speaker;    // 화자 이름표(로컬라이즈 키 또는 literal)
    std::string   portrait;   // 초상화 스프라이트 vpath(빈값=이름표만)
    LocalizedText body;       // 본문(리치텍스트 태그 허용, 로컬라이즈 키)
    std::string   voiceCue;   // 음성 AudioCue vpath(빈값=무음) — 재생은 DialogueSystem
    std::string   gotoId;     // 이 라인 후 점프할 id(빈값=순차 진행. 선택지 있으면 선택지 우선)

    std::vector<DialogueChoice> choices;   // 비면 단순 진행 라인, 있으면 선택 대기
};

// 대사 스크립트 에셋. 라인 목록 + 시작 라인.
struct DialogueScript {
    std::string               startId;   // 시작 라인 id(빈값=index 0)
    std::vector<DialogueLine> lines;
    uint32_t                  version = 1;

    // id 로 라인 인덱스 조회(없으면 -1).
    int32_t IndexOf(std::string_view id) const;
};

// DialogueScript ↔ JSON 텍스트 왕복(refl/ser JsonArchive). 에셋 저장·로드용.
Expected<std::string, Error>   SaveDialogueJson(const DialogueScript& script);
Expected<DialogueScript, Error> LoadDialogueJson(std::string_view json);

} // namespace mye::runtime
