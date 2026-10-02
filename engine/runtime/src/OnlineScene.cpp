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
    result.hash = hash;
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
} // namespace mye::runtime
