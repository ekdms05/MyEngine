// NetGameServerTests.cpp — 넷↔게임플레이↔영속 통합 루프백 (M9·M8·M10 통합)
//
// 인증 접속 → 캐릭터 세션 로드 → 서버권위 이동 → 퇴장 시 위치 저장. 실제 UDP(127.0.0.1). 소켓 불가 시 스킵.
#include "TestFramework.h"

#include "mye/gameserver/NetGameServer.h"
#include "mye/net/NetClient.h"
#include "mye/net/UdpSocket.h"

#include <chrono>
#include <cmath>
#include <thread>

using namespace mye;
using namespace mye::gameserver;

namespace { void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); } }

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
