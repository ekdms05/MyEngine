// RuntimeSaveLocaleTests.cpp — M6-A 헤드리스 테스트: 세이브 왕복·settings·로컬라이제이션·씬 전환
//
// 소유: 06 (M6-A, 에이전트 world). 검증 범위:
//   1) SaveSystem 슬롯 왕복 — ISaveParticipant 기여 데이터 저장→복원 일치, 버전 헤더, 원자적 쓰기.
//   2) settings 슬롯(kSettingsSlot) 저장/로드(볼륨·언어).
//   3) 마이그레이션 체인(구버전 세이브 승격) + VersionMismatch(미래 버전 거부).
//   4) LocalizationSystem — JSON 테이블 로드·키 조회·폴백 체인·자리표시자·언어 전환 이벤트.
//   5) SceneTransitionManager — 페이드아웃→로딩→액티베이트→페이드인 순서·진행률·완료 이벤트.
#include "TestFramework.h"

#include "mye/core/Events.h"
#include "mye/asset/FileSystem.h"
#include "mye/ser/Archive.h"

#include "mye/runtime/SaveSystem.h"
#include "mye/runtime/Localization.h"
#include "mye/runtime/SceneTransition.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace mye;
using namespace mye::runtime;

namespace {

namespace fs = std::filesystem;

int g_saveTempCounter = 0;
fs::path MakeSaveTempDir() {
    auto base = fs::temp_directory_path() /
                ("mye_save_test_" + std::to_string(g_saveTempCounter++));
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);
    return base;
}

// 플레이어 상태 참여자 — IArchive 프리미티브로 직접 대칭 직렬화(리플렉션 불필요).
struct PlayerSaveParticipant : ISaveParticipant {
    int64_t     level = 0;
    double      hp = 0.0;
    std::string mapName;
    bool        alive = false;

    std::string_view SectionId() const override { return "player"; }
    void Serialize(ser::IArchive& ar) override {
        uint32_t ver = 1;
        ar.BeginObject("player", ver);
        ar.Key("level");   ar.Value(level);
        ar.Key("hp");      ar.Value(hp);
        ar.Key("mapName"); ar.Value(mapName);
        ar.Key("alive");   ar.Value(alive);
        ar.EndObject();
    }
};

// settings 참여자 — 볼륨·언어(정수 태그).
struct SettingsSaveParticipant : ISaveParticipant {
    double  masterVolume = 1.0;
    int64_t localeTag = 0;   // 0=ko, 1=en

    std::string_view SectionId() const override { return "settings"; }
    void Serialize(ser::IArchive& ar) override {
        uint32_t ver = 1;
        ar.BeginObject("settings", ver);
        ar.Key("masterVolume"); ar.Value(masterVolume);
        ar.Key("localeTag");    ar.Value(localeTag);
        ar.EndObject();
    }
};

} // namespace

// ---------------------------------------------------------------------------
// 1) 세이브 슬롯 왕복 + 버전 헤더 + 원자적 쓰기(temp 잔재 없음).
// ---------------------------------------------------------------------------
MYE_TEST(SaveSystemRoundtrip) {
    auto dir = MakeSaveTempDir();

    SaveSystem save;
    save.Initialize(nullptr, "saves");
    save.SetUserRootDir(dir.string());

    PlayerSaveParticipant p;
    p.level = 7; p.hp = 88.5; p.mapName = "마을"; p.alive = true;
    save.RegisterParticipant(&p);

    MYE_EXPECT(!save.Exists(SlotId{0}));

    SaveHeader hdr;
    hdr.title = "챕터1";
    hdr.timestampUnix = 1700000000;
    hdr.playTimeSec = 123.5;
    auto wr = save.WriteSlot(SlotId{0}, hdr);
    MYE_EXPECT(static_cast<bool>(wr));
    MYE_EXPECT(save.Exists(SlotId{0}));

    // 원자적 쓰기 검증: .save.tmp 잔재가 없어야 한다.
    std::error_code ec;
    MYE_EXPECT(!fs::exists(dir / "saves" / "slot0.save.tmp", ec));
    MYE_EXPECT(fs::exists(dir / "saves" / "slot0.save", ec));

    // 헤더만 조회(버전이 코드 정본으로 기록됐는지).
    auto hr = save.ReadHeader(SlotId{0});
    MYE_EXPECT(static_cast<bool>(hr));
    MYE_EXPECT(hr.Value().exists);
    MYE_EXPECT(hr.Value().header.version == SaveSystem::kCurrentVersion);
    MYE_EXPECT(hr.Value().header.title == "챕터1");
    MYE_EXPECT(hr.Value().header.timestampUnix == 1700000000);

    // 다른 인스턴스로 복원 → 값 일치.
    PlayerSaveParticipant q;
    save.UnregisterParticipant(&p);
    save.RegisterParticipant(&q);
    auto rd = save.ReadSlot(SlotId{0});
    MYE_EXPECT(static_cast<bool>(rd));
    MYE_EXPECT(q.level == 7);
    MYE_EXPECT_NEAR(q.hp, 88.5, 1e-9);
    MYE_EXPECT(q.mapName == "마을");
    MYE_EXPECT(q.alive == true);

    // 삭제.
    auto del = save.DeleteSlot(SlotId{0});
    MYE_EXPECT(static_cast<bool>(del));
    MYE_EXPECT(!save.Exists(SlotId{0}));

    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// 2) settings 슬롯 저장/로드(볼륨·언어).
// ---------------------------------------------------------------------------
MYE_TEST(SaveSystemSettingsSlot) {
    auto dir = MakeSaveTempDir();
    SaveSystem save;
    save.Initialize(nullptr, "saves");
    save.SetUserRootDir(dir.string());

    SettingsSaveParticipant s;
    s.masterVolume = 0.6; s.localeTag = 1;   // en
    save.RegisterParticipant(&s);

    SaveHeader hdr; hdr.title = "settings";
    MYE_EXPECT(static_cast<bool>(save.WriteSlot(kSettingsSlot, hdr)));
    // 슬롯 경로 규약: settings.save.
    std::error_code ec;
    MYE_EXPECT(fs::exists(dir / "saves" / "settings.save", ec));

    SettingsSaveParticipant loaded;
    save.UnregisterParticipant(&s);
    save.RegisterParticipant(&loaded);
    MYE_EXPECT(static_cast<bool>(save.ReadSlot(kSettingsSlot)));
    MYE_EXPECT_NEAR(loaded.masterVolume, 0.6, 1e-9);
    MYE_EXPECT(loaded.localeTag == 1);

    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// 3) 미래 버전 거부(VersionMismatch) + 부재 슬롯 읽기 실패.
// ---------------------------------------------------------------------------
MYE_TEST(SaveSystemVersionAndMissing) {
    auto dir = MakeSaveTempDir();
    SaveSystem save;
    save.Initialize(nullptr, "saves");
    save.SetUserRootDir(dir.string());

    // 부재 슬롯 읽기는 실패(NotFound).
    auto miss = save.ReadSlot(SlotId{3});
    MYE_EXPECT(!miss.HasValue());

    // 미래 버전 세이브 파일을 손으로 심어 VersionMismatch 유도.
    fs::create_directories(dir / "saves");
    const std::string future =
        "{\"__header\":{\"magic\":1297696083,\"version\":999,\"timestampUnix\":0,"
        "\"playTimeSec\":0.0,\"title\":\"\",\"thumbnailVpath\":\"\"},\"__payload\":{}}";
    {
        std::ofstream out(dir / "saves" / "slot5.save", std::ios::binary);
        out.write(future.data(), static_cast<std::streamsize>(future.size()));
    }
    auto r = save.ReadSlot(SlotId{5});
    MYE_EXPECT(!r.HasValue());
    MYE_EXPECT(r.GetError().code == static_cast<int32_t>(RuntimeError::VersionMismatch));

    std::error_code ec;
    fs::remove_all(dir, ec);
}

// 마이그레이션: v0 페이로드의 player.level 을 +100 승격(스키마 변경 시뮬레이션).
struct V0ToV1Migration : ISaveMigration {
    uint32_t FromVersion() const override { return 0; }
    Expected<void, Error> Migrate(std::string& payloadJson) override {
        // payload 는 SaveSystem 이 json::Stringify(pretty, indent=2)로 만든 문자열이다.
        //   따라서 키-값 구분자는 ": "(공백 포함). "level": 5 → "level": 105 치환.
        const std::string from = "\"level\": 5";
        const std::string to   = "\"level\": 105";
        auto pos = payloadJson.find(from);
        if (pos != std::string::npos) payloadJson.replace(pos, from.size(), to);
        return Expected<void, Error>{};
    }
};

// ---------------------------------------------------------------------------
// 3b) 마이그레이션 체인 — v0 세이브를 v1 로 승격 후 참여자 Load.
// ---------------------------------------------------------------------------
MYE_TEST(SaveSystemMigrationChain) {
    auto dir = MakeSaveTempDir();
    SaveSystem save;
    save.Initialize(nullptr, "saves");
    save.SetUserRootDir(dir.string());
    save.RegisterMigration(std::make_unique<V0ToV1Migration>());

    // v0 세이브를 손으로 심는다(player.level=5, __payload 는 참여자 섹션 스키마와 동일).
    fs::create_directories(dir / "saves");
    const std::string v0 =
        "{\"__header\":{\"magic\":1297696083,\"version\":0,\"timestampUnix\":0,"
        "\"playTimeSec\":0.0,\"title\":\"old\",\"thumbnailVpath\":\"\"},"
        "\"__payload\":{\"player\":{\"__version\":1,\"level\":5,\"hp\":10.0,"
        "\"mapName\":\"숲\",\"alive\":true}}}";
    {
        std::ofstream out(dir / "saves" / "slot1.save", std::ios::binary);
        out.write(v0.data(), static_cast<std::streamsize>(v0.size()));
    }

    PlayerSaveParticipant p;
    save.RegisterParticipant(&p);
    auto r = save.ReadSlot(SlotId{1});
    MYE_EXPECT(static_cast<bool>(r));
    MYE_EXPECT(p.level == 105);   // 마이그레이션 적용됨(5 → 105)
    MYE_EXPECT(p.mapName == "숲");

    std::error_code ec;
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// 4) 로컬라이제이션 — JSON 로드·키 조회·폴백·자리표시자·언어 전환 이벤트.
// ---------------------------------------------------------------------------
MYE_TEST(LocalizationLoadFormatFallbackSwitch) {
    EventBus bus;
    int localeChanges = 0;
    uint8_t lastLocale = 255;
    bus.Subscribe<LocaleChangedEvent>([&](const LocaleChangedEvent& e) {
        ++localeChanges; lastLocale = e.locale; return true;
    });

    LocalizationSystem loc;
    loc.Initialize(&bus);

    auto ko = loc.LoadTableJson(Locale::Korean,
        "{\"greeting\":\"안녕하세요, {name}님\",\"only_ko\":\"한글전용\"}");
    MYE_EXPECT(static_cast<bool>(ko));
    auto en = loc.LoadTableJson(Locale::English,
        "{\"greeting\":\"Hello, {name}\",\"only_en\":\"english only\"}");
    MYE_EXPECT(static_cast<bool>(en));

    // 파싱 실패 케이스.
    MYE_EXPECT(!loc.LoadTableJson(Locale::Korean, "{ not json").HasValue());

    // 기본 로케일 ko 조회 + 자리표시자.
    MYE_EXPECT(loc.CurrentLocale() == Locale::Korean);
    MYE_EXPECT(loc.Get("greeting") == "안녕하세요, {name}님");
    MYE_EXPECT(loc.Format("greeting", {{"name", "홍길동"}}) == "안녕하세요, 홍길동님");

    // 미지 키 → key 문자열 그대로.
    MYE_EXPECT(loc.Get("no.such.key") == "no.such.key");

    // 로케일 전환 → 이벤트 발행.
    loc.SetLocale(Locale::English);
    MYE_EXPECT(localeChanges == 1);
    MYE_EXPECT(lastLocale == static_cast<uint8_t>(Locale::English));
    MYE_EXPECT(loc.Format("greeting", {{"name", "Gil"}}) == "Hello, Gil");

    // 폴백 체인: en 현재인데 en 에만 없고 ko 에 있는 키 → ko 폴백.
    MYE_EXPECT(loc.Get("only_ko") == "한글전용");

    // 같은 로케일 재설정은 이벤트 없음.
    loc.SetLocale(Locale::English);
    MYE_EXPECT(localeChanges == 1);

    // LocalizedText 해석: literal 은 그대로, keyed 는 Format.
    MYE_EXPECT(loc.Resolve(LocalizedText::Literal("원문")) == "원문");
    MYE_EXPECT(loc.Resolve(LocalizedText::Key("only_en")) == "english only");

    loc.Shutdown();
}

// ---------------------------------------------------------------------------
// 5) 씬 전환 상태기계 — 페이드/로드/액티베이트/완료 순서·진행률·이벤트.
// ---------------------------------------------------------------------------
MYE_TEST(SceneTransitionStateMachine) {
    EventBus bus;
    int changed = 0;
    bool changedOk = false;
    bus.Subscribe<SceneChangedEvent>([&](const SceneChangedEvent& e) {
        ++changed; changedOk = e.succeeded; return true;
    });

    // 가짜 로더: 3회 poll 후 done, activate 성공.
    int pollCount = 0;
    int activateCount = 0;
    SceneLoaderFn loader;
    loader.begin = [](const SceneRef&) { return SceneLoadTicket{42}; };
    loader.poll = [&](SceneLoadTicket t, bool& outDone) -> float {
        (void)t;
        ++pollCount;
        outDone = (pollCount >= 3);
        return static_cast<float>(pollCount) / 3.0f;
    };
    loader.activate = [&](SceneLoadTicket t) -> Expected<void, Error> {
        (void)t; ++activateCount; return Expected<void, Error>{};
    };

    SceneTransitionManager mgr;
    mgr.Initialize(nullptr, nullptr, loader, &bus);

    MYE_EXPECT(mgr.Phase() == TransitionPhase::Idle);
    MYE_EXPECT(!mgr.IsTransitioning());

    TransitionDesc desc;
    desc.fadeOutSec = 0.2f;
    desc.fadeInSec = 0.2f;
    auto started = mgr.ChangeScene(SceneRef{"assets://scenes/village.scene"}, desc);
    MYE_EXPECT(static_cast<bool>(started));
    MYE_EXPECT(mgr.Phase() == TransitionPhase::FadeOut);
    MYE_EXPECT(mgr.ConsumesInput());

    // 재진입 거부(Busy).
    auto busy = mgr.ChangeScene(SceneRef{"assets://x"}, desc);
    MYE_EXPECT(!busy.HasValue());
    MYE_EXPECT(busy.GetError().code == static_cast<int32_t>(RuntimeError::Busy));

    // 페이드아웃 완료까지 tick(0.1s * 3 = 0.3s > 0.2s).
    mgr.Update(0.1f);
    MYE_EXPECT(mgr.FadeAlpha() > 0.0f && mgr.FadeAlpha() < 1.0f);
    mgr.Update(0.1f);
    mgr.Update(0.1f);
    // fadeOut 완료 → Loading 진입 + begin 호출됨.
    MYE_EXPECT(mgr.Phase() == TransitionPhase::Loading);
    MYE_EXPECT_NEAR(mgr.FadeAlpha(), 1.0f, 1e-4);

    // Loading: poll 3회 후 done → Activating.
    mgr.Update(0.016f);   // poll 1 (progress ~0.33)
    MYE_EXPECT(mgr.LoadProgress() > 0.0f && mgr.LoadProgress() < 1.0f);
    mgr.Update(0.016f);   // poll 2
    mgr.Update(0.016f);   // poll 3 → done
    // Activating 은 즉시 처리되어 FadeIn 으로 넘어감(같은 Update 내 or 다음).
    // 다음 Update 에서 Activating→FadeIn 처리.
    if (mgr.Phase() == TransitionPhase::Activating) mgr.Update(0.016f);
    MYE_EXPECT(activateCount == 1);
    MYE_EXPECT(mgr.Phase() == TransitionPhase::FadeIn);

    // 페이드인 완료 → Idle + SceneChangedEvent(성공).
    mgr.Update(0.1f);
    mgr.Update(0.1f);
    mgr.Update(0.1f);
    MYE_EXPECT(mgr.Phase() == TransitionPhase::Idle);
    MYE_EXPECT(!mgr.IsTransitioning());
    MYE_EXPECT_NEAR(mgr.FadeAlpha(), 0.0f, 1e-4);
    MYE_EXPECT(changed == 1);
    MYE_EXPECT(changedOk);

    mgr.Shutdown();
}
