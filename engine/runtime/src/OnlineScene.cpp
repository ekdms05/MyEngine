#include "mye/runtime/OnlineScene.h"
#include "mye/core/JsonFile.h"
#include "mye/ecs/World.h"
#include "mye/gameplay/Progression.h"
#include "mye/phys/PhysicsComponents3D.h"
#include "mye/phys/PhysicsWorld2D.h"
#include "mye/runtime/ObjectComponents.h"
#include "mye/scene/SceneReflection.h"
#include "mye/scene/SceneSerializer.h"
#include "mye/scene/Transform.h"
#include "mye/scene/Renderable.h"
#include <set>
#include <filesystem>
#include <algorithm>
#include <system_error>
namespace mye::runtime {
namespace {
Expected<uint64_t, Error> LoadOnlineWorld(std::string_view project, std::string_view requested,
                                        ecs::World& world, std::string& sceneId) {
    try {
        if (project.find('\0') != std::string_view::npos || Utf8Path(project).extension() != ".myeproj")
            return Error{"Online project requires a .myeproj manifest", 1};
        auto manifest = ReadJsonFile(Utf8Path(project));
        if (!manifest) return manifest.GetError();
        const auto* version = manifest.Value().Find("version");
        const auto* mainScene = manifest.Value().Find("mainScene");
        const auto* name = manifest.Value().Find("name");
        if (!manifest.Value().IsObject() || !version || !version->IsInteger() || version->AsInt() != 1 ||
            !mainScene || !mainScene->IsString() || !name || !name->IsString())
            return Error{"Online project requires a version 1 manifest, name and mainScene", 1};
        sceneId = requested.empty() ? std::string(mainScene->AsString()) : std::string(requested);
        if (sceneId.find('\0') != std::string::npos || Utf8Path(sceneId).has_root_path())
            return Error{"Online scene must be project relative", 1};
        std::error_code ec;
        const auto parent = Utf8Path(project).parent_path();
        const auto root = std::filesystem::canonical(parent.empty() ? std::filesystem::path(".") : parent, ec);
        if (ec) return Error{"Online project directory unavailable", 1};
        const auto path = std::filesystem::weakly_canonical(root / Utf8Path(sceneId), ec);
        const auto relative = path.lexically_relative(root / "assets");
        if (ec || relative.empty() || relative.is_absolute() || *relative.begin() == ".." ||
            path.extension() != ".scene")
            return Error{"Online scene must be inside project assets", 1};
        auto data = ReadJsonFile(path);
        if (!data) return data.GetError();
        scene::RegisterCoreComponents(world);
        RegisterObjectComponents(world);
        gameplay::RegisterProgressionReflection();
        world.RegisterComponent<gameplay::Progression>("Progression");
        auto loaded = scene::SceneSerializer{}.ReadInto(world, data.Value());
        if (!loaded) return loaded.GetError();
        if (auto valid = ValidateObjectComponents(world); !valid) return valid.GetError();
        sceneId = Utf8String(path.lexically_relative(root));
        // A compatibility fingerprint, not an authentication proof. The server loads its own data.
        return HashFnv1a64(json::Stringify(data.Value()));
    } catch (const std::system_error& error) {
        return Error{"Online project path error: " + std::string(error.what()), error.code().value()};
    }
}
Expected<OnlineScene2D, Error> BuildOnlineScene2D(ecs::World& world, uint64_t hash, std::string sceneId) {
    OnlineScene2D result;
    result.sceneId = std::move(sceneId);
    result.hash = HashFnv1a64(result.sceneId + "\n" + std::to_string(hash));
    ecs::Entity character;
    world.Query<CharacterController2D>().Each([&](ecs::Entity e, const auto& c) {
        if (c.enabled) character = e;
    });
    if (character.IsNull())
        return Error{"Online scene requires exactly one enabled CharacterController2D prototype", 1};
    bool physics3D = false;
    world.Query<phys::Collider3D>().Each([&](ecs::Entity, const auto&) { physics3D = true; });
    if (physics3D) return Error{"Online 2D scene does not support 3D physics components", 1};
    auto gathered = phys::GatherCollisionBodies2D(world);
    if (!gathered) return gathered.GetError();
    for (const auto& body : gathered.Value()) {
        if (body.id == character.Packed()) result.character = body;
        else if (body.kinematic) return Error{"Online 2D scene supports one character and static colliders", 1};
        else result.colliders.push_back(body);
    }
    const auto* collider = world.TryGet<phys::Collider2D>(character);
    const auto* controller = world.TryGet<CharacterController2D>(character);
    const auto& position = world.TryGet<scene::LocalTransform>(character)->position;
    result.offset = collider->offset;
    result.spawn = {position.x, position.y};
    result.speed = controller->speed;
    result.maxSlideIters = world.TryGet<phys::KinematicBody2D>(character)->maxSlideIters;
    std::string portalError;
    world.Query<ScenePortal, scene::WorldTransform>().Each([&](ecs::Entity entity, const auto& portal, const auto& pose) {
        if (!portal.onInteract) { portalError = "Online 2D portals currently require interact activation"; return; }
        const auto* interaction = world.TryGet<InteractionTarget>(entity);
        if (!interaction || !interaction->enabled) return;
        const auto* floor = world.TryGet<scene::FloorLevel>(entity);
        const auto id = static_cast<uint32_t>(entity.Packed()) + 1;
        if (!id) { portalError = "Portal identity overflow"; return; }
        result.portals.push_back({id, {pose.matrix.m[3][0], pose.matrix.m[3][1]}, interaction->radius,
            floor ? floor->level : result.character.floorLevel, portal.scenePath, portal.spawnName});
    });
    if (!portalError.empty()) return Error{portalError, 1};
    world.Query<scene::ObjectName, scene::WorldTransform>().Each([&](ecs::Entity entity, const auto& name, const auto& pose) {
        if (name.value.empty()) return;
        const auto* floor = world.TryGet<scene::FloorLevel>(entity);
        result.spawns.push_back({name.value, {pose.matrix.m[3][0], pose.matrix.m[3][1]},
            floor ? floor->level : result.character.floorLevel});
    });
    std::sort(result.portals.begin(), result.portals.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    if (auto valid = phys::ValidateSpawn2D(result.character, result.colliders); !valid)
        return valid.GetError();
    // Scene-local opaque ids remain stable regardless of component-pool iteration order.
    std::sort(result.colliders.begin(), result.colliders.end(),
              [](const auto& a, const auto& b) { return a.id < b.id; });
    return result;
}

Expected<OnlineScene3D, Error> BuildOnlineScene3D(ecs::World& world, uint64_t hash, std::string sceneId) {
    OnlineScene3D result;
    result.sceneId = std::move(sceneId);
    result.hash = hash;
    if (auto gathered = phys::GatherPhysicsWorld3D(world, result.physics); !gathered)
        return gathered.GetError();
    int count = 0;
    std::string error;
    world.Query<CharacterController3D, scene::LocalTransform>().Each(
        [&](ecs::Entity e, const auto& c, const auto& pose) {
            if (!c.enabled) return;
            ++count;
            auto settings = phys::CharacterSettings3D(world, e);
            if (!settings) error = settings.GetError().message;
            else {
                result.settings = settings.Value();
                result.spawn = pose.position;
            }
        });
    if (count != 1)
        return Error{"Online scene requires exactly one enabled CharacterController3D prototype", 1};
    if (!error.empty()) return Error{error, 1};
    phys::MotionState3D state;
    state.position = result.spawn;
    if (auto moved = result.physics.Step(state, {}, false, 1.0f / 60, result.settings); !moved)
        return moved.GetError();
    result.spawn = state.position;
    return result;
}
} // namespace

Expected<OnlineScene, Error> LoadOnlineScene(std::string_view project, std::string_view scene) {
    ecs::World world;
    std::string sceneId;
    auto loaded = LoadOnlineWorld(project, scene, world, sceneId);
    if (!loaded) return loaded.GetError();
    bool twoDimensional = false;
    world.Query<CharacterController2D>().Each([&](ecs::Entity, const auto& c) {
        if (c.enabled) twoDimensional = true;
    });
    if (twoDimensional) {
        auto result = BuildOnlineScene2D(world, loaded.Value(), std::move(sceneId));
        if (!result) return result.GetError();
        return OnlineScene{std::move(result).Value()};
    }
    auto result = BuildOnlineScene3D(world, loaded.Value(), std::move(sceneId));
    if (!result) return result.GetError();
    return OnlineScene{std::move(result).Value()};
}

Expected<OnlineScene2D, Error> LoadOnlineScene2D(std::string_view project, std::string_view scene) {
    ecs::World world;
    std::string sceneId;
    auto loaded = LoadOnlineWorld(project, scene, world, sceneId);
    if (!loaded) return loaded.GetError();
    return BuildOnlineScene2D(world, loaded.Value(), std::move(sceneId));
}

Expected<OnlineScene3D, Error> LoadOnlineScene3D(std::string_view project, std::string_view scene) {
    ecs::World world;
    std::string sceneId;
    auto loaded = LoadOnlineWorld(project, scene, world, sceneId);
    if (!loaded) return loaded.GetError();
    return BuildOnlineScene3D(world, loaded.Value(), std::move(sceneId));
}
Expected<std::vector<OnlineScene2D>, Error> LoadOnlineMaps2D(std::string_view project, std::string_view scene) {
    auto first = LoadOnlineScene2D(project, scene);
    if (!first) return first.GetError();
    std::vector<OnlineScene2D> maps;
    maps.push_back(std::move(first).Value());
    std::set<std::string> visited{maps.front().sceneId};
    for (size_t i = 0; i < maps.size(); ++i) {
        const auto portals = maps[i].portals; // Loading a destination can reallocate maps.
        for (size_t j = 0; j < portals.size(); ++j) {
            const auto& portal = portals[j];
            const auto existing = std::find_if(maps.begin(), maps.end(), [&](const auto& value) { return value.sceneId == portal.sceneId; });
            if (existing != maps.end()) continue;
            auto loaded = LoadOnlineScene2D(project, portal.sceneId);
            if (!loaded) return loaded.GetError();
            maps[i].portals[j].sceneId = loaded.Value().sceneId;
            if (!visited.insert(loaded.Value().sceneId).second) continue;
            if (maps.size() >= 64) return Error{"Online map catalog supports up to 64 reachable scenes", 1};
            maps.push_back(std::move(loaded).Value());
        }
    }
    for (const auto& map : maps) for (const auto& portal : map.portals) {
        const auto target = std::find_if(maps.begin(), maps.end(), [&](const auto& value) { return value.sceneId == portal.sceneId; });
        if (target == maps.end()) return Error{"Online portal destination is absent from the catalog", 1};
        const auto& destination = *target;
        const auto spawn = std::find_if(destination.spawns.begin(), destination.spawns.end(),
            [&](const auto& value) { return value.name == portal.spawnName; });
        if (spawn == destination.spawns.end() || spawn->floorLevel != destination.character.floorLevel ||
            std::count_if(destination.spawns.begin(), destination.spawns.end(),
                [&](const auto& value) { return value.name == portal.spawnName; }) != 1)
            return Error{"Online portal spawn is missing or belongs to another floor: " + portal.spawnName, 1};
        auto body = destination.character; body.pos = spawn->position + destination.offset;
        if (auto valid = phys::ValidateSpawn2D(body, destination.colliders); !valid) return valid.GetError();
    }
    // Decorative names need not be unique. Publish only names actually used as arrival points.
    for (auto& map : maps) std::erase_if(map.spawns, [&](const auto& spawn) {
        return std::none_of(maps.begin(), maps.end(), [&](const auto& source) {
            return std::any_of(source.portals.begin(), source.portals.end(), [&](const auto& portal) {
                return portal.sceneId == map.sceneId && portal.spawnName == spawn.name;
            });
        });
    });
    return maps;
}
} // namespace mye::runtime
