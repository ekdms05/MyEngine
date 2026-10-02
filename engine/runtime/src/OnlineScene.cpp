#include "mye/runtime/OnlineScene.h"
#include "mye/core/JsonFile.h"
#include "mye/ecs/World.h"
#include "mye/gameplay/Progression.h"
#include "mye/phys/PhysicsComponents3D.h"
#include "mye/runtime/ObjectComponents.h"
#include "mye/scene/SceneReflection.h"
#include "mye/scene/SceneSerializer.h"
#include "mye/scene/Transform.h"
#include <filesystem>
namespace mye::runtime {
Expected<OnlineScene3D, Error> LoadOnlineScene3D(std::string_view project, std::string_view scene) {
    auto manifest = ReadJsonFile(Utf8Path(project));
    if (!manifest) return manifest.GetError();
    const auto* version = manifest.Value().Find("version");
    const auto* mainScene = manifest.Value().Find("mainScene");
    if (!version || !version->IsInteger() || version->AsInt() != 1 || !mainScene || !mainScene->IsString())
        return Error{"Online project requires a version 1 manifest and mainScene", 1};
    OnlineScene3D result;
    result.sceneId = scene.empty() ? std::string(mainScene->AsString()) : std::string(scene);
    std::error_code ec;
    const auto root = std::filesystem::weakly_canonical(Utf8Path(project).parent_path(), ec);
    if (ec) return Error{"Online project directory unavailable", 1};
    const auto path = std::filesystem::weakly_canonical(root / Utf8Path(result.sceneId), ec);
    const auto relative = path.lexically_relative(root / "assets");
    if (ec || relative.empty() || relative.is_absolute() || *relative.begin() == ".." ||
        path.extension() != ".scene")
        return Error{"Online scene must be inside project assets", 1};
    auto data = ReadJsonFile(path);
    if (!data) return data.GetError();
    ecs::World world;
    scene::RegisterCoreComponents(world);
    RegisterObjectComponents(world);
    gameplay::RegisterProgressionReflection();
    world.RegisterComponent<gameplay::Progression>("Progression");
    auto loaded = scene::SceneSerializer{}.ReadInto(world, data.Value());
    if (!loaded) return loaded.GetError();
    if (auto valid = ValidateObjectComponents(world); !valid) return valid.GetError();
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
    // A compatibility fingerprint, not an authentication proof. Authority owns all collision data.
    result.hash = HashFnv1a64(json::Stringify(data.Value()));
    return result;
}
} // namespace mye::runtime
