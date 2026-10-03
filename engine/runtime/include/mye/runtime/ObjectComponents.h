#pragma once

#include "mye/asset/AssetGuid.h"
#include "mye/core/Base.h"
#include "mye/core/Math.h"
#include "mye/ecs/ComponentType.h"
#include <string>
#include <vector>

namespace mye::ecs { class World; }
namespace mye::anim { struct SpriteAnimator; }
namespace mye::runtime {

struct CharacterController2D {
    MYE_COMPONENT(CharacterController2D);
    bool enabled = true;
    float speed = 3.0f;
    asset::AssetRef idleAnimation, walkAnimation;
};

struct CharacterController3D {
    MYE_COMPONENT(CharacterController3D);
    bool enabled = true, cameraRelative = true;
    asset::AssetRef idleAnimation, walkAnimation;
    asset::AssetGuid requestedMotion;
};

struct InteractionTarget {
    MYE_COMPONENT(InteractionTarget);
    bool enabled = true;
    float radius = 1.5f;
    std::string prompt = "E: 상호작용";
};

// One screen-space document per local Play world; game rules remain in project Lua.
struct GameUi {
    MYE_COMPONENT(GameUi);
    bool enabled = true;
    asset::AssetRef document;
};

struct ScenePortal {
    MYE_COMPONENT(ScenePortal);
    std::string scenePath; // assets/scenes/*.scene, relative to the project
    std::string spawnName;
    bool onInteract = true;
};

enum class ObjectEvent : int32_t { Start, Interact, TriggerEnter, TriggerExit };
enum class ObjectAction : int32_t { Message, SetVisible, MoveTo, ChangeMap, LuaCallback };

// Each connection is event -> action. Matching connections execute in displayed order.
struct ObjectConnection {
    ObjectEvent event = ObjectEvent::Interact;
    ObjectAction action = ObjectAction::Message;
    std::string target; // empty = this object; otherwise a unique ObjectName
    std::string text;
    float x = 0, y = 0;
    float z = 0;
    bool visible = true;
};

struct ObjectBehavior {
    MYE_COMPONENT(ObjectBehavior);
    std::vector<ObjectConnection> connections;
    std::string luaSource;
};

void RegisterObjectComponents(ecs::World& world);
Expected<void, Error> ValidateObjectComponents(ecs::World& world);
// Facing follows the requested movement even against a wall; walking follows actual displacement.
void UpdateCharacterAnimation2D(anim::SpriteAnimator& animator, const CharacterController2D& controller,
                                Vec2 lastMove, Vec2 facing);
} // namespace mye::runtime

#include "mye/refl/TypeBuilder.h"
MYE_REFLECT_NAME(mye::runtime::CharacterController2D, "CharacterController2D");
MYE_REFLECT_NAME(mye::runtime::CharacterController3D, "CharacterController3D");
MYE_REFLECT_NAME(mye::runtime::InteractionTarget, "InteractionTarget");
MYE_REFLECT_NAME(mye::runtime::GameUi, "GameUi");
MYE_REFLECT_NAME(mye::runtime::ScenePortal, "ScenePortal");
MYE_REFLECT_NAME(mye::runtime::ObjectBehavior, "ObjectBehavior");
MYE_REFLECT(mye::runtime::ObjectConnection);
MYE_REFLECT_ENUM(mye::runtime::ObjectEvent);
MYE_REFLECT_ENUM(mye::runtime::ObjectAction);
namespace mye::refl {
template<> void Reflect(TypeBuilder<mye::runtime::CharacterController2D>&);
template<> void Reflect(TypeBuilder<mye::runtime::CharacterController3D>&);
template<> void Reflect(TypeBuilder<mye::runtime::InteractionTarget>&);
template<> void Reflect(TypeBuilder<mye::runtime::GameUi>&);
template<> void Reflect(TypeBuilder<mye::runtime::ScenePortal>&);
template<> void Reflect(TypeBuilder<mye::runtime::ObjectBehavior>&);
}
