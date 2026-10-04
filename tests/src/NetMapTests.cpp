#include "TestFramework.h"
#include "mye/gameserver/NetGameServer.h"
#include "mye/net/NetClient.h"
#include <chrono>
#include <thread>

using namespace mye;
namespace {
void MapPoll(gameserver::NetGameServer& server, net::NetClient& a, net::NetClient& b, int ticks = 3) {
    for (int i = 0; i < ticks; ++i) {
        server.Tick(net::kFixedDelta2D); a.Receive(); b.Receive();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
struct TwoMaps {
    phys::MotionSettings2D a, b;
    std::vector<gameserver::MapPortal2D> portalsA{{1, {}, 1, 0, "assets/b.scene", "door"}};
    std::vector<gameserver::MapPortal2D> portalsB{{1, {2, 0}, 1, 1, "assets/a.scene", "return"}};
    std::vector<gameserver::MapSpawn2D> spawnsA{{"return", {}, 0}}, spawnsB{{"door", {2, 0}, 1}};
    TwoMaps() {
        a.body.id = b.body.id = 1;
        a.body.shape = b.body.shape = phys::Shape2D::MakeCircle(.1f);
        b.body.floorLevel = 1; b.body.floorMask = phys::FloorBit(1);
    }
    bool Configure(gameserver::NetGameServer& server) {
        return server.Configure2D({}, a, 10, "assets/a.scene", {}, portalsA, spawnsA) &&
            server.RegisterMap2D({}, b, 20, "assets/b.scene", {}, portalsB, spawnsB);
    }
};
}
MYE_TEST(OnlineMapsFilterReplicationPreserveStatsAndRestoreSavedDestination) {
    net::NetSubsystem network; MYE_EXPECT(network.ok); if (!network.ok) return;
    persist::PersistenceService persistence;
    const auto aa = persistence.Accounts().Register("map-a", "test-password").Value();
    const auto ab = persistence.Accounts().Register("map-b", "test-password").Value();
    const auto ca = persistence.Characters().Create(aa, "Map A").Value();
    const auto cb = persistence.Characters().Create(ab, "Map B").Value();
    TwoMaps maps;
    gameserver::NetGameServer server(persistence);
    MYE_EXPECT(maps.Configure(server));
    MYE_EXPECT(server.ConfigureCombat2D({2, 1, 1}) && server.Start(0));
    net::NetClient a, b;
    MYE_EXPECT(a.Open() && b.Open());
    MYE_EXPECT(a.Configure2D({}, maps.a, 10, ca) && b.Configure2D({}, maps.a, 10, cb));
    const auto endpoint = net::Endpoint::Loopback(server.Port());
    a.Connect(endpoint, "map-a", "test-password"); b.Connect(endpoint, "map-b", "test-password");
    MapPoll(server, a, b);
    MYE_EXPECT(a.Connected() && b.Connected() && a.EntityCount() == 2);
    auto* player = server.Game().Get(server.SessionOf(a.Id()));
    MYE_EXPECT(player); if (!player) return;
    player->stats.hp = 50;
    MYE_EXPECT(a.EnterPortal2D(999, 20, {}, maps.b)); MapPoll(server, a, b);
    MYE_EXPECT(!a.LastPortal2D().accepted && a.SceneHash2D() == 10 && player->sceneId == "assets/a.scene");
    MYE_EXPECT(a.EnterPortal2D(1, 999, {}, maps.b)); MapPoll(server, a, b);
    MYE_EXPECT(!a.LastPortal2D().accepted && player->stats.hp == 50);
    MYE_EXPECT(a.SendInput2D({1, 0}));
    MYE_EXPECT(!a.EnterPortal2D(1, 20, {}, maps.b)); // Never carry pending source input into another map.
    MapPoll(server, a, b);
    MYE_EXPECT(a.EnterPortal2D(1, 20, {}, maps.b)); MapPoll(server, a, b);
    MYE_EXPECT(a.LastPortal2D().accepted && a.LastPortal2D().epoch == 1 && a.SceneHash2D() == 20);
    MYE_EXPECT(a.EntityCount() == 1 && b.EntityCount() == 1 && a.LatestHealth2D().size() == 1);
    phys::MotionState2D state;
    MYE_EXPECT(a.GetPredicted2D(state) && state.floorLevel == 1);
    MYE_EXPECT_NEAR(state.position.x, 2, 0);
    MYE_EXPECT(player->sceneId == "assets/b.scene" && player->stats.hp == 50);
    MYE_EXPECT(a.Attack2D(b.Id())); MapPoll(server, a, b);
    MYE_EXPECT(!a.LastAttack2D().accepted);
    a.Disconnect(); MapPoll(server, a, b);
    const auto* saved = persistence.Characters().Get(ca);
    MYE_EXPECT(saved->sceneId == "assets/b.scene" && saved->floorLevel == 1 && saved->hp == 50);
    MYE_EXPECT(a.Configure2D({}, maps.b, 20, ca));
    a.Connect(endpoint, "map-a", "test-password"); MapPoll(server, a, b);
    MYE_EXPECT(a.Connected() && a.SceneHash2D() == 20 && a.EntityCount() == 1);
    MYE_EXPECT(a.GetPredicted2D(state) && state.floorLevel == 1 && state.position.x == 2);
    MYE_EXPECT(a.EnterPortal2D(1, 10, {}, maps.a)); MapPoll(server, a, b);
    MYE_EXPECT(a.LastPortal2D().accepted && a.SceneHash2D() == 10 && a.EntityCount() == 2 && b.EntityCount() == 2);
    MYE_EXPECT(server.Stop());
}

MYE_TEST(OnlineMapsRejectOldEpochAfterRoundtripAndReplayPortalOutcome) {
    net::NetSubsystem network; MYE_EXPECT(network.ok); if (!network.ok) return;
    persist::PersistenceService persistence;
    const auto account = persistence.Accounts().Register("map-raw", "test-password").Value();
    const auto character = persistence.Characters().Create(account, "Map Raw").Value();
    TwoMaps maps;
    gameserver::NetGameServer server(persistence); MYE_EXPECT(maps.Configure(server) && server.Start(0));
    const auto endpoint = net::Endpoint::Loopback(server.Port());
    net::UdpSocket socket; MYE_EXPECT(socket.Open(0));
    net::BitWriter hello; net::WriteHeader(hello, net::MsgType::ConnectMap2D);
    net::WriteString(hello, "map-raw"); net::WriteString(hello, "test-password");
    net::WriteU64(hello, 10); net::WriteU64(hello, character); net::WriteU64(hello, 46);
    const auto& bytes = hello.Finish(); socket.SendTo(endpoint, bytes.data(), bytes.size());
    uint8_t buffer[1400]; net::Endpoint from; uint64_t token = 0; uint32_t id = 0;
    for (int i = 0; i < 10 && !token; ++i) {
        server.Tick(net::kFixedDelta2D);
        for (int n; (n = socket.RecvFrom(from, buffer, sizeof(buffer))) > 0;) {
            net::BitReader reader(buffer, n); net::MsgType type;
            if (net::ReadHeader(reader, type) && type == net::MsgType::AcceptMap2D) {
                id = reader.ReadBits(32); token = net::ReadU64(reader);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    MYE_EXPECT(token && id); if (!token) return;
    const auto transmit = [&](net::BitWriter& writer) {
        const auto& payload = writer.Finish(); socket.SendTo(endpoint, payload.data(), payload.size());
        server.Tick(net::kFixedDelta2D);
    };
    const auto portal = [&](uint32_t sequence, uint64_t source, uint32_t epoch, uint64_t destination) {
        net::BitWriter writer; net::WriteHeader(writer, net::MsgType::Portal2D); net::WriteU64(writer, token);
        net::WriteU64(writer, source); writer.WriteBits(epoch, 32); writer.WriteBits(sequence, 32);
        writer.WriteBits(1, 32); net::WriteU64(writer, destination); transmit(writer);
    };
    const auto movement = [&](uint64_t hash, uint32_t epoch) {
        net::BitWriter writer; net::WriteHeader(writer, net::MsgType::InputMap2D); net::WriteU64(writer, token);
        net::WriteU64(writer, hash); writer.WriteBits(epoch, 32); writer.WriteBits(1, 8);
        net::WriteMovementInput(writer, {1, {1, 0}, false}); transmit(writer);
    };
    phys::MotionState2D state;
    portal(1, 10, 0, 20);
    MYE_EXPECT(server.Net().MapHash2D(id) == 20);
    movement(10, 0);
    MYE_EXPECT(server.Net().GetEntity2D(id, state) && state.position.x == 2);
    movement(20, 1);
    MYE_EXPECT(server.Net().GetEntity2D(id, state) && state.position.x > 2);
    const auto moved = state.position.x;
    portal(1, 10, 0, 20); // Lost result retry: cached outcome, no second teleport.
    MYE_EXPECT(server.Net().GetEntity2D(id, state) && state.position.x == moved);
    portal(2, 20, 1, 10);
    MYE_EXPECT(server.Net().MapHash2D(id) == 10);
    movement(10, 0); // Same map hash after A-B-A is insufficient; the old epoch is refused.
    MYE_EXPECT(server.Net().GetEntity2D(id, state) && state.position.x == 0);
    movement(10, 2);
    MYE_EXPECT(server.Net().GetEntity2D(id, state) && state.position.x > 0);
    MYE_EXPECT(server.Stop());
}
