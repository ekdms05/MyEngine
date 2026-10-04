#include "TestFramework.h"
#include "mye/gameserver/NetGameServer.h"
#include "mye/net/NetClient.h"
#include <chrono>
#include <thread>

using namespace mye;
namespace {
void CombatPoll(gameserver::NetGameServer& server, net::NetClient& a, net::NetClient& b, int ticks = 1) {
    for (int i = 0; i < ticks; ++i) {
        server.Tick(net::kFixedDelta2D); a.Receive(); b.Receive();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
}
MYE_TEST(OnlineCombatChecksRangeWallCooldownDeathAndReconnect) {
    net::NetSubsystem network;
    MYE_EXPECT(network.ok); if (!network.ok) return;
    for (const bool blocked : {false, true}) {
        persist::PersistenceService persistence;
        const auto aa = persistence.Accounts().Register("combat-a", "test-password").Value();
        const auto ab = persistence.Accounts().Register("combat-b", "test-password").Value();
        const auto ca = persistence.Characters().Create(aa, "Combat A").Value();
        const auto cb = persistence.Characters().Create(ab, "Combat B").Value();
        auto* record = persistence.Characters().GetMutable(cb);
        record->sceneId = "assets/combat.scene"; record->posX = 1;
        phys::MotionSettings2D settings;
        settings.body.id = 1; settings.body.shape = phys::Shape2D::MakeCircle(.1f);
        std::vector<phys::CollisionBody2D> walls;
        if (blocked) {
            phys::CollisionBody2D wall;
            wall.id = 2; wall.pos = {.5f, 0}; wall.shape = phys::Shape2D::MakeBox(.01f, 10);
            walls.push_back(wall);
        }
        gameserver::NetGameServer server(persistence);
        MYE_EXPECT(server.Configure2D(walls, settings, 92, "assets/combat.scene", {}));
        MYE_EXPECT(!server.ConfigureCombat2D({2, 1, 0}));
        MYE_EXPECT(server.ConfigureCombat2D({2, 1, 60}));
        MYE_EXPECT(server.Start(0));
        net::NetClient a, b;
        MYE_EXPECT(a.Open() && b.Open());
        MYE_EXPECT(a.Configure2D(walls, settings, 92, ca) && b.Configure2D(walls, settings, 92, cb));
        const auto endpoint = net::Endpoint::Loopback(server.Port());
        a.Connect(endpoint, "combat-a", "test-password"); b.Connect(endpoint, "combat-b", "test-password");
        CombatPoll(server, a, b, 4);
        MYE_EXPECT(a.Connected() && b.Connected() && b.LatestHealth2D().size() == 2);
        const auto targetId = server.SessionOf(b.Id());
        auto* target = server.Game().Get(targetId);
        MYE_EXPECT(target); if (!target) return;
        const auto hp = target->stats.hp;
        MYE_EXPECT(a.Attack2D(b.Id()));
        MYE_EXPECT(!a.Attack2D(b.Id())); // The producer owns one unconfirmed request.
        CombatPoll(server, a, b, 2);
        MYE_EXPECT(!a.AttackPending2D());
        MYE_EXPECT(a.LastAttack2D().accepted == !blocked);
        if (blocked) { MYE_EXPECT(target->stats.hp == hp); MYE_EXPECT(server.Stop()); continue; }
        MYE_EXPECT(a.LastAttack2D().damage > 0 && target->stats.hp == hp - a.LastAttack2D().damage);
        MYE_EXPECT(a.Attack2D(b.Id())); CombatPoll(server, a, b, 2);
        MYE_EXPECT(!a.LastAttack2D().accepted && a.LastAttack2D().reason == "Attack is cooling down");
        const auto hpAfter = target->stats.hp;
        MYE_EXPECT(a.Attack2D(99999)); CombatPoll(server, a, b, 2);
        MYE_EXPECT(!a.LastAttack2D().accepted && target->stats.hp == hpAfter);
        for (int i = 0; i < 30; ++i) { MYE_EXPECT(b.SendInput2D({1, 0})); CombatPoll(server, a, b); }
        MYE_EXPECT(a.Attack2D(b.Id())); CombatPoll(server, a, b, 2);
        MYE_EXPECT(!a.LastAttack2D().accepted && a.LastAttack2D().reason == "Target is outside attack range");
        MYE_EXPECT(target->stats.hp == hpAfter);
        for (int i = 0; i < 30; ++i) { MYE_EXPECT(b.SendInput2D({-1, 0})); CombatPoll(server, a, b); }
        CombatPoll(server, a, b, 60);
        target->stats.hp = 1;
        MYE_EXPECT(a.Attack2D(b.Id())); CombatPoll(server, a, b, 2);
        MYE_EXPECT(a.LastAttack2D().accepted && target->stats.hp == 0);
        phys::MotionState2D before, after;
        MYE_EXPECT(server.Net().GetEntity2D(b.Id(), before));
        MYE_EXPECT(b.SendInput2D({1, 0})); CombatPoll(server, a, b, 2);
        MYE_EXPECT(server.Net().GetEntity2D(b.Id(), after));
        MYE_EXPECT_NEAR(before.position.x, after.position.x, 0);
        MYE_EXPECT(b.Attack2D(a.Id())); CombatPoll(server, a, b, 2);
        MYE_EXPECT(!b.LastAttack2D().accepted);
        b.Disconnect(); CombatPoll(server, a, b, 2);
        MYE_EXPECT(persistence.Characters().Get(cb)->dead && persistence.Characters().Get(cb)->hp == 0);
        persist::CharacterStore restored;
        MYE_EXPECT(restored.LoadJson(persistence.Characters().ToJson()));
        MYE_EXPECT(restored.Get(cb)->dead);
        MYE_EXPECT(b.Configure2D(walls, settings, 92, cb));
        b.Connect(endpoint, "combat-b", "test-password"); CombatPoll(server, a, b, 4);
        const auto* rejoined = server.Game().Get(server.SessionOf(b.Id()));
        MYE_EXPECT(rejoined && rejoined->stats.hp == 0);
        MYE_EXPECT(server.Stop());
    }
}

MYE_TEST(OnlineCombatRejectsForgedPacketsAndReplaysCachedOutcome) {
    net::NetSubsystem network; MYE_EXPECT(network.ok); if (!network.ok) return;
    persist::PersistenceService persistence;
    const auto aa = persistence.Accounts().Register("raw-a", "test-password").Value();
    const auto ab = persistence.Accounts().Register("raw-b", "test-password").Value();
    const auto ca = persistence.Characters().Create(aa, "Raw A").Value();
    const auto cb = persistence.Characters().Create(ab, "Raw B").Value();
    phys::MotionSettings2D settings;
    settings.body.id = 1; settings.body.shape = phys::Shape2D::MakeCircle(.1f);
    gameserver::NetGameServer server(persistence);
    MYE_EXPECT(server.Configure2D({}, settings, 93, "assets/raw.scene", {}));
    MYE_EXPECT(server.ConfigureCombat2D({2, 1, 60}) && server.Start(0));
    const auto endpoint = net::Endpoint::Loopback(server.Port());
    net::UdpSocket raw, foreign;
    net::NetClient b;
    MYE_EXPECT(raw.Open(0) && foreign.Open(0) && b.Open());
    MYE_EXPECT(b.Configure2D({}, settings, 93, cb)); b.Connect(endpoint, "raw-b", "test-password");
    net::BitWriter hello;
    net::WriteHeader(hello, net::MsgType::Connect2D);
    net::WriteString(hello, "raw-a"); net::WriteString(hello, "test-password");
    net::WriteU64(hello, 93); net::WriteU64(hello, ca); net::WriteU64(hello, 43);
    const auto& handshake = hello.Finish(); raw.SendTo(endpoint, handshake.data(), handshake.size());
    uint64_t token = 0; uint32_t id = 0;
    uint8_t buffer[1400]; net::Endpoint from;
    for (int i = 0; i < 10 && !token; ++i) {
        server.Tick(net::kFixedDelta2D); b.Receive();
        for (int n; (n = raw.RecvFrom(from, buffer, sizeof(buffer))) > 0;) {
            net::BitReader reader(buffer, n); net::MsgType type;
            if (net::ReadHeader(reader, type) && type == net::MsgType::Accept2D) {
                id = reader.ReadBits(32); token = net::ReadU64(reader);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    MYE_EXPECT(token && id && b.Connected()); if (!token || !b.Connected()) return;
    auto* target = server.Game().Get(server.SessionOf(b.Id()));
    const auto hp = target->stats.hp;
    const auto send = [&](net::UdpSocket& socket, uint64_t suppliedToken, uint32_t sequence, bool trailing) {
        net::BitWriter writer; net::WriteHeader(writer, net::MsgType::Attack2D);
        net::WriteU64(writer, suppliedToken); writer.WriteBits(sequence, 32); writer.WriteBits(b.Id(), 32);
        if (trailing) writer.WriteBits(1, 8);
        const auto& bytes = writer.Finish(); socket.SendTo(endpoint, bytes.data(), bytes.size());
        server.Tick(net::kFixedDelta2D); b.Receive();
    };
    send(raw, token ^ 1, 1, false); send(raw, token, 1, true); send(raw, token, 2, false);
    send(foreign, token, 1, false);
    MYE_EXPECT(target->stats.hp == hp);
    send(raw, token, 1, false);
    const auto hpAfter = target->stats.hp;
    MYE_EXPECT(hpAfter < hp);
    // Discard the first response, then retry exactly the same request.
    while (raw.RecvFrom(from, buffer, sizeof(buffer)) > 0) {}
    send(raw, token, 1, false);
    MYE_EXPECT(target->stats.hp == hpAfter);
    bool replayed = false;
    for (int n; (n = raw.RecvFrom(from, buffer, sizeof(buffer))) > 0;) {
        net::BitReader reader(buffer, n); net::MsgType type;
        if (!net::ReadHeader(reader, type) || type != net::MsgType::AttackResult2D) continue;
        MYE_EXPECT(net::ReadU64(reader) == token);
        MYE_EXPECT(reader.ReadBits(32) == 1 && reader.ReadBits(32) == b.Id());
        replayed = reader.ReadBits(1) != 0;
    }
    MYE_EXPECT(replayed && server.Stop());
}
