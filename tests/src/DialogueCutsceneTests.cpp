// DialogueCutsceneTests.cpp — 대화·컷신 코루틴 프리미티브 헤드리스 테스트 (M6-A, 06)
//
// 검증 범위:
//   - 대사 데이터 JSON 왕복(한글 텍스트·화자·선택지·분기 라운드트립).
//   - DialogueSystem 스크립트 라인 진행 + 선택지 분기(goto) 상태기계.
//   - say/choose 코루틴이 시뮬 입력(Advance/Pick)으로 진행·인덱스 반환.
//   - move_to 코루틴이 목표 도달 시 재개(MoveController + World Transform 전진).
//   - 로컬라이제이션 키 해석(한글).
#include "TestFramework.h"

#include "mye/runtime/DialogueData.h"
#include "mye/runtime/DialogueSystem.h"
#include "mye/runtime/CutsceneRuntime.h"
#include "mye/runtime/Localization.h"
#include "mye/runtime/RuntimeBindings.h"

#include "mye/script/ScriptRuntime.h"
#include "mye/script/CoroutineScheduler.h"

#include "mye/ecs/World.h"
#include "mye/scene/Transform.h"

#include "mye/core/Events.h"

#include <sol/sol.hpp>

using namespace mye;
using namespace mye::runtime;

// ===========================================================================
// 대사 데이터 JSON 왕복 — 한글 텍스트·화자·선택지·분기 라운드트립
// ===========================================================================
MYE_TEST(DialogueDataJsonRoundTrip) {
    DialogueScript s;
    s.startId = "start";
    s.version = 1;

    DialogueLine l0;
    l0.id = "start";
    l0.speaker = LocalizedText::Literal("루미나");
    l0.body = LocalizedText::Literal("여행자여, 어디로 가려 하는가?");
    l0.portrait = "assets://portraits/lumina.png";
    DialogueChoice c0; c0.text = LocalizedText::Literal("숲으로"); c0.gotoId = "forest";
    DialogueChoice c1; c1.text = LocalizedText::Literal("마을로"); c1.gotoId = "town";
    l0.choices = {c0, c1};

    DialogueLine l1; l1.id = "forest"; l1.body = LocalizedText::Literal("숲은 위험하다네.");
    DialogueLine l2; l2.id = "town";   l2.body = LocalizedText::Key("dialogue.town.body");

    s.lines = {l0, l1, l2};

    auto json = SaveDialogueJson(s);
    MYE_EXPECT(bool(json));
    if (!json) return;

    auto loaded = LoadDialogueJson(json.Value());
    MYE_EXPECT(bool(loaded));
    if (!loaded) return;

    const DialogueScript& r = loaded.Value();
    MYE_EXPECT(r.startId == "start");
    MYE_EXPECT(r.lines.size() == 3);
    MYE_EXPECT(r.lines[0].speaker.literal == "루미나");
    MYE_EXPECT(r.lines[0].body.literal == "여행자여, 어디로 가려 하는가?");
    MYE_EXPECT(r.lines[0].choices.size() == 2);
    MYE_EXPECT(r.lines[0].choices[0].text.literal == "숲으로");
    MYE_EXPECT(r.lines[0].choices[0].gotoId == "forest");
    MYE_EXPECT(r.lines[0].choices[1].gotoId == "town");
    MYE_EXPECT(r.lines[2].body.key == "dialogue.town.body");
    MYE_EXPECT(r.IndexOf("forest") == 1);
    MYE_EXPECT(r.IndexOf("town") == 2);
    MYE_EXPECT(r.IndexOf("nope") == -1);
}

// ===========================================================================
// DialogueSystem 스크립트 진행 + 선택지 분기(goto)
// ===========================================================================
MYE_TEST(DialogueScriptProgressionAndBranch) {
    DialogueScript s;
    s.startId = "a";
    DialogueLine a; a.id = "a"; a.body = LocalizedText::Literal("첫 줄");
    DialogueLine b; b.id = "b"; b.body = LocalizedText::Literal("선택하라");
    DialogueChoice c0; c0.text = LocalizedText::Literal("왼쪽"); c0.gotoId = "left";
    DialogueChoice c1; c1.text = LocalizedText::Literal("오른쪽"); c1.gotoId = "right";
    b.choices = {c0, c1};
    DialogueLine left;  left.id = "left";   left.body = LocalizedText::Literal("왼쪽 길");
    DialogueLine right; right.id = "right"; right.body = LocalizedText::Literal("오른쪽 길");
    s.lines = {a, b, left, right};

    DialogueSystem d;
    d.Initialize(nullptr, nullptr, nullptr, nullptr);   // 헤드리스: box/loc/audio 없음

    MYE_EXPECT(bool(d.StartScript(s)));
    MYE_EXPECT(d.State() == DialogueState::ShowingLine);
    MYE_EXPECT(d.CurrentLineIndex() == 0);

    // 재진입 거부(Busy).
    MYE_EXPECT(!d.StartScript(s));

    d.Advance();   // a → b (b 는 선택지 라인 → 다음 Advance 에서 WaitingChoice)
    MYE_EXPECT(d.CurrentLineIndex() == 1);
    MYE_EXPECT(d.State() == DialogueState::ShowingLine);

    d.Advance();   // b 의 선택지 진입
    MYE_EXPECT(d.State() == DialogueState::WaitingChoice);
    MYE_EXPECT(d.IsWaitingChoice());

    d.Pick(1);     // 오른쪽 → right(index 3)
    MYE_EXPECT(d.State() == DialogueState::ShowingLine);
    MYE_EXPECT(d.CurrentLineIndex() == 3);
    MYE_EXPECT(d.PickedChoice() == 1);

    d.Advance();   // right 후 순차 → 범위 초과 → Finished
    MYE_EXPECT(d.State() == DialogueState::Finished);

    d.Update(0.016f);   // Finished → Idle
    MYE_EXPECT(d.State() == DialogueState::Idle);
    MYE_EXPECT(!d.IsActive());
}

// ---- 코루틴 테스트 공용 헬퍼 ----
static script::StdLibPolicy CoPolicy() { return script::StdLibPolicy{}; }

// ===========================================================================
// say/choose 코루틴 — 시뮬 입력(Advance/Pick)으로 진행·선택 인덱스 반환
// ===========================================================================
MYE_TEST(CutsceneSayChooseCoroutine) {
    EventBus bus;
    DialogueSystem dlg;
    dlg.Initialize(nullptr, nullptr, nullptr, &bus);   // 헤드리스(box 없음): 코루틴이 상태로 폴링

    CutsceneRuntime cut;
    cut.Initialize(&dlg, nullptr, nullptr);

    RuntimeBindings bindings(&dlg, &cut, nullptr, nullptr, nullptr);

    script::ScriptRuntime rt;
    rt.Initialize(CoPolicy(), &bus, nullptr);
    rt.AddBindingModule(&bindings);
    rt.ReapplyBindings();

    (void)rt.DoString("_cs = { phase = 0, choice = -1 }", "setup");
    auto r = rt.DoString(R"LUA(
        mye.co.start(function()
            _cs.phase = 1
            mye.dialogue.say("나레이터", "안녕하세요.")   -- 진행 입력까지 yield
            _cs.phase = 2
            _cs.choice = mye.dialogue.choose({ "네", "아니오" })  -- 선택까지 yield
            _cs.phase = 3
        end)
    )LUA", "cutscene.lua");
    MYE_EXPECT(bool(r));

    sol::state& lua = rt.State();
    MYE_EXPECT(int(lua["_cs"]["phase"]) == 1);          // say 시작 후 대기 yield
    MYE_EXPECT(dlg.State() == DialogueState::ShowingLine);

    // 아직 진행 입력 없음 → tick 해도 phase 유지.
    rt.UpdateCoroutines(0.016f);
    MYE_EXPECT(int(lua["_cs"]["phase"]) == 1);

    // 시뮬 진행 입력 → say 완료 → choose 진입.
    dlg.Advance();
    MYE_EXPECT(dlg.State() == DialogueState::Finished);
    dlg.Update(0.016f);   // Finished → Idle (다음 say/choose 준비)
    rt.UpdateCoroutines(0.016f);
    MYE_EXPECT(int(lua["_cs"]["phase"]) == 2);
    MYE_EXPECT(dlg.State() == DialogueState::WaitingChoice);

    // 시뮬 선택 입력 → choose 반환.
    dlg.Pick(1);
    rt.UpdateCoroutines(0.016f);
    MYE_EXPECT(int(lua["_cs"]["phase"]) == 3);
    MYE_EXPECT(int(lua["_cs"]["choice"]) == 1);
    MYE_EXPECT(rt.Coroutines().ActiveCount() == 0);

    rt.Shutdown();
}

// ===========================================================================
// move_to 코루틴 — 목표 도달 시 재개(World Transform 직선 전진)
// ===========================================================================
MYE_TEST(CutsceneMoveToCoroutine) {
    ecs::World world;
    ecs::Entity e = world.Create();
    scene::LocalTransform& tf = world.Add<scene::LocalTransform>(e);
    tf.position = Vec3{0.0f, 0.0f, 0.0f};

    MoveController move;
    move.Initialize(&world, nullptr);   // nav 없음 → 직선 이동

    CutsceneRuntime cut;
    cut.Initialize(nullptr, &move, nullptr);

    RuntimeBindings bindings(nullptr, &cut, nullptr, nullptr, nullptr);

    EventBus bus;
    script::ScriptRuntime rt;
    rt.Initialize(CoPolicy(), &bus, nullptr);
    rt.AddBindingModule(&bindings);
    rt.ReapplyBindings();

    // entity.Packed() 를 double 경계로 전달(계약).
    sol::state& lua = rt.State();
    lua["_ENT"] = static_cast<double>(e.Packed());
    (void)rt.DoString("_mv = { done = false }", "setup");
    auto r = rt.DoString(R"LUA(
        mye.co.start(function()
            mye.cutscene.move_to(_ENT, 10.0, 0.0, 5.0)   -- 속도 5/초, 거리 10 → 2초
            _mv.done = true
        end)
    )LUA", "move.lua");
    MYE_EXPECT(bool(r));
    MYE_EXPECT(int(lua["_mv"]["done"].get<bool>() ? 1 : 0) == 0);

    // 시뮬 틱: MoveController.Update(고정틱) → 코루틴 재개(표현). 1초씩 두 번.
    for (int i = 0; i < 3 && !lua["_mv"]["done"].get<bool>(); ++i) {
        move.Update(1.0f);            // 시뮬: Transform 전진
        rt.UpdateCoroutines(1.0f);    // 코루틴: move_done 폴링
    }

    MYE_EXPECT(lua["_mv"]["done"].get<bool>());
    scene::LocalTransform* after = world.TryGet<scene::LocalTransform>(e);
    MYE_EXPECT(after != nullptr);
    if (after) MYE_EXPECT_NEAR(after->position.x, 10.0f, 1e-3f);
    MYE_EXPECT(move.IsMoveDone(e));

    rt.Shutdown();
}

// ===========================================================================
// 로컬라이제이션 키 해석(한글) + 자리표시자 치환
// ===========================================================================
MYE_TEST(LocalizationKoreanResolve) {
    LocalizationSystem loc;
    loc.Initialize(nullptr);
    loc.Set(Locale::Korean, "greet", "안녕하세요, {name}님!");
    loc.Set(Locale::English, "greet", "Hello, {name}!");

    loc.SetLocale(Locale::Korean);
    MYE_EXPECT(loc.CurrentLocale() == Locale::Korean);

    std::vector<FormatArg> args = { FormatArg{"name", "루미나"} };
    MYE_EXPECT(loc.Format("greet", args) == "안녕하세요, 루미나님!");

    // LocalizedText 해석: keyed → Format, literal → 그대로.
    LocalizedText keyed = LocalizedText::Key("greet");
    MYE_EXPECT(loc.Resolve(keyed, args) == "안녕하세요, 루미나님!");
    LocalizedText lit = LocalizedText::Literal("직접 문자열");
    MYE_EXPECT(loc.Resolve(lit) == "직접 문자열");

    // 미지 키 → 폴백(키 문자열 자체).
    MYE_EXPECT(loc.Get("missing.key") == "missing.key");

    loc.SetLocale(Locale::English);
    MYE_EXPECT(loc.Format("greet", args) == "Hello, 루미나!");

    loc.Shutdown();
}
