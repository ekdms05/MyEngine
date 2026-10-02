// NetGameServerTests.cpp — 넷↔게임플레이↔영속 통합 루프백 (M9·M8·M10 통합)
//
// 인증 접속 → 캐릭터 세션 로드 → 서버권위 이동 → 퇴장 시 위치 저장. 실제 UDP(127.0.0.1). 소켓 불가 시 스킵.
#include "TestFramework.h"

#include "mye/gameserver/NetGameServer.h"
#include "mye/net/NetClient.h"
#include "mye/net/UdpSocket.h"

#include <chrono>
#include <cmath>
#include <limits>
#include <thread>

using namespace mye;
using namespace mye::gameserver;

namespace { void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); } }

MYE_TEST(NetGameServer3DAuthorityPredictionOwnershipAndPersistence) {
    net::NetSubsystem sys;
    MYE_EXPECT(sys.ok);
    if (!sys.ok) return;
    persist::PersistenceService persistence;
    const auto a = persistence.Accounts().Register("xyz-a", "test-password").Value();
    const auto b = persistence.Accounts().Register("xyz-b", "test-password").Value();
    const auto ca = persistence.Characters().Create(a, "XYZ A").Value();
    const auto cb = persistence.Characters().Create(b, "XYZ B").Value();
    phys::PhysicsWorld3D physics;
    phys::MotionSettings3D settings;
    MYE_EXPECT(physics.Add({1, {{0, -.5f, 0}, {20, .5f, 20}}}));
    MYE_EXPECT(physics.Add({2, {{2, 2, 0}, {.2f, 2, 20}}}));
    MYE_EXPECT(physics.Add({3, {{0, .5f, 2}, {1, .5f, 2}}, phys::Shape3D::Ramp}));
    NetGameServer server(persistence);
    MYE_EXPECT(server.Configure3D(physics, settings, 123, "assets/online.scene", {0, 0, -1}));
    MYE_EXPECT(server.Start(0));
    if (!server.IsRunning()) return;
    const auto endpoint = net::Endpoint::Loopback(server.Port());
    net::NetClient first, second, badOwner, badScene, duplicate;
    const auto connect = [&](net::NetClient& client, uint64_t hash, uint64_t character,
                             std::string_view user) {
        MYE_EXPECT(client.Open());
        MYE_EXPECT(client.Configure3D(physics, settings, hash, character));
        client.Connect(endpoint, user, "test-password");
    };
    connect(first, 123, ca, "xyz-a");
    connect(second, 123, cb, "xyz-b");
    for (int i = 0; i < 10; ++i) {
        server.Tick(net::kFixedDelta3D);
        first.Receive();
        second.Receive();
        SleepMs(1);
    }
    MYE_EXPECT(first.Connected() && second.Connected());
    MYE_EXPECT(first.LatestSnapshot3D().size() == 2 && second.LatestSnapshot3D().size() == 2);
    connect(badOwner, 123, cb, "xyz-a");
    connect(badScene, 999, ca, "xyz-a");
    connect(duplicate, 123, ca, "xyz-a");
    for (int i = 0; i < 4; ++i) {
        server.Tick(net::kFixedDelta3D);
        badOwner.Receive();
        badScene.Receive();
        duplicate.Receive();
    }
    MYE_EXPECT(!badOwner.Connected() && !badScene.Connected() && !duplicate.Connected());
    MYE_EXPECT(server.PlayerCount() == 2);
    for (int i = 0; i < 90; ++i) {
        MYE_EXPECT(first.SendInput3D({0, 1}, i == 20));
        MYE_EXPECT(second.SendInput3D({1, 0}, false));
        server.Tick(net::kFixedDelta3D);
        first.Receive();
        second.Receive();
    }
    phys::MotionState3D predicted, authority;
    MYE_EXPECT(first.GetPredicted3D(predicted));
    MYE_EXPECT(server.Net().GetEntity3D(first.Id(), authority));
    MYE_EXPECT(authority.position.z > 2 && authority.position.y > .5f);
    MYE_EXPECT_NEAR(predicted.position.x, authority.position.x, .001f);
    MYE_EXPECT_NEAR(predicted.position.y, authority.position.y, .001f);
    MYE_EXPECT_NEAR(predicted.position.z, authority.position.z, .001f);
    MYE_EXPECT(server.Net().GetEntity3D(second.Id(), authority));
    MYE_EXPECT(authority.position.x < 1.51f);
    first.Disconnect();
    second.Disconnect();
    server.Tick(net::kFixedDelta3D);
    MYE_EXPECT(server.PlayerCount() == 0);
    const auto* saved = persistence.Characters().Get(ca);
    MYE_EXPECT(saved->world3D && saved->posY > .5f && saved->posZ > 2 &&
               saved->sceneId == "assets/online.scene");
    persist::CharacterStore copy;
    MYE_EXPECT(copy.LoadJson(persistence.Characters().ToJson()));
    MYE_EXPECT_NEAR(copy.Get(ca)->posZ, saved->posZ, 0);
    connect(first, 123, ca, "xyz-a");
    for (int i = 0; i < 3; ++i) {
        server.Tick(net::kFixedDelta3D);
        first.Receive();
    }
    MYE_EXPECT(first.GetPredicted3D(predicted));
    MYE_EXPECT_NEAR(predicted.position.z, saved->posZ, .001f);
    // A syntactically valid but noncontiguous batch must not partially change authority.
    net::UdpSocket raw;
    MYE_EXPECT(raw.Open());
    net::BitWriter join;
    net::WriteHeader(join, net::MsgType::Connect3D);
    net::WriteString(join, "xyz-b");
    net::WriteString(join, "test-password");
    net::WriteU64(join, 123);
    net::WriteU64(join, cb);
    net::WriteU64(join, 42);
    auto bytes = join.Finish();
    MYE_EXPECT(raw.SendTo(endpoint, bytes.data(), bytes.size()) == int(bytes.size()));
    server.Tick(net::kFixedDelta3D);
    uint8_t buffer[1400];
    net::Endpoint from;
    uint64_t token = 0;
    uint32_t rawId = 0;
    for (int attempt = 0; attempt < 100 && !token; ++attempt) {
        const int n = raw.RecvFrom(from, buffer, sizeof(buffer));
        if (n > 0) {
            net::BitReader accepted(buffer, n);
            net::MsgType message;
            if (net::ReadHeader(accepted, message) && message == net::MsgType::Accept3D) {
                rawId = accepted.ReadBits(32);
                token = net::ReadU64(accepted);
            }
        }
        SleepMs(1);
    }
    MYE_EXPECT(token && rawId);
    MYE_EXPECT(server.Net().GetEntity3D(rawId, authority));
    const auto rawPosition = authority.position;
    net::BitWriter gap;
    net::WriteHeader(gap, net::MsgType::Input3D);
    net::WriteU64(gap, token);
    gap.WriteBits(2, 8);
    net::WriteInput3D(gap, {1, {-1, 0}, false});
    net::WriteInput3D(gap, {3, {-1, 0}, false});
    bytes = gap.Finish();
    MYE_EXPECT(raw.SendTo(endpoint, bytes.data(), bytes.size()) == int(bytes.size()));
    server.Tick(net::kFixedDelta3D);
    MYE_EXPECT(server.Net().GetEntity3D(rawId, authority));
    MYE_EXPECT_NEAR(authority.position.x, rawPosition.x, 0);
    MYE_EXPECT(server.Stop());
    net::BitWriter w;
    net::WriteHeader(w, net::MsgType::Input3D);
    net::WriteU64(w, 42);
    net::WriteInput3D(w, {1, {std::numeric_limits<float>::quiet_NaN(), 0}, false});
    net::BitReader reader(w.Finish());
    net::MsgType type;
    MYE_EXPECT(net::ReadHeader(reader, type));
    MYE_EXPECT(net::ReadU64(reader) == 42);
    net::Input3D invalid;
    MYE_EXPECT(!net::ReadInput3D(reader, invalid));
    NetGameServer legacy(persistence);
    net::NetClient xy;
    MYE_EXPECT(legacy.Start(0) && xy.Open());
    xy.Connect(net::Endpoint::Loopback(legacy.Port()), "xyz-a", "test-password");
    for (int i = 0; i < 5; ++i) {
        legacy.Tick(net::kFixedDelta3D);
        xy.Receive();
        SleepMs(1);
    }
    MYE_EXPECT(!xy.Connected() && legacy.PlayerCount() == 0);
    MYE_EXPECT(legacy.Stop());
    predicted.position.x = 100001;
    MYE_EXPECT(!net::ValidState3D(predicted));
}

MYE_TEST(NetGameServerAuthJoinMovePersist) {
    net::NetSubsystem sys;
    if (!sys.ok) return;

    // 계정 + 캐릭터 준비(시작 위치 지정).
    persist::PersistenceService p;
    const auto acc = p.Accounts().Register("player1", "pw").Value();
    const auto cid = p.Characters().Create(acc, "Hero").Value();
    p.Characters().GetMutable(cid)->posX = 0.0f;
    p.Characters().GetMutable(cid)->posY = 0.0f;

    NetGameServer server(p);
    net::NetClient client;
    if (!server.Start(0) || !client.Open(0)) return;
    const float dt = 1.0f / 60.0f;
    server.SetMoveSpeed(6.0f);
    client.SetMoveSpeed(6.0f);

    const net::Endpoint sep = net::Endpoint::Loopback(server.Port());
    client.Connect(sep, "player1", "pw");   // 인증 접속

    // 핸드셰이크 + 세션 생성까지 펌프.
    for (int i = 0; i < 500 && !client.Connected(); ++i) { server.Tick(dt); client.Receive(); SleepMs(1); }
    MYE_EXPECT(client.Connected());

    // 몇 틱 더 돌려 세션이 붙는지 확인.
    for (int i = 0; i < 5; ++i) { server.Tick(dt); client.Receive(); SleepMs(1); }
    MYE_EXPECT(server.PlayerCount() == 1);
    const SessionId sid = server.SessionOf(client.Id());
    MYE_EXPECT(sid != 0);
    const PlayerSession* s = server.Game().Get(sid);
    MYE_EXPECT(s != nullptr && s->characterId == cid && s->accountId == acc);
    // 캐릭터 스탯이 런타임에 로드됨(신규라 HP 최대치).
    MYE_EXPECT(s->stats.derived.maxHp > 0 && s->stats.hp == s->stats.derived.maxHp);

    // +X 이동 입력 → 서버권위 위치 전진.
    for (int i = 0; i < 60; ++i) {
        client.SendInput(static_cast<uint32_t>(i + 1), 1.0f, 0.0f, dt);
        SleepMs(1);
        server.Tick(dt);
        SleepMs(1);
        client.Receive();
    }
    MYE_EXPECT(server.Game().Get(sid)->x > 1.0f);   // 세션에 권위 위치 동기됨

    // 퇴장 → 세션 저장(마지막 위치가 CharacterRecord 에 반영).
    client.Disconnect();
    for (int i = 0; i < 30; ++i) { server.Tick(dt); SleepMs(1); }
    MYE_EXPECT(server.PlayerCount() == 0);

    const persist::CharacterRecord* rec = p.Characters().Get(cid);
    MYE_EXPECT(rec != nullptr);
    MYE_EXPECT(rec->posX > 1.0f);   // 이동한 위치가 저장됨
}

// 틀린 자격증명은 세션을 얻지 못한다(인증 관문).
MYE_TEST(NetGameServerRejectsBadCredentials) {
    net::NetSubsystem sys;
    if (!sys.ok) return;

    persist::PersistenceService p;
    const auto acc = p.Accounts().Register("player2", "right").Value();
    (void)p.Characters().Create(acc, "Hero2");

    NetGameServer server(p);
    net::NetClient bad;
    if (!server.Start(0) || !bad.Open(0)) return;
    const float dt = 1.0f / 60.0f;

    const net::Endpoint sep = net::Endpoint::Loopback(server.Port());
    bad.Connect(sep, "player2", "wrong");   // 틀린 비밀번호

    for (int i = 0; i < 200; ++i) { server.Tick(dt); bad.Receive(); SleepMs(1); }
    MYE_EXPECT(!bad.Connected());
    MYE_EXPECT(server.PlayerCount() == 0);   // 세션 없음
}

MYE_TEST(NetGameServerStopPreservesActiveSession) {
    net::NetSubsystem sys;
    MYE_EXPECT(sys.ok);
    if (!sys.ok) return;
    persist::PersistenceService p;
    const auto acc = p.Accounts().Register("active", "pw").Value();
    const auto cid = p.Characters().Create(acc, "ActivePlayer").Value();
    NetGameServer server(p);
    net::NetClient client;
    const bool ready = server.Start(0) && client.Open(0);
    MYE_EXPECT(ready);
    if (!ready) return;
    client.Connect(net::Endpoint::Loopback(server.Port()), "active", "pw");
    for (int i = 0; i < 500 && !client.Connected(); ++i) {
        server.Tick(1.0f / 60.0f);
        client.Receive();
        SleepMs(1);
    }
    MYE_EXPECT(client.Connected());
    const auto sid = server.SessionOf(client.Id());
    auto* session = server.Game().Get(sid);
    MYE_EXPECT(session != nullptr);
    if (!session) return;
    session->x = 12.0f;
    session->y = -3.0f;
    session->stats.hp = 7;
    session->prog.xp = 123;
    MYE_EXPECT(server.Stop());
    const auto* record = p.Characters().Get(cid);
    MYE_EXPECT(record->posX == 12.0f && record->posY == -3.0f);
    MYE_EXPECT(record->hp == 7 && record->xp == 123);
    MYE_EXPECT(server.Game().SessionCount() == 0);
}
