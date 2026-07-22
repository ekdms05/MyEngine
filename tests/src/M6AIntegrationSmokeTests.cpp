// M6AIntegrationSmokeTests.cpp — M6-A 통합 스모크(헤드리스): 대화·컷신·세이브·씬전환 엔드투엔드
//
// 목적(M6-A 통합·검증 할 일 §3): 하나의 Lua 컷신 코루틴 흐름에서 아래를 한 번에 증명한다.
//   (a) say 로 한글 대사 표시 → 진행 입력(Advance) → 다음 라인.
//   (b) choose 선택지 → 분기(선택 인덱스로 Lua 분기).
//   (c) move_to 캐릭터 이동 완료까지 코루틴 대기(World Transform 전진 후 재개).
//   (d) 세이브 슬롯에 게임 상태 저장 → 새 인스턴스로 로드해 복원 일치.
//   (e) 씬 전환 페이드 → 로드(poll) → 완료 시뮬(SceneTransitionManager 상태기계).
//   (f) 대화창 한글 대사 + 선택지 버튼을 BMP 로 덤프하고 잉크 픽셀 수를 센다.
//
// 전부 GPU 없이 CPU 로 구동한다. Lua 는 RuntimeBindings 표면(mye.dialogue/cutscene/...)을
//   실제로 등록해 사용한다 — RuntimeModule OnPostInitialize 배선과 동일한 서브시스템 배치를
//   테스트가 직접 구성(헤드리스). 결과 수치는 stdout 으로 남긴다(evidence).
#include "TestFramework.h"
#include "TestFontGen.h"

#include "mye/runtime/DialogueSystem.h"
#include "mye/runtime/DialogueData.h"
#include "mye/runtime/CutsceneRuntime.h"
#include "mye/runtime/Localization.h"
#include "mye/runtime/SaveSystem.h"
#include "mye/runtime/SceneTransition.h"
#include "mye/runtime/RuntimeBindings.h"

#include "mye/script/ScriptRuntime.h"
#include "mye/script/CoroutineScheduler.h"

#include "mye/ecs/World.h"
#include "mye/scene/Transform.h"

#include "mye/ser/Archive.h"
#include "mye/text/FontFace.h"
#include "mye/core/Events.h"

#include <sol/sol.hpp>

#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace mye;
using namespace mye::runtime;

namespace {

namespace fs = std::filesystem;

std::span<const std::byte> AsBytes(const std::vector<uint8_t>& v) {
    return std::span<const std::byte>(reinterpret_cast<const std::byte*>(v.data()), v.size());
}

std::vector<char32_t> Utf8ToUtf32(std::string_view s) {
    std::vector<char32_t> out;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t cp; int n;
        if (c < 0x80)      { cp = c; n = 1; }
        else if (c < 0xE0) { cp = c & 0x1F; n = 2; }
        else if (c < 0xF0) { cp = c & 0x0F; n = 3; }
        else               { cp = c & 0x07; n = 4; }
        for (int k = 1; k < n && i + k < s.size(); ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        out.push_back(cp);
        i += n;
    }
    return out;
}

struct CpuCanvas {
    int w, h;
    std::vector<uint8_t> px;   // BGRA top-down
    CpuCanvas(int W, int H) : w(W), h(H), px(static_cast<size_t>(W) * H * 4, 0) {}
    void FillRect(int x0, int y0, int rw, int rh, uint8_t r, uint8_t g, uint8_t b) {
        for (int y = y0; y < y0 + rh && y < h; ++y)
            for (int x = x0; x < x0 + rw && x < w; ++x) {
                if (x < 0 || y < 0) continue;
                uint8_t* p = &px[(static_cast<size_t>(y) * w + x) * 4];
                p[0] = b; p[1] = g; p[2] = r; p[3] = 255;
            }
    }
    int BlitGlyph(const text::RasterBitmap& gph, int dx, int dy) {
        if (!gph.valid || !gph.pixels) return 0;
        int inked = 0;
        for (uint32_t y = 0; y < gph.height; ++y)
            for (uint32_t x = 0; x < gph.width; ++x) {
                uint8_t a = gph.pixels[y * gph.rowPitch + x];
                if (a == 0) continue;
                int px_ = dx + static_cast<int>(x), py_ = dy + static_cast<int>(y);
                if (px_ < 0 || py_ < 0 || px_ >= w || py_ >= h) continue;
                uint8_t* p = &px[(static_cast<size_t>(py_) * w + px_) * 4];
                float af = a / 255.0f;
                p[0] = static_cast<uint8_t>(p[0] * (1 - af) + 235 * af);
                p[1] = static_cast<uint8_t>(p[1] * (1 - af) + 240 * af);
                p[2] = static_cast<uint8_t>(p[2] * (1 - af) + 245 * af);
                p[3] = 255; ++inked;
            }
        return inked;
    }
    bool WriteBmp(const char* path) const {
        FILE* f = std::fopen(path, "wb");
        if (!f) return false;
        const uint32_t rowBytes = static_cast<uint32_t>(w) * 4;
        const uint32_t imgSize = rowBytes * static_cast<uint32_t>(h);
        uint8_t hdr[54] = {};
        hdr[0] = 'B'; hdr[1] = 'M';
        *reinterpret_cast<uint32_t*>(&hdr[2]) = 54 + imgSize;
        *reinterpret_cast<uint32_t*>(&hdr[10]) = 54;
        *reinterpret_cast<uint32_t*>(&hdr[14]) = 40;
        *reinterpret_cast<int32_t*>(&hdr[18]) = w;
        *reinterpret_cast<int32_t*>(&hdr[22]) = -h;
        *reinterpret_cast<uint16_t*>(&hdr[26]) = 1;
        *reinterpret_cast<uint16_t*>(&hdr[28]) = 32;
        *reinterpret_cast<uint32_t*>(&hdr[34]) = imgSize;
        std::fwrite(hdr, 1, 54, f);
        std::fwrite(px.data(), 1, imgSize, f);
        std::fclose(f);
        return true;
    }
};

// 게임 상태 참여자 — 컷신이 갱신하는 진행 상태(맵·선택·좌표)를 저장한다.
struct GameStateParticipant : ISaveParticipant {
    int64_t     chapter = 0;
    int64_t     branchChoice = -1;
    double      playerX = 0.0;
    std::string mapName;

    std::string_view SectionId() const override { return "gamestate"; }
    void Serialize(ser::IArchive& ar) override {
        uint32_t ver = 1;
        ar.BeginObject("gamestate", ver);
        ar.Key("chapter");      ar.Value(chapter);
        ar.Key("branchChoice"); ar.Value(branchChoice);
        ar.Key("playerX");      ar.Value(playerX);
        ar.Key("mapName");      ar.Value(mapName);
        ar.EndObject();
    }
};

fs::path MakeTempDir() {
    static int counter = 0;
    auto base = fs::temp_directory_path() / ("mye_m6a_smoke_" + std::to_string(counter++));
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);
    return base;
}

} // namespace

// =============================================================================
// M6-A 엔드투엔드: 하나의 Lua 컷신 코루틴이 say/choose/move_to 를 순차 구동하고,
//   흐름 뒤 세이브 왕복 + 씬 전환 시뮬 + 대화창 BMP 덤프까지 한 테스트에서 검증.
// =============================================================================
MYE_TEST(M6AIntegrationCutsceneFlow) {
    EventBus bus;

    // ---- 로컬라이제이션(한글 키 테이블) ----
    LocalizationSystem loc;
    loc.Initialize(&bus);
    auto ko = loc.LoadTableJson(Locale::Korean,
        "{\"npc.greet\":\"여행자여, 어디로 가려는가?\","
        "\"choice.forest\":\"숲으로\",\"choice.town\":\"마을로\"}");
    MYE_EXPECT(static_cast<bool>(ko));
    MYE_EXPECT(loc.Get("npc.greet") == "여행자여, 어디로 가려는가?");

    // ---- ECS World + 플레이어 엔티티(이동 대상) ----
    ecs::World world;
    ecs::Entity player = world.Create();
    scene::LocalTransform& tf = world.Add<scene::LocalTransform>(player);
    tf.position = Vec3{0.0f, 0.0f, 0.0f};

    // ---- 대화·이동·컷신 백엔드(RuntimeModule 배선과 동일 배치, 헤드리스) ----
    DialogueSystem dlg;
    dlg.Initialize(nullptr, &loc, nullptr, &bus);   // box 없음: 코루틴이 상태 폴링

    MoveController move;
    move.Initialize(&world, nullptr);               // nav 없음 → 직선 이동

    CutsceneRuntime cut;
    cut.Initialize(&dlg, &move, nullptr);

    RuntimeBindings bindings(&dlg, &cut, nullptr, nullptr, &loc);

    script::ScriptRuntime rt;
    rt.Initialize(script::StdLibPolicy{}, &bus, nullptr);
    rt.AddBindingModule(&bindings);
    rt.ReapplyBindings();

    sol::state& lua = rt.State();
    lua["_ENT"] = static_cast<double>(player.Packed());
    (void)rt.DoString("_g = { phase=0, choice=-1, arrived=false }", "setup");

    // 하나의 컷신 코루틴: (a) say 한글 → (b) choose 분기 → (c) move_to 대기.
    auto started = rt.DoString(R"LUA(
        mye.co.start(function()
            _g.phase = 1
            mye.dialogue.say("루미나", mye.loc.text("npc.greet"))   -- (a) 한글 대사
            _g.phase = 2
            _g.choice = mye.dialogue.choose({ mye.loc.text("choice.forest"),
                                              mye.loc.text("choice.town") })  -- (b) 선택
            _g.phase = 3
            if _g.choice == 0 then
                mye.cutscene.move_to(_ENT, 12.0, 0.0, 6.0)   -- (c) 숲으로 이동
            else
                mye.cutscene.move_to(_ENT, -8.0, 0.0, 6.0)   -- 마을로 이동
            end
            _g.arrived = true
            _g.phase = 4
        end)
    )LUA", "cutscene.lua");
    MYE_EXPECT(static_cast<bool>(started));

    // (a) say 시작 → ShowingLine, phase=1 에서 대기.
    MYE_EXPECT(int(lua["_g"]["phase"]) == 1);
    MYE_EXPECT(dlg.State() == DialogueState::ShowingLine);
    // 진행 입력 전에는 tick 해도 유지.
    rt.UpdateCoroutines(0.016f);
    MYE_EXPECT(int(lua["_g"]["phase"]) == 1);

    // 진행 입력(Advance) → say 완료 → choose 진입.
    dlg.Advance();
    dlg.Update(0.016f);                 // Finished → Idle
    rt.UpdateCoroutines(0.016f);
    MYE_EXPECT(int(lua["_g"]["phase"]) == 2);
    MYE_EXPECT(dlg.State() == DialogueState::WaitingChoice);

    // (b) 선택지 0(숲으로) → 분기 진입 + move_to 시작.
    dlg.Pick(0);
    rt.UpdateCoroutines(0.016f);
    MYE_EXPECT(int(lua["_g"]["phase"]) == 3);
    MYE_EXPECT(int(lua["_g"]["choice"]) == 0);
    MYE_EXPECT(!lua["_g"]["arrived"].get<bool>());   // 아직 이동 중

    // (c) 이동 시뮬: 거리 12, 속도 6 → 2초. 고정틱 1초씩 재개.
    int ticks = 0;
    for (int i = 0; i < 5 && !lua["_g"]["arrived"].get<bool>(); ++i) {
        cut.UpdateSim(1.0f);            // 시뮬(고정틱): MoveController 전진
        rt.UpdateCoroutines(1.0f);      // 표현: move_done 폴링
        ++ticks;
    }
    MYE_EXPECT(lua["_g"]["arrived"].get<bool>());
    MYE_EXPECT(int(lua["_g"]["phase"]) == 4);
    MYE_EXPECT(rt.Coroutines().ActiveCount() == 0);

    scene::LocalTransform* after = world.TryGet<scene::LocalTransform>(player);
    MYE_EXPECT(after != nullptr);
    float finalX = after ? after->position.x : -999.0f;
    MYE_EXPECT_NEAR(finalX, 12.0f, 1e-2f);

    // =========================================================================
    // (d) 세이브 왕복: 컷신 결과(챕터·분기·좌표·맵)를 저장 → 새 인스턴스 로드 → 일치.
    // =========================================================================
    auto saveDir = MakeTempDir();
    SaveSystem save;
    save.Initialize(nullptr, "saves");
    save.SetUserRootDir(saveDir.string());

    GameStateParticipant gs;
    gs.chapter = 1;
    gs.branchChoice = int(lua["_g"]["choice"]);
    gs.playerX = finalX;
    gs.mapName = "숲";
    save.RegisterParticipant(&gs);

    MYE_EXPECT(!save.Exists(SlotId{0}));
    SaveHeader hdr; hdr.title = "챕터1 자동저장"; hdr.timestampUnix = 1700000000;
    auto wr = save.WriteSlot(SlotId{0}, hdr);
    MYE_EXPECT(static_cast<bool>(wr));
    MYE_EXPECT(save.Exists(SlotId{0}));

    // 원자적 쓰기 흔적 없음.
    std::error_code ec;
    MYE_EXPECT(!fs::exists(saveDir / "saves" / "slot0.save.tmp", ec));
    MYE_EXPECT(fs::exists(saveDir / "saves" / "slot0.save", ec));

    // 새 인스턴스로 로드 → 복원 일치.
    GameStateParticipant restored;
    save.UnregisterParticipant(&gs);
    save.RegisterParticipant(&restored);
    auto rd = save.ReadSlot(SlotId{0});
    MYE_EXPECT(static_cast<bool>(rd));
    MYE_EXPECT(restored.chapter == 1);
    MYE_EXPECT(restored.branchChoice == 0);
    MYE_EXPECT_NEAR(restored.playerX, 12.0, 1e-2);
    MYE_EXPECT(restored.mapName == "숲");

    auto readHdr = save.ReadHeader(SlotId{0});
    MYE_EXPECT(static_cast<bool>(readHdr));
    MYE_EXPECT(readHdr.Value().header.version == SaveSystem::kCurrentVersion);
    MYE_EXPECT(readHdr.Value().header.title == "챕터1 자동저장");

    // =========================================================================
    // (e) 씬 전환: 페이드아웃 → 로딩(poll) → 액티베이트 → 페이드인 → 완료 이벤트.
    // =========================================================================
    int sceneChanged = 0;
    bool sceneOk = false;
    bus.Subscribe<SceneChangedEvent>([&](const SceneChangedEvent& e) {
        ++sceneChanged; sceneOk = e.succeeded; return true;
    });

    int pollCount = 0, activateCount = 0;
    SceneLoaderFn loader;
    loader.begin = [](const SceneRef&) { return SceneLoadTicket{7}; };
    loader.poll = [&](SceneLoadTicket, bool& done) -> float {
        ++pollCount; done = (pollCount >= 3);
        return static_cast<float>(pollCount) / 3.0f;
    };
    loader.activate = [&](SceneLoadTicket) -> Expected<void, Error> {
        ++activateCount; return Expected<void, Error>{};
    };

    SceneTransitionManager sceneMgr;
    sceneMgr.Initialize(nullptr, nullptr, loader, &bus);
    MYE_EXPECT(sceneMgr.Phase() == TransitionPhase::Idle);

    TransitionDesc td; td.fadeOutSec = 0.2f; td.fadeInSec = 0.2f;
    auto sc = sceneMgr.ChangeScene(SceneRef{"assets://scenes/forest.scene"}, td);
    MYE_EXPECT(static_cast<bool>(sc));
    MYE_EXPECT(sceneMgr.Phase() == TransitionPhase::FadeOut);
    MYE_EXPECT(sceneMgr.ConsumesInput());

    // 페이드아웃(0.1*3 > 0.2) → Loading.
    sceneMgr.Update(0.1f);
    float midAlpha = sceneMgr.FadeAlpha();
    MYE_EXPECT(midAlpha > 0.0f && midAlpha < 1.0f);
    sceneMgr.Update(0.1f);
    sceneMgr.Update(0.1f);
    MYE_EXPECT(sceneMgr.Phase() == TransitionPhase::Loading);

    // Loading: poll 3회 → Activating → FadeIn.
    sceneMgr.Update(0.016f);
    MYE_EXPECT(sceneMgr.LoadProgress() > 0.0f && sceneMgr.LoadProgress() < 1.0f);
    sceneMgr.Update(0.016f);
    sceneMgr.Update(0.016f);
    if (sceneMgr.Phase() == TransitionPhase::Activating) sceneMgr.Update(0.016f);
    MYE_EXPECT(activateCount == 1);
    MYE_EXPECT(sceneMgr.Phase() == TransitionPhase::FadeIn);

    // 페이드인 완료 → Idle + SceneChangedEvent(성공).
    sceneMgr.Update(0.1f);
    sceneMgr.Update(0.1f);
    sceneMgr.Update(0.1f);
    MYE_EXPECT(sceneMgr.Phase() == TransitionPhase::Idle);
    MYE_EXPECT(sceneChanged == 1);
    MYE_EXPECT(sceneOk);

    // =========================================================================
    // (f) 대화창 BMP 덤프: 한글 대사 + 선택지 버튼 픽셀. 잉크 수를 센다.
    // =========================================================================
    const std::string bodyMsg   = loc.Get("npc.greet");    // "여행자여, 어디로 가려는가?"
    const std::string speaker   = "루미나";
    const std::string choice0   = loc.Get("choice.forest"); // "숲으로"
    const std::string choice1   = loc.Get("choice.town");   // "마을로"

    // 폰트: 등장 한글 음절 커버.
    std::vector<uint32_t> hangul;
    auto addHangul = [&](const std::string& s) {
        for (char32_t c : Utf8ToUtf32(s))
            if (c >= 0xAC00 && c <= 0xD7A3) hangul.push_back(static_cast<uint32_t>(c));
    };
    addHangul(bodyMsg); addHangul(speaker); addHangul(choice0); addHangul(choice1);
    auto ttf = testfont::MakeTestFont(hangul);
    auto faceRes = text::FreeTypeFace::Create(text::FontId{0}, AsBytes(ttf));
    MYE_EXPECT(faceRes.HasValue());
    if (!faceRes.HasValue()) return;
    auto face = std::move(faceRes.Value());

    CpuCanvas canvas(960, 540);
    canvas.FillRect(0, 0, 960, 540, 12, 12, 18);           // 배경
    // 대화창(하단) + 이름표.
    const int boxX = 60, boxY = 360, boxW = 840, boxH = 150;
    canvas.FillRect(boxX, boxY, boxW, boxH, 30, 35, 52);   // 대화창 프레임
    canvas.FillRect(boxX + 20, boxY - 26, 140, 30, 40, 46, 70); // 이름표
    // 선택지 버튼 두 개(대화창 우측).
    const int btn0X = boxX + 560, btn0Y = boxY + 28, btnW = 240, btnH = 40;
    const int btn1Y = btn0Y + 54;
    canvas.FillRect(btn0X, btn0Y, btnW, btnH, 100, 100, 116); // hover 밝게
    canvas.FillRect(btn0X, btn1Y, btnW, btnH, 70, 70, 82);

    auto drawText = [&](const std::string& s, int x, int y, uint16_t px) {
        int ink = 0, penX = x;
        for (char32_t cp : Utf8ToUtf32(s)) {
            if (cp == U' ') { penX += px / 3; continue; }
            text::RasterBitmap g = face->rasterize(cp, px, text::GlyphStyle::Normal,
                                                   text::HintMode::Normal);
            int bx = penX + g.bearing.x;
            int by = y - g.bearing.y;
            ink += canvas.BlitGlyph(g, bx, by);
            penX += g.advance ? g.advance : static_cast<int16_t>(px);
        }
        return ink;
    };

    int inkSpeaker = drawText(speaker, boxX + 28, boxY - 4, 22);
    int inkBody    = drawText(bodyMsg, boxX + 24, boxY + 48, 26);
    int inkC0      = drawText(choice0, btn0X + 16, btn0Y + 28, 22);
    int inkC1      = drawText(choice1, btn0X + 16, btn1Y + 28, 22);
    int totalInk = inkSpeaker + inkBody + inkC0 + inkC1;

    // 증명: 모든 텍스트 요소가 실제 잉크를 냈다(빈 사각형 아님).
    MYE_EXPECT(inkSpeaker > 0);
    MYE_EXPECT(inkBody > 0);
    MYE_EXPECT(inkC0 > 0);
    MYE_EXPECT(inkC1 > 0);
    MYE_EXPECT(totalInk > 1500);

    const bool wrote = canvas.WriteBmp("m6a_dialogue.bmp");
    MYE_EXPECT(wrote);

    std::printf("[m6a-smoke] flow: say->advance ok, choose=%d(forest), move ticks=%d finalX=%.2f\n",
                int(lua["_g"]["choice"]), ticks, finalX);
    std::printf("[m6a-smoke] save: roundtrip chapter=%lld branch=%lld playerX=%.2f map=%s\n",
                static_cast<long long>(restored.chapter),
                static_cast<long long>(restored.branchChoice),
                restored.playerX, restored.mapName.c_str());
    std::printf("[m6a-smoke] scene: polls=%d activate=%d changed=%d ok=%d midAlpha=%.2f\n",
                pollCount, activateCount, sceneChanged, sceneOk ? 1 : 0, midAlpha);
    std::printf("[m6a-smoke] bmp: hangul=%zu inkSpeaker=%d inkBody=%d inkC0=%d inkC1=%d total=%d -> m6a_dialogue.bmp\n",
                hangul.size(), inkSpeaker, inkBody, inkC0, inkC1, totalInk);

    rt.Shutdown();
    (void)save.DeleteSlot(SlotId{0});
    fs::remove_all(saveDir, ec);
}
