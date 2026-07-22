// mye/runtime/DialogueSystem.h — 대화 진행 상태기계 + DialogueBox UI 배선 (docs/06 §7, M6-A)
//
// 소유: 06 (M6-A). 대화창(이름표·초상화·본문 한글 RichText·선택지 버튼)을 mye_ui 로 구성하고,
//   대사 진행(라인 전진·선택 대기·분기 goto)을 상태기계로 관리한다. 컷신 코루틴 say/choose
//   프리미티브(CutsceneRuntime)가 이 시스템을 구동한다 — Lua 는 코루틴으로 컷신을 기술.
//
// 시뮬/표현 분리(docs/06 §8): 대화 진행 상태(현재 라인·선택)는 상태이고, DialogueBox 렌더는
//   표현이다. DialogueSystem 은 상태를 소유하고, UI 는 상태를 표시·입력을 되돌려줄 뿐이다.
//
// UI 는 UiDocument(대화창 레이아웃) + 스킨으로 정의(데이터). 위젯 이름 규약(find 키):
//   "speaker"(Label) · "portrait"(Image) · "body"(Label) · "choices"(선택지 컨테이너).
//   실제 위젯 바인딩은 구현 에이전트가 UiSystem/Widget 으로 채운다.
#pragma once

#include "mye/runtime/RuntimeTypes.h"
#include "mye/runtime/DialogueData.h"
#include "mye/core/Base.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mye { class EventBus; }
namespace mye::ui    { class UiSystem; class UiCanvas; struct UiDocument; }
namespace mye::audio { class AudioEngine; }

namespace mye::runtime {

class LocalizationSystem;

// 대화 시스템 상태(컷신 코루틴이 폴링·대기).
enum class DialogueState : uint8_t {
    Idle,          // 대화 없음(창 닫힘)
    ShowingLine,   // 본문 표시 중(진행 입력 대기 — advance 로 다음)
    WaitingChoice, // 선택지 표시 중(선택 입력 대기)
    Finished,      // 스크립트 종료(다음 프레임 Idle 로)
};

// 대화 완료/선택 통지 이벤트(게임 로직·퀘스트가 구독). trivially copyable 유지 못하므로
//   이벤트 페이로드는 인덱스만 싣고 상세는 시스템 질의로.
struct DialogueLineChangedEvent {
    MYE_EVENT(DialogueLineChangedEvent);
    int32_t lineIndex;   // 새 현재 라인 인덱스(-1 = 종료)
};

// ---------------------------------------------------------------------------
// DialogueBox — 대화창 위젯 배선 래퍼(이름표·초상화·본문·선택지).
//   UiDocument 로 레이아웃을 정의하고, DialogueBox 가 위젯을 find 로 잡아 내용을 갱신한다.
//   구현: UiSystem 에 문서를 Open 하고, 위젯 포인터를 캐시해 SetLine/SetChoices 로 갱신.
// ---------------------------------------------------------------------------
class DialogueBox {
public:
    DialogueBox();
    ~DialogueBox();
    DialogueBox(const DialogueBox&) = delete;
    DialogueBox& operator=(const DialogueBox&) = delete;

    // ui/canvas 는 비소유(수명 > DialogueBox). loc 로 본문·이름표 로컬라이즈. doc 는 대화창 레이아웃.
    Expected<void, Error> Init(ui::UiSystem& ui, ui::UiCanvas& canvas,
                               LocalizationSystem& loc, const ui::UiDocument& doc);
    void Shutdown();

    void Show();    // 창 표시(페이드/슬라이드 연출은 후속 UiTween)
    void Hide();
    bool IsVisible() const;

    // 라인 내용 갱신: 화자·초상화·본문. args 는 자리표시자 치환({name} 등).
    void SetLine(const DialogueLine& line, const std::vector<FormatArg>& args = {});

    // 선택지 버튼 구성. onPick(index) 는 버튼 클릭 시 호출(구현이 위젯 이벤트에 배선).
    void SetChoices(const std::vector<DialogueChoice>& choices,
                    std::function<void(int32_t)> onPick,
                    const std::vector<FormatArg>& args = {});
    void ClearChoices();

    // "다음으로 진행" 입력 감지 결과를 시스템이 폴링(클릭/키). 소비 시 true 후 리셋.
    bool ConsumeAdvanceInput();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// ---------------------------------------------------------------------------
// DialogueSystem — 대화 진행 상태기계. 컷신 코루틴(say/choose)이 구동한다.
//   EngineContext 서비스. Lua 바인딩 say/choose(05)의 백엔드.
// ---------------------------------------------------------------------------
class DialogueSystem {
public:
    MYE_SERVICE(mye::runtime::DialogueSystem);

    DialogueSystem();
    ~DialogueSystem();
    DialogueSystem(const DialogueSystem&) = delete;
    DialogueSystem& operator=(const DialogueSystem&) = delete;

    // box/loc/events 비소유. audio 는 voiceCue 재생용(nullptr 허용). events 로 라인 변경 발행.
    void Initialize(DialogueBox* box, LocalizationSystem* loc,
                    audio::AudioEngine* audio, EventBus* events);
    void Shutdown();

    // ---- 스크립트 기반 진행(데이터 주도) ----
    // 스크립트를 시작(창 표시 + 시작 라인). 이미 진행 중이면 Busy.
    Expected<void, Error> StartScript(const DialogueScript& script);

    // ---- 컷신 프리미티브 백엔드(단발 say/choose — Lua 코루틴이 호출) ----
    // 단일 라인 표시(화자+본문). 코루틴은 이후 IsWaitingAdvance 폴링 → Advance 로 진행 완료.
    Expected<void, Error> Say(const DialogueLine& line,
                              const std::vector<FormatArg>& args = {});
    // 선택지 표시. 코루틴은 PickedChoice() 가 >=0 될 때까지 대기 후 인덱스 회수.
    Expected<void, Error> Choose(const std::vector<DialogueChoice>& choices,
                                 const std::vector<FormatArg>& args = {});

    // ---- 진행 제어 ----
    void Advance();          // ShowingLine 상태에서 다음 라인/종료로 전진
    void Pick(int32_t index);// WaitingChoice 상태에서 선택 확정

    // ---- 상태 질의(코루틴 대기 조건) ----
    DialogueState State() const;
    bool    IsActive() const;              // Idle 아님
    bool    IsWaitingAdvance() const;      // ShowingLine
    bool    IsWaitingChoice() const;       // WaitingChoice
    int32_t PickedChoice() const;          // 마지막 선택 인덱스(-1=미선택). Choose 후 코루틴이 소비.
    int32_t CurrentLineIndex() const;      // 스크립트 진행 시 현재 인덱스(단발 say 는 -1)

    // 매 프레임 — DialogueBox 입력(advance/선택 클릭) 폴링해 상태 전이.
    void Update(float dt);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mye::runtime
