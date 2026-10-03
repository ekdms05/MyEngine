#include "mye/runtime/ObjectComponents.h"
#include "mye/anim/SpriteAnimator.h"
#include "mye/core/JsonFile.h"
#include "mye/ecs/World.h"
#include "mye/phys/Collision.h"
#include "mye/phys/PhysicsComponents3D.h"
#include "mye/scene/Camera3D.h"
#include "mye/scene/Renderable.h"
#include "mye/scene/Transform.h"
#include "mye/ui/UiDocument.h"
#include <cmath>
#include <filesystem>
#include <unordered_set>

using namespace mye::runtime;
template <> void mye::refl::Reflect(TypeBuilder<CharacterController3D>& b) {
    b.Field("enabled", &CharacterController3D::enabled)
        .Field("cameraRelative", &CharacterController3D::cameraRelative)
        .Attr(Attribute::MakeTooltip("현재 게임 카메라의 XZ 축으로 프로젝트 이동 입력을 변환합니다. 속도/점프는 "
                                     "KinematicBody3D.settings에서 지정합니다."))
        .Field("idleAnimation", &CharacterController3D::idleAnimation)
        .Field("walkAnimation", &CharacterController3D::walkAnimation);
}
template<> void mye::refl::Reflect(TypeBuilder<CharacterController2D>& b) {
    b.Version(1).Field("enabled", &CharacterController2D::enabled).Attr(Attribute::MakeTooltip("이 기능의 활성 여부입니다. 캐릭터 조작을 끄면 이동 속도가 0으로 설정됩니다.")).Field("speed", &CharacterController2D::speed).Attr(Attribute::MakeTooltip("월드 단위/초. 기본 3은 48 PPU에서 초당 144 픽셀입니다. 허용 범위 0~100."))
        .Field("idleAnimation", &CharacterController2D::idleAnimation).Attr(Attribute::MakeTooltip("대기할 때 재생할 .anim 에셋을 드래그하세요.")).Field("walkAnimation", &CharacterController2D::walkAnimation).Attr(Attribute::MakeTooltip("이동할 때 재생할 .anim 에셋을 드래그하세요."));
}
template<> void mye::refl::Reflect(TypeBuilder<InteractionTarget>& b) {
    b.Version(1).Field("enabled", &InteractionTarget::enabled).Attr(Attribute::MakeTooltip("interact 조작(기본 E)의 대상 활성 여부입니다.")).Field("radius", &InteractionTarget::radius).Attr(Attribute::MakeTooltip("캐릭터와 대상의 상호작용 거리. 월드 단위이며 0보다 크고 100 이하입니다.")).Field("prompt", &InteractionTarget::prompt).Attr(Attribute::MakeTooltip("상호작용 안내 문자열. 로컬 Play/MyGame 화면에 표시됩니다. 안내 키는 프로젝트 입력 설정과 함께 작성하세요."));
}
template<> void mye::refl::Reflect(TypeBuilder<ScenePortal>& b) {
    b.Version(1).Field("scenePath", &ScenePortal::scenePath).Attr(Attribute::MakeTooltip("프로젝트 기준 assets/scenes/*.scene 경로입니다. 프로젝트 밖으로 이동할 수 없습니다.")).Field("spawnName", &ScenePortal::spawnName).Attr(Attribute::MakeTooltip("목적지 씬의 ObjectName.value와 정확히 일치하는 고유 이름입니다.")).Field("onInteract", &ScenePortal::onInteract).Attr(Attribute::MakeTooltip("켜면 InteractionTarget + interact 조작, 끄면 트리거 Collider2D 진입으로 이동합니다."));
}
template<> void mye::refl::Reflect(TypeBuilder<GameUi>& b) {
    b.Version(1).Field("enabled", &GameUi::enabled)
        .Field("document", &GameUi::document)
        .Attr(Attribute::MakeTooltip("저장한 .ui 에셋을 지정합니다. 로컬 Play/MyGame에서 mye.ui로 표시값을 갱신합니다. 버튼 클릭·TextInput/키보드 포커스·모달 차단을 지원합니다. 실제 IME 장치 검수·온라인 UI는 별도입니다."));
}
template<> void mye::refl::Reflect(EnumBuilder<ObjectEvent>& b) {
    b.Value("Start", ObjectEvent::Start).Value("Interact", ObjectEvent::Interact)
        .Value("TriggerEnter", ObjectEvent::TriggerEnter).Value("TriggerExit", ObjectEvent::TriggerExit);
}
template<> void mye::refl::Reflect(EnumBuilder<ObjectAction>& b) {
    b.Value("Message", ObjectAction::Message).Value("SetVisible", ObjectAction::SetVisible)
        .Value("MoveTo", ObjectAction::MoveTo).Value("ChangeMap", ObjectAction::ChangeMap).Value("LuaCallback", ObjectAction::LuaCallback);
}
template<> void mye::refl::Reflect(TypeBuilder<ObjectConnection>& b) {
    b.Version(1)
        .Field("event", &ObjectConnection::event)
        .Field("action", &ObjectConnection::action)
        .Field("target", &ObjectConnection::target)
        .Field("text", &ObjectConnection::text)
        .Field("x", &ObjectConnection::x)
        .Field("y", &ObjectConnection::y)
        .Field("z", &ObjectConnection::z)
        .Field("visible", &ObjectConnection::visible);
}
template<> void mye::refl::Reflect(TypeBuilder<ObjectBehavior>& b) {
    b.Version(1).Field("connections", &ObjectBehavior::connections).Attr(Attribute::MakeTooltip("발생 이벤트에서 실행할 행동 목록. 위에서 아래 순서로 처리합니다.")).Field("luaSource", &ObjectBehavior::luaSource).Attr(Attribute::MakeTooltip("return 테이블에 콜백을 작성합니다. 실행을 다시 시작하면 반영되며 씬에 저장됩니다."));
}

namespace mye::runtime {
void UpdateCharacterAnimation2D(anim::SpriteAnimator& animator, const CharacterController2D& controller,
                                Vec2 lastMove, Vec2 facing) {
    const bool moving = lastMove.x * lastMove.x + lastMove.y * lastMove.y > 1e-12f;
    if (animator.stateMachine.guid.IsValid()) {
        if (auto* parameter = animator.FindParam("moving"); parameter && parameter->type == anim::ParamType::Bool)
            parameter->value = moving ? 1.0f : 0.0f;
    } else anim::RequestAnimation(animator, moving ? controller.walkAnimation : controller.idleAnimation);
    animator.facing = anim::Dir8FromVector(facing, animator.facing);
}

void RegisterObjectComponents(ecs::World& world) {
    (void)refl::GetType<CharacterController2D>(); (void)refl::GetType<InteractionTarget>();
    (void)refl::GetType<CharacterController3D>();
    (void)refl::GetType<GameUi>();
    (void)refl::GetType<ScenePortal>(); (void)refl::GetType<ObjectBehavior>();
    world.RegisterComponent<CharacterController2D>("CharacterController2D");
    world.RegisterComponent<CharacterController3D>("CharacterController3D");
    world.RegisterComponent<GameUi>("GameUi");
    world.RegisterComponent<InteractionTarget>("InteractionTarget");
    world.RegisterComponent<ScenePortal>("ScenePortal");
    world.RegisterComponent<ObjectBehavior>("ObjectBehavior");
}
Expected<void, Error> ValidateObjectComponents(ecs::World& world) {
    std::string error;
    int screens = 0;
    world.Query<GameUi>().Each([&](ecs::Entity, const GameUi& ui) {
        if (!ui.enabled) return;
        ++screens;
        if (!ui.document.guid.IsValid() || (ui.document.type != 0 && ui.document.type != ui::UiDocument::kAssetTypeId))
            error = "GameUi needs a valid UiDocument asset reference";
    });
    if (screens > 1) error = "A local Play world supports one enabled GameUi document";
    std::unordered_set<std::string> names;
    world.Query<scene::ObjectName>().Each([&](ecs::Entity, const scene::ObjectName& n) {
        if (n.value.size() > 256 || (!n.value.empty() && !names.insert(n.value).second)) error = "Object names must be unique and at most 256 bytes";
    });
    const auto pathValid = [](const std::string& text) {
        const auto path = Utf8Path(text);
        if (text.find('\0') != std::string::npos || text.find('\\') != std::string::npos || path.is_absolute() || path.extension() != ".scene" || !text.starts_with("assets/")) return false;
        for (const auto& part : path) if (part == "..") return false;
        return true;
    };
    world.Query<scene::LocalTransform>().Each([&](ecs::Entity, const scene::LocalTransform& t) {
        if (!std::isfinite(t.position.x) || !std::isfinite(t.position.y) || !std::isfinite(t.position.z)) error = "Object position must be finite";
    });
    world.Query<phys::Collider2D>().Each([&](ecs::Entity, const phys::Collider2D& c) {
        if ((c.shape.kind != phys::ShapeKind::AABB && c.shape.kind != phys::ShapeKind::Circle) ||
            !std::isfinite(c.shape.half.x) || !std::isfinite(c.shape.half.y) || c.shape.half.x <= 0 || c.shape.half.y <= 0 ||
            !std::isfinite(c.offset.x) || !std::isfinite(c.offset.y)) error = "Collider dimensions must be finite and positive";
    });
    world.Query<phys::KinematicBody2D>().Each([&](ecs::Entity, const phys::KinematicBody2D& b) {
        if (!std::isfinite(b.skin) || b.skin < 0 || b.skin > 1 || b.maxSlideIters < 1 || b.maxSlideIters > 16) error = "Invalid kinematic body settings";
    });
    int localControllers = 0;
    world.Query<CharacterController2D>().Each([&](ecs::Entity e, const CharacterController2D& c) {
        if (c.enabled) ++localControllers;
        if (const auto* parent = world.TryGet<scene::Parent>(e); parent && !parent->parent.IsNull()) error = "Character controller must be a scene root for world XY physics";
        if (!std::isfinite(c.speed) || c.speed < 0 || c.speed > 100 || !world.Has<phys::KinematicBody2D>(e) || !world.Has<phys::Collider2D>(e) || !world.Has<scene::LocalTransform>(e)) error = "Controller needs transform, body, collider and a speed in [0,100]";
    });
    if (localControllers > 1) error = "A Play scene supports one enabled local character controller";
    world.Query<CharacterController3D>().Each([&](ecs::Entity e, const auto& controller) {
        if (controller.enabled) ++localControllers;
        if (auto settings = phys::CharacterSettings3D(world, e); !settings)
            error = settings.GetError().message;
        if (world.Has<CharacterController2D>(e)) error = "An object cannot have both 2D and 3D controllers";
    });
    world.Query<phys::Collider3D>().Each([&](ecs::Entity e, const auto& c) {
        const auto finite = [](Vec3 v) {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        };
        if (!finite(c.half) || !finite(c.offset) || c.half.x <= 0 || c.half.y <= 0 || c.half.z <= 0 ||
            c.shape > phys::Shape3D::Ramp || !world.Has<scene::LocalTransform>(e))
            error = "3D collider requires finite positive dimensions and a transform";
        if (world.Has<phys::Collider2D>(e)) error = "An object cannot have both 2D and 3D colliders";
    });
    world.Query<phys::KinematicBody3D>().Each([&](ecs::Entity e, const auto&) {
        if (!world.Has<CharacterController3D>(e))
            error = "KinematicBody3D requires CharacterController3D; free rigid bodies are unsupported";
    });
    if (localControllers > 1) error = "A Play scene supports one enabled local character controller";
    world.Query<InteractionTarget>().Each([&](ecs::Entity e, const InteractionTarget& t) {
        if (!std::isfinite(t.radius) || t.radius <= 0 || t.radius > 100 || !world.Has<scene::LocalTransform>(e)) error = "Interaction target needs a transform and radius in (0,100]";
    });
    world.Query<ScenePortal>().Each([&](ecs::Entity e, const ScenePortal& p) {
        if (!pathValid(p.scenePath) || p.spawnName.empty()) error = "Portal needs a project assets .scene path and named spawn";
        const auto* collider = world.TryGet<phys::Collider2D>(e);
        const auto* collider3D = world.TryGet<phys::Collider3D>(e);
        if (p.onInteract ? !world.Has<InteractionTarget>(e)
                         : (!collider || !collider->isTrigger) && (!collider3D || !collider3D->isTrigger))
            error = "Portal needs InteractionTarget for E or a trigger collider for automatic entry";
    });
    world.Query<ObjectBehavior>().Each([&](ecs::Entity, const ObjectBehavior& b) {
        if (b.connections.size() > 64 || b.luaSource.size() > 65536) error = "Object behavior exceeds 64 connections or 64 KiB of Lua";
        for (const auto& c : b.connections) {
            if (c.event < ObjectEvent::Start || c.event > ObjectEvent::TriggerExit ||
                c.action < ObjectAction::Message || c.action > ObjectAction::LuaCallback ||
                !std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(c.z))
                error = "Invalid event/action connection";
            if (!c.target.empty() && !names.contains(c.target) && c.action != ObjectAction::ChangeMap) error = "Connection target object does not exist";
            if (c.action == ObjectAction::LuaCallback && (c.text.empty() || c.text.size() > 128)) error = "Lua action needs a callback name of 1..128 bytes";
            if (c.action == ObjectAction::ChangeMap && (!pathValid(c.text) || c.target.empty())) error = "Map connection needs a scene path and spawn name";
        }
    });
    world.Query<scene::MeshRenderer>().Each([&](ecs::Entity, const auto& mesh) {
        if (mesh.depthMode > 2) error = "MeshRenderer depthMode must be 0, 1 or 2";
    });
    world.Query<scene::BillboardRenderer>().Each([&](ecs::Entity e, const auto& billboard) {
        if (billboard.mode > scene::BillboardMode::None || world.Has<scene::SpriteRenderer>(e))
            error = "BillboardRenderer needs a valid mode and cannot share an entity with SpriteRenderer";
    });
    if (!error.empty()) return Error{error, 1};
    scene::UpdateWorldTransforms(world);
    phys::PhysicsWorld3D physics;
    if (auto gathered = phys::GatherPhysicsWorld3D(world, physics); !gathered) return gathered.GetError();
    world.Query<CharacterController3D, scene::LocalTransform>().Each(
        [&](ecs::Entity e, const auto& c, const auto& pose) {
            if (!c.enabled) return;
            phys::MotionState3D state;
            state.position = pose.position;
            auto settings = phys::CharacterSettings3D(world, e);
            if (!settings) error = settings.GetError().message;
            else if (auto moved = physics.Step(state, {}, false, 1.0f / 60, settings.Value()); !moved)
                error = moved.GetError().message;
        });
    if (!error.empty()) return Error{error, 1};
    auto camera = scene::BuildGameView(world, render::Camera2D{});
    if (!camera) return camera.GetError();
    return {};
}
} // namespace mye::runtime
