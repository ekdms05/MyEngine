// mye/runtime/DialogueSystem.cpp — 대화 진행 상태기계 + DialogueBox UI 배선 (M6-A)
//
// DialogueBox: UiDocument 를 UiSystem 에 Open 하고 speaker/portrait/body/choices 위젯을 캐시,
//   SetLine/SetChoices 로 한글 RichText·초상화·선택지 버튼을 갱신한다(loc 로 로컬라이즈).
// DialogueSystem: 라인 진행·선택·분기 상태기계. 컷신 코루틴(say/choose)이 폴링·구동한다.
#include "mye/runtime/DialogueSystem.h"

#include "mye/runtime/Localization.h"
#include "mye/core/Events.h"

#include "mye/ui/UiSystem.h"
#include "mye/ui/UiDocument.h"
#include "mye/ui/Widget.h"
#include "mye/ui/Widgets.h"

#include "mye/audio/AudioEngine.h"

namespace mye::runtime {

// ---- DialogueBox -----------------------------------------------------------
struct DialogueBox::Impl {
    ui::UiSystem*       ui = nullptr;
    ui::UiCanvas*       canvas = nullptr;
    LocalizationSystem* loc = nullptr;

    ui::UiDocumentHandle handle{};
    ui::Widget*  root = nullptr;      // Open 후 canvas.root()
    ui::Label*   speaker = nullptr;   // "speaker"
    ui::Image*   portrait = nullptr;  // "portrait"
    ui::Label*   body = nullptr;      // "body"
    ui::Widget*  choices = nullptr;   // "choices" 컨테이너(런타임 버튼 부착)

    bool visible = false;
    bool advancePressed = false;

    std::function<void(int32_t)> onPick;   // 현재 선택지 콜백(SetChoices 가 배선)

    // 이름으로 위젯을 잡아 타입 확인 후 캐시.
    template <typename T> T* Find(std::string_view name) {
        if (!root) return nullptr;
        ui::Widget* w = root->findByName(name);
        return w ? w->As<T>() : nullptr;
    }
};

DialogueBox::DialogueBox() : m_impl(std::make_unique<Impl>()) {}
DialogueBox::~DialogueBox() = default;

Expected<void, Error> DialogueBox::Init(ui::UiSystem& ui, ui::UiCanvas& canvas,
                                        LocalizationSystem& loc, const ui::UiDocument& doc) {
    m_impl->ui = &ui;
    m_impl->canvas = &canvas;
    m_impl->loc = &loc;

    auto opened = ui.Open(doc, canvas);
    if (!opened) return opened.GetError();
    m_impl->handle = opened.Value();
    m_impl->root = canvas.root();

    // 위젯 이름 규약(동결): speaker·portrait·body·choices.
    m_impl->speaker  = m_impl->Find<ui::Label>("speaker");
    m_impl->portrait = m_impl->Find<ui::Image>("portrait");
    m_impl->body     = m_impl->Find<ui::Label>("body");
    if (ui::Widget* c = m_impl->root ? m_impl->root->findByName("choices") : nullptr)
        m_impl->choices = c;

    // "advance" 버튼(옵션): 클릭 시 진행 입력 세팅. 없으면 시스템이 다른 경로로 Advance.
    if (m_impl->root) {
        if (ui::Widget* w = m_impl->root->findByName("advance")) {
            if (ui::Button* b = w->As<ui::Button>()) {
                Impl* impl = m_impl.get();
                b->onClick = [impl]() { impl->advancePressed = true; };
            }
        }
    }

    Hide();
    return Expected<void, Error>{};
}

void DialogueBox::Shutdown() {
    if (m_impl->ui && m_impl->handle.IsValid()) m_impl->ui->Close(m_impl->handle);
    m_impl->ui = nullptr;
    m_impl->canvas = nullptr;
    m_impl->loc = nullptr;
    m_impl->root = nullptr;
    m_impl->speaker = nullptr;
    m_impl->portrait = nullptr;
    m_impl->body = nullptr;
    m_impl->choices = nullptr;
    m_impl->visible = false;
    m_impl->onPick = nullptr;
}

void DialogueBox::Show() {
    m_impl->visible = true;
    if (m_impl->root) m_impl->root->visibility = ui::Visibility::Visible;
}
void DialogueBox::Hide() {
    m_impl->visible = false;
    ClearChoices();
    if (m_impl->root) m_impl->root->visibility = ui::Visibility::Hidden;
}
bool DialogueBox::IsVisible() const { return m_impl->visible; }

void DialogueBox::SetLine(const DialogueLine& line, const std::vector<FormatArg>& args) {
    LocalizationSystem* loc = m_impl->loc;
    // 화자 이름표.
    if (m_impl->speaker) {
        std::string s = loc ? loc->Resolve(line.speaker, args) : line.speaker.literal;
        m_impl->speaker->setText(std::move(s));
    }
    // 본문(리치텍스트 태그 허용 — Label 이 파싱).
    if (m_impl->body) {
        std::string b = loc ? loc->Resolve(line.body, args) : line.body.literal;
        m_impl->body->setText(std::move(b));
    }
    // 초상화: vpath 유효 시 표시(스프라이트 실제 로드는 표현계층/에셋 배선 몫 — 가시성만 토글).
    if (m_impl->portrait) {
        m_impl->portrait->visibility =
            line.portrait.empty() ? ui::Visibility::Hidden : ui::Visibility::Visible;
    }
    ClearChoices();   // 진행 라인은 선택지 비움.
}

void DialogueBox::SetChoices(const std::vector<DialogueChoice>& choices,
                             std::function<void(int32_t)> onPick,
                             const std::vector<FormatArg>& args) {
    ClearChoices();
    m_impl->onPick = std::move(onPick);
    if (!m_impl->choices) return;

    LocalizationSystem* loc = m_impl->loc;
    Impl* impl = m_impl.get();
    for (int32_t i = 0; i < static_cast<int32_t>(choices.size()); ++i) {
        auto btn = std::make_unique<ui::Button>();
        btn->name = "choice_" + std::to_string(i);
        // 세로 스택 배치 가정(choices 컨테이너가 StackLayout). 점앵커 폭은 컨테이너가 조정.
        btn->anchors = ui::AnchorRect::TopLeft(Vec2{0.0f, 0.0f}, Vec2{200.0f, 28.0f});
        btn->onClick = [impl, i]() { if (impl->onPick) impl->onPick(i); };

        // 캡션 라벨(자식).
        auto lbl = std::make_unique<ui::Label>();
        std::string t = loc ? loc->Resolve(choices[static_cast<size_t>(i)].text, args)
                            : choices[static_cast<size_t>(i)].text.literal;
        lbl->setText(std::move(t));
        lbl->anchors = ui::AnchorRect::Fill();
        btn->label = lbl.get();
        btn->addChild(std::move(lbl));

        m_impl->choices->addChild(std::move(btn));
    }
}

void DialogueBox::ClearChoices() {
    if (!m_impl->choices) return;
    // 자식 전부 제거(런타임 생성 버튼).
    while (!m_impl->choices->children().empty()) {
        ui::Widget* first = m_impl->choices->children().front().get();
        (void)m_impl->choices->removeChild(first);
    }
}

bool DialogueBox::ConsumeAdvanceInput() {
    bool p = m_impl->advancePressed;
    m_impl->advancePressed = false;
    return p;
}

// ---- DialogueSystem --------------------------------------------------------
struct DialogueSystem::Impl {
    DialogueBox*        box = nullptr;
    LocalizationSystem* loc = nullptr;
    audio::AudioEngine* audio = nullptr;
    EventBus*           events = nullptr;

    DialogueState  state = DialogueState::Idle;
    DialogueScript script;      // StartScript 진행 시 보관
    bool           hasScript = false;
    int32_t        lineIndex = -1;
    int32_t        picked = -1;

    void PublishLine(int32_t idx) {
        if (events) { DialogueLineChangedEvent e{idx}; events->Publish(e); }
    }
    void GotoIdle() {
        state = DialogueState::Idle;
        hasScript = false;
        lineIndex = -1;
        if (box) box->Hide();
    }
    // voiceCue 재생(있으면). AudioCue 에셋 로드는 asset 배선(M6-B) 필요 — 여기선 훅만.
    void PlayVoice(const DialogueLine& /*line*/) {
        // TODO(M6-B): AssetManager 로 voiceCue vpath → AudioCue 로드 후 audio->PostCue.
    }
    // 스크립트 라인을 박스에 표시.
    void ShowScriptLine(int32_t idx) {
        if (idx < 0 || idx >= static_cast<int32_t>(script.lines.size())) return;
        const DialogueLine& l = script.lines[static_cast<size_t>(idx)];
        if (box) box->SetLine(l);
        PlayVoice(l);
    }
};

DialogueSystem::DialogueSystem() : m_impl(std::make_unique<Impl>()) {}
DialogueSystem::~DialogueSystem() = default;

void DialogueSystem::Initialize(DialogueBox* box, LocalizationSystem* loc,
                                audio::AudioEngine* audio, EventBus* events) {
    m_impl->box = box;
    m_impl->loc = loc;
    m_impl->audio = audio;
    m_impl->events = events;
}

void DialogueSystem::Shutdown() {
    m_impl->GotoIdle();
    m_impl->box = nullptr;
    m_impl->loc = nullptr;
    m_impl->audio = nullptr;
    m_impl->events = nullptr;
}

Expected<void, Error> DialogueSystem::StartScript(const DialogueScript& script) {
    if (m_impl->state != DialogueState::Idle)
        return MakeError(RuntimeError::Busy, "대화가 이미 진행 중");
    if (script.lines.empty())
        return MakeError(RuntimeError::InvalidArg, "빈 대사 스크립트");

    m_impl->script = script;
    m_impl->hasScript = true;
    int32_t start = script.startId.empty() ? 0 : m_impl->script.IndexOf(script.startId);
    if (start < 0) start = 0;
    m_impl->lineIndex = start;
    m_impl->state = DialogueState::ShowingLine;
    if (m_impl->box) m_impl->box->Show();
    m_impl->ShowScriptLine(start);
    m_impl->PublishLine(start);
    return Expected<void, Error>{};
}

Expected<void, Error> DialogueSystem::Say(const DialogueLine& line,
                                          const std::vector<FormatArg>& args) {
    if (m_impl->state == DialogueState::WaitingChoice)
        return MakeError(RuntimeError::Busy, "선택 대기 중");
    m_impl->hasScript = false;
    m_impl->lineIndex = -1;
    m_impl->state = DialogueState::ShowingLine;
    if (m_impl->box) { m_impl->box->Show(); m_impl->box->SetLine(line, args); }
    m_impl->PlayVoice(line);
    return Expected<void, Error>{};
}

Expected<void, Error> DialogueSystem::Choose(const std::vector<DialogueChoice>& choices,
                                             const std::vector<FormatArg>& args) {
    if (choices.empty())
        return MakeError(RuntimeError::InvalidArg, "빈 선택지");
    m_impl->picked = -1;
    m_impl->state = DialogueState::WaitingChoice;
    if (m_impl->box) {
        m_impl->box->Show();
        DialogueSystem* self = this;
        m_impl->box->SetChoices(choices, [self](int32_t i) { self->Pick(i); }, args);
    }
    return Expected<void, Error>{};
}

void DialogueSystem::Advance() {
    if (m_impl->state != DialogueState::ShowingLine) return;

    if (!m_impl->hasScript) {
        // 단발 say: 진행 = 종료.
        m_impl->state = DialogueState::Finished;
        return;
    }
    // 스크립트: 다음 라인 결정(선택지 있으면 WaitingChoice, gotoId 있으면 점프, 아니면 순차).
    const DialogueLine& cur = m_impl->script.lines[static_cast<size_t>(m_impl->lineIndex)];
    if (!cur.choices.empty()) {
        (void)Choose(cur.choices);
        return;
    }
    int32_t next;
    if (!cur.gotoId.empty()) {
        next = m_impl->script.IndexOf(cur.gotoId);
    } else {
        next = m_impl->lineIndex + 1;
    }
    if (next < 0 || next >= static_cast<int32_t>(m_impl->script.lines.size())) {
        m_impl->state = DialogueState::Finished;
        m_impl->PublishLine(-1);
        return;
    }
    m_impl->lineIndex = next;
    m_impl->state = DialogueState::ShowingLine;
    m_impl->ShowScriptLine(next);
    m_impl->PublishLine(next);
}

void DialogueSystem::Pick(int32_t index) {
    if (m_impl->state != DialogueState::WaitingChoice) return;
    m_impl->picked = index;

    if (m_impl->hasScript) {
        const DialogueLine& cur = m_impl->script.lines[static_cast<size_t>(m_impl->lineIndex)];
        if (index >= 0 && index < static_cast<int32_t>(cur.choices.size())) {
            const std::string& gotoId = cur.choices[static_cast<size_t>(index)].gotoId;
            int32_t next = gotoId.empty() ? (m_impl->lineIndex + 1)
                                          : m_impl->script.IndexOf(gotoId);
            if (next < 0 || next >= static_cast<int32_t>(m_impl->script.lines.size())) {
                m_impl->state = DialogueState::Finished;
                m_impl->PublishLine(-1);
                return;
            }
            m_impl->lineIndex = next;
            m_impl->state = DialogueState::ShowingLine;
            if (m_impl->box) m_impl->box->ClearChoices();
            m_impl->ShowScriptLine(next);
            m_impl->PublishLine(next);
            return;
        }
    }
    // 단발 choose: 선택만 기록하고 표시 종료 대기(코루틴이 PickedChoice 소비).
    if (m_impl->box) m_impl->box->ClearChoices();
    m_impl->state = DialogueState::ShowingLine;
}

DialogueState DialogueSystem::State() const { return m_impl->state; }
bool DialogueSystem::IsActive() const { return m_impl->state != DialogueState::Idle; }
bool DialogueSystem::IsWaitingAdvance() const { return m_impl->state == DialogueState::ShowingLine; }
bool DialogueSystem::IsWaitingChoice() const { return m_impl->state == DialogueState::WaitingChoice; }
int32_t DialogueSystem::PickedChoice() const { return m_impl->picked; }
int32_t DialogueSystem::CurrentLineIndex() const { return m_impl->lineIndex; }

void DialogueSystem::Update(float /*dt*/) {
    // 상태 종료 처리 + DialogueBox 입력 폴링.
    if (m_impl->state == DialogueState::Finished) {
        m_impl->GotoIdle();
        return;
    }
    if (m_impl->box && m_impl->state == DialogueState::ShowingLine) {
        if (m_impl->box->ConsumeAdvanceInput()) Advance();
    }
}

} // namespace mye::runtime
