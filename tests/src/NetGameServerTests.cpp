// NetGameServerTests.cpp — 넷↔게임플레이↔영속 통합 루프백 (M9·M8·M10 통합)
//
// 인증 접속 → 캐릭터 세션 로드 → 서버권위 이동 → 퇴장 시 위치 저장. 실제 UDP(127.0.0.1). 소켓 불가 시 스킵.
#include "TestFramework.h"

#include "mye/gameserver/NetGameServer.h"
#include "mye/net/NetClient.h"
#include "mye/net/UdpSocket.h"
#include "mye/core/JsonFile.h"

#include <chrono>
#include <cmath>
#include <limits>
#include <filesystem>
#include <thread>

using namespace mye;
using namespace mye::gameserver;

namespace { void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); } }

MYE_TEST(NetGameServer2DSharedCollisionAckOwnershipAndSavedFloor) {
    net::NetSubsystem sys;
    MYE_EXPECT(sys.ok);
    if (!sys.ok) return;
    persist::PersistenceService persistence;
    const auto a = persistence.Accounts().Register("xy-a", "test-only-password").Value();
    const auto b = persistence.Accounts().Register("xy-b", "test-only-password").Value();
    const auto ca = persistence.Characters().Create(a, "XY A").Value();
    const auto cb = persistence.Characters().Create(b, "XY B").Value();
    phys::MotionSettings2D settings;
    settings.body.id = 1;
    settings.body.shape = phys::Shape2D::MakeCircle(.2f);
    settings.body.floorLevel = 1; settings.body.floorMask = phys::FloorBit(1);
    settings.offset = {.05f, .1f}; settings.speed = 100;
    std::vector<phys::CollisionBody2D> walls(3);
    walls[0].id = 2; walls[0].pos = {1.6f, 0}; walls[0].shape = phys::Shape2D::MakeBox(.005f, 1000);
    walls[0].floorLevel = 1; walls[0].floorMask = phys::FloorBit(1);
    walls[1].id = 3; walls[1].floorLevel = 2; walls[1].floorMask = phys::FloorBit(2);
    walls[2].id = 4; walls[2].isTrigger = true;
    walls[2].floorLevel = 1; walls[2].floorMask = phys::FloorBit(1);
    NetGameServer server(persistence);
    MYE_EXPECT(server.Configure2D(walls, settings, 123, "assets/online.scene", {}));
    MYE_EXPECT(server.Start(0));
    if (!server.IsRunning()) return;
    const auto endpoint = net::Endpoint::Loopback(server.Port());
    net::NetClient first, second, badOwner, badHash, duplicate;
    const auto connect = [&](net::NetClient& client, uint64_t hash, uint64_t character, std::string_view user) {
        MYE_EXPECT(client.Open());
        MYE_EXPECT(client.Configure2D(walls, settings, hash, character));
        client.Connect(endpoint, user, "test-only-password");
    };
    connect(first, 123, ca, "xy-a"); connect(second, 123, cb, "xy-b");
    for (int i = 0; i < 4; ++i) { server.Tick(net::kFixedDelta2D); first.Receive(); second.Receive(); SleepMs(1); }
    MYE_EXPECT(first.Connected() && second.Connected());
    MYE_EXPECT(first.LatestSnapshot2D().size() == 2 && second.LatestSnapshot2D().size() == 2);
    MYE_EXPECT(first.EntityCount() == 2);
    MYE_EXPECT(!first.SendInput3D({}, false));
    connect(badOwner, 123, cb, "xy-a"); connect(badHash, 999, ca, "xy-a"); connect(duplicate, 123, ca, "xy-a");
    for (int i = 0; i < 3; ++i) {
        server.Tick(net::kFixedDelta2D); badOwner.Receive(); badHash.Receive(); duplicate.Receive(); SleepMs(1);
    }
    MYE_EXPECT(!badOwner.Connected() && !badHash.Connected() && !duplicate.Connected() && server.PlayerCount() == 2);
    for (int i = 0; i < 3; ++i) MYE_EXPECT(first.SendInput2D({1, 0}));
    const auto beforeTick = server.Net().CurrentTick();
    server.Tick(1.0f / 30);
    MYE_EXPECT(server.Net().CurrentTick() == beforeTick);
    phys::MotionState2D authority, predicted;
    MYE_EXPECT(server.Net().GetEntity2D(first.Id(), authority));
    MYE_EXPECT_NEAR(authority.position.x, 0, 0);
    for (int i = 0; i < 3; ++i) {
        server.Tick(net::kFixedDelta2D); first.Receive(); second.Receive(); SleepMs(1);
        const auto& snapshot = first.LatestSnapshot2D();
        const auto mine = std::find_if(snapshot.begin(), snapshot.end(), [&](const auto& e) { return e.netId == first.Id(); });
        MYE_EXPECT(mine != snapshot.end() && mine->ack == static_cast<uint32_t>(i + 1));
    }
    for (int i = 0; i < 30; ++i) {
        MYE_EXPECT(first.SendInput2D({1, 0})); MYE_EXPECT(second.SendInput2D({0, 1}));
        server.Tick(net::kFixedDelta2D); first.Receive(); second.Receive(); SleepMs(1);
    }
    MYE_EXPECT(first.GetPredicted2D(predicted) && server.Net().GetEntity2D(first.Id(), authority));
    MYE_EXPECT_NEAR(authority.position.x, 1.345f, .00001f);
    MYE_EXPECT_NEAR(predicted.position.x, authority.position.x, .00001f);
    MYE_EXPECT(authority.onWall && authority.floorLevel == 1);
    MYE_EXPECT_NEAR(authority.facingRadians, kPi / 2, .00001f);
    MYE_EXPECT(second.GetPredicted2D(predicted) && server.Net().GetEntity2D(second.Id(), authority));
    MYE_EXPECT_NEAR(authority.position.y, 50, .0001f);
    MYE_EXPECT_NEAR(predicted.position.y, authority.position.y, .0001f);
    first.Disconnect(); second.Disconnect();
    for (int i = 0; i < 3; ++i) { server.Tick(net::kFixedDelta2D); SleepMs(1); }
    MYE_EXPECT(server.PlayerCount() == 0);
    const auto* saved = persistence.Characters().Get(ca);
    MYE_EXPECT(!saved->world3D && saved->floorLevel == 1 && saved->sceneId == "assets/online.scene");
    MYE_EXPECT_NEAR(saved->posX, 1.345f, .00001f);
    const auto root = Utf8Path(MYE_TEST_DATA_DIR) / "online-2d" /
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    MYE_EXPECT(persistence.SaveAll(Utf8String(root)));
    const auto kept = json::Stringify(persistence.Characters().ToJson());
    NetGameServer legacy(persistence);
    legacy.Net().SetAuthenticator([a](std::string_view, std::string_view) { return a; });
    net::NetClient oldClient;
    MYE_EXPECT(legacy.Start(0) && oldClient.Open());
    oldClient.Connect(net::Endpoint::Loopback(legacy.Port()), "xy-a", "test-only-password");
    for (int i = 0; i < 3; ++i) { legacy.Tick(net::kFixedDelta2D); oldClient.Receive(); SleepMs(1); }
    MYE_EXPECT(!oldClient.Connected() && legacy.PlayerCount() == 0);
    MYE_EXPECT(legacy.Stop());
    MYE_EXPECT(json::Stringify(persistence.Characters().ToJson()) == kept);
    persist::PersistenceService reloaded;
    MYE_EXPECT(reloaded.LoadAll(Utf8String(root)));
    MYE_EXPECT(reloaded.Characters().Get(ca)->floorLevel == 1);
    NetGameServer restarted(reloaded);
    MYE_EXPECT(server.Stop());
    MYE_EXPECT(restarted.Configure2D(walls, settings, 123, "assets/online.scene", {}));
    MYE_EXPECT(restarted.Start(0));
    const auto retry = [&](net::NetClient& client) {
        MYE_EXPECT(client.Open()); MYE_EXPECT(client.Configure2D(walls, settings, 123, ca));
        client.Connect(net::Endpoint::Loopback(restarted.Port()), "xy-a", "test-only-password");
        for (int i = 0; i < 3; ++i) { restarted.Tick(net::kFixedDelta2D); client.Receive(); SleepMs(1); }
    };
    retry(first);
    MYE_EXPECT(first.GetPredicted2D(predicted));
    MYE_EXPECT_NEAR(predicted.position.x, saved->posX, .00001f);
    first.Disconnect(); restarted.Tick(net::kFixedDelta2D);
    const auto goodRecord = *reloaded.Characters().Get(ca);
    for (int invalid = 0; invalid < 5; ++invalid) {
        auto* record = reloaded.Characters().GetMutable(ca);
        *record = goodRecord;
        if (invalid == 0) record->floorLevel = 2;
        if (invalid == 1) record->sceneId = "assets/other.scene";
        if (invalid == 2) record->world3D = true;
        if (invalid == 3) record->posX = 1.55f; // Stored collider center is inside the thin wall.
        if (invalid == 4) record->posZ = 1; // A 2D binding must not silently discard a stored third axis.
        const auto before = json::Stringify(reloaded.Characters().ToJson());
        net::NetClient refused; retry(refused);
        MYE_EXPECT(!refused.Connected() && restarted.PlayerCount() == 0);
        MYE_EXPECT(json::Stringify(reloaded.Characters().ToJson()) == before);
    }
    MYE_EXPECT(restarted.Stop());
}

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
    net::WriteMovementInput(gap, {1, {-1, 0}, false});
    net::WriteMovementInput(gap, {3, {-1, 0}, false});
    bytes = gap.Finish();
    MYE_EXPECT(raw.SendTo(endpoint, bytes.data(), bytes.size()) == int(bytes.size()));
    server.Tick(net::kFixedDelta3D);
    MYE_EXPECT(server.Net().GetEntity3D(rawId, authority));
    MYE_EXPECT_NEAR(authority.position.x, rawPosition.x, 0);
    MYE_EXPECT(server.Stop());
    net::BitWriter w;
    net::WriteHeader(w, net::MsgType::Input3D);
    net::WriteU64(w, 42);
    net::WriteMovementInput(w, {1, {std::numeric_limits<float>::quiet_NaN(), 0}, false});
    net::BitReader reader(w.Finish());
    net::MsgType type;
    MYE_EXPECT(net::ReadHeader(reader, type));
    MYE_EXPECT(net::ReadU64(reader) == 42);
    net::MovementInput invalid;
    MYE_EXPECT(!net::ReadMovementInput(reader, invalid));
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

MYE_TEST(NetClient2DRefusesMalformedSnapshotsAndReplaysOnlyUnconfirmedInputs) {
    net::NetSubsystem sys;
    MYE_EXPECT(sys.ok);
    if (!sys.ok) return;
    net::UdpSocket authority;
    net::NetClient client;
    phys::MotionSettings2D settings;
    MYE_EXPECT(authority.Open() && client.Open());
    MYE_EXPECT(client.Configure2D({}, settings, 123, 7));
    client.Connect(net::Endpoint::Loopback(authority.LocalPort()), "test", "test");
    uint8_t buffer[1400]; net::Endpoint peer;
    int received = 0;
    for (int i = 0; i < 100 && received <= 0; ++i) { received = authority.RecvFrom(peer, buffer, sizeof(buffer)); SleepMs(1); }
    MYE_EXPECT(received > 0);
    if (received <= 0) return;
    net::BitReader join(buffer, received); net::MsgType type;
    std::string user, pass;
    MYE_EXPECT(net::ReadHeader(join, type) && type == net::MsgType::Connect2D);
    MYE_EXPECT(net::ReadConnect(join, user, pass));
    MYE_EXPECT(net::ReadU64(join) == 123 && net::ReadU64(join) == 7);
    const auto nonce = net::ReadU64(join);
    const auto deliver = [&](net::BitWriter& writer) {
        const auto& bytes = writer.Finish();
        MYE_EXPECT(authority.SendTo(peer, bytes.data(), bytes.size()) == int(bytes.size()));
        SleepMs(1); client.Receive();
    };
    net::BitWriter accept;
    net::WriteHeader(accept, net::MsgType::Accept2D); accept.WriteBits(1, 32);
    net::WriteU64(accept, 42); net::WriteU64(accept, nonce);
    deliver(accept);
    MYE_EXPECT(client.Connected());
    phys::MotionState2D state;
    const auto snapshot = [&](uint32_t tick, uint32_t ack, uint64_t token, int count, bool trailing = false) {
        net::BitWriter writer;
        net::WriteHeader(writer, net::MsgType::Snapshot2D); net::WriteU64(writer, token);
        writer.WriteBits(tick, 32); writer.WriteBits(count, 8);
        for (int i = 0; i < count; ++i) {
            writer.WriteBits(static_cast<uint32_t>(i + 1), 32); writer.WriteBits(ack, 32);
            net::WriteState2D(writer, state);
        }
        if (trailing) writer.WriteBits(255, 8);
        if (count == int(net::kMaxSnapshotEntities2D)) MYE_EXPECT(writer.Finish().size() <= 1400);
        deliver(writer);
    };
    snapshot(1, 0, 42, 1);
    phys::MotionState2D predicted;
    MYE_EXPECT(client.GetPredicted2D(predicted));
    for (int i = 0; i < 3; ++i) MYE_EXPECT(client.SendInput2D({1, 0}));
    state.position.x = .05f;
    snapshot(2, 1, 42, 1);
    MYE_EXPECT(client.GetPredicted2D(predicted));
    MYE_EXPECT_NEAR(predicted.position.x, .15f, .00001f); // Two outstanding fixed ticks are replayed.
    MYE_EXPECT(client.PendingInputs() == 2);
    for (int invalid = 0; invalid < 7; ++invalid) {
        state.position.x = 99;
        state.floorLevel = invalid == 3 ? 1 : 0;
        if (invalid == 4) state.position.x = std::numeric_limits<float>::quiet_NaN();
        snapshot(invalid == 0 ? 2 : 3, invalid == 6 ? UINT32_MAX : invalid == 1 ? 4 : 1,
                 invalid == 2 ? 43 : 42, 1, invalid == 5);
        MYE_EXPECT(client.LastTick() == 2);
        MYE_EXPECT(client.GetPredicted2D(predicted));
        MYE_EXPECT_NEAR(predicted.position.x, .15f, .00001f);
    }
    state = {};
    snapshot(3, 0, 42, 1); // A newer tick cannot retract an acknowledged input.
    MYE_EXPECT(client.LastTick() == 2);
    MYE_EXPECT(client.GetPredicted2D(predicted));
    MYE_EXPECT_NEAR(predicted.position.x, .15f, .00001f);
    state.position.x = .15f;
    snapshot(4, 3, 42, int(net::kMaxSnapshotEntities2D));
    MYE_EXPECT(client.LatestSnapshot2D().size() == net::kMaxSnapshotEntities2D);
    MYE_EXPECT(client.EntityCount() == net::kMaxSnapshotEntities2D && client.PendingInputs() == 0);
    MYE_EXPECT(client.GetPredicted2D(predicted));
    MYE_EXPECT_NEAR(predicted.position.x, .15f, .00001f);
    for (int i = 0; i < 240; ++i) MYE_EXPECT(client.SendInput2D({}));
    MYE_EXPECT(!client.SendInput2D({}) && !client.Connected());
    MYE_EXPECT(client.PendingInputs() == 240);
}

MYE_TEST(NetServer2DRejectsWrongTokenGapsAndModeBeforeSimulation) {
    net::NetSubsystem sys;
    MYE_EXPECT(sys.ok);
    if (!sys.ok) return;
    net::NetServer server;
    phys::MotionSettings2D settings;
    MYE_EXPECT(server.Configure2D({}, settings, 123, [](uint32_t, uint64_t, uint64_t) -> Expected<phys::MotionState2D, Error> {
        return phys::MotionState2D{};
    }));
    server.SetAuthenticator([](std::string_view user, std::string_view pass) -> uint64_t {
        return user == "test" && pass == "test" ? 1 : 0;
    });
    net::UdpSocket peer;
    MYE_EXPECT(server.Start(0) && peer.Open());
    const auto endpoint = net::Endpoint::Loopback(server.Port());
    const auto send = [&](net::BitWriter& writer) {
        const auto& bytes = writer.Finish();
        MYE_EXPECT(peer.SendTo(endpoint, bytes.data(), bytes.size()) == int(bytes.size()));
        SleepMs(1); server.Receive();
    };
    net::BitWriter connect;
    net::WriteHeader(connect, net::MsgType::Connect2D);
    net::WriteString(connect, "test"); net::WriteString(connect, "test");
    net::WriteU64(connect, 123); net::WriteU64(connect, 7); net::WriteU64(connect, 42);
    send(connect);
    uint8_t buffer[1400]; net::Endpoint from; uint64_t token = 0; uint32_t id = 0;
    for (int i = 0; i < 100 && !token; ++i) {
        const int n = peer.RecvFrom(from, buffer, sizeof(buffer));
        if (n > 0) {
            net::BitReader reader(buffer, n); net::MsgType type;
            if (net::ReadHeader(reader, type) && type == net::MsgType::Accept2D) {
                id = reader.ReadBits(32); token = net::ReadU64(reader);
            }
        }
        SleepMs(1);
    }
    MYE_EXPECT(token && id);
    for (int invalid = 0; invalid < 6; ++invalid) {
        net::BitWriter input;
        net::WriteHeader(input, invalid == 4 ? net::MsgType::Input3D : net::MsgType::Input2D);
        net::WriteU64(input, invalid == 0 ? token ^ 1 : token);
        input.WriteBits(invalid == 1 ? 2 : 1, 8);
        net::WriteMovementInput(input, {1, {invalid == 5 ? std::numeric_limits<float>::quiet_NaN() : 1.0f, 0}, invalid == 2});
        if (invalid == 1) net::WriteMovementInput(input, {3, {1, 0}, false});
        if (invalid == 3) input.WriteBits(255, 8);
        send(input);
    }
    server.Tick(net::kFixedDelta2D);
    phys::MotionState2D state;
    MYE_EXPECT(server.GetEntity2D(id, state)); MYE_EXPECT_NEAR(state.position.x, 0, 0);
    net::BitWriter valid;
    net::WriteHeader(valid, net::MsgType::Input2D); net::WriteU64(valid, token); valid.WriteBits(1, 8);
    net::WriteMovementInput(valid, {1, {1, 0}, false});
    send(valid); send(valid); // Redundancy is consumed once.
    server.Tick(net::kFixedDelta2D);
    MYE_EXPECT(server.GetEntity2D(id, state)); MYE_EXPECT_NEAR(state.position.x, .05f, .00001f);
    server.Tick(net::kFixedDelta2D);
    MYE_EXPECT(server.GetEntity2D(id, state)); MYE_EXPECT_NEAR(state.position.x, .05f, .00001f);
    for (int i = 0; i < 301; ++i) server.Tick(net::kFixedDelta2D);
    MYE_EXPECT(server.ClientCount() == 0 && server.KickedCount() == 1);
    server.Stop();
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
