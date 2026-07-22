// mye/script/src/bindings/EcsBindings.cpp — 엔티티·컴포넌트·스폰/파괴 바인딩 (docs/05, M3-C)
//
// mye.Entity: {ecs::Entity + World*} 경량 래퍼. 주요 컴포넌트 접근자를 메서드로 노출한다:
//   - Transform 위치(get/set_position)
//   - KinematicBody2D 속도(get/set_velocity)
//   - SpriteAnimator 파라미터(set_bool/set_float/set_trigger), 방향(set_facing/facing_from_move)
// mye.world: spawn/destroy(CommandBuffer 경유 지연) + find/valid.
//
// 안전: World/컴포넌트 미보유·무효 핸들 접근은 크래시 대신 안전값 반환 또는 no-op.
//   구조 변경(spawn/destroy)은 순회 중 금지 규약을 지키려 EcsBindingModule 이 소유한
//   CommandBuffer 에 기록하고, 앱/ScriptSystem 이 Update 페이즈 경계에서 FlushDeferred() 한다.
#include "mye/script/bindings/EngineBindings.h"

#include "mye/anim/AnimationTypes.h"
#include "mye/anim/SpriteAnimator.h"
#include "mye/core/Math.h"
#include "mye/ecs/CommandBuffer.h"
#include "mye/ecs/Entity.h"
#include "mye/ecs/World.h"
#include "mye/phys/Collision.h"
#include "mye/scene/Transform.h"

#include <sol/sol.hpp>

#include <string>

namespace mye::script {

namespace {

// Lua 에 노출하는 엔티티 핸들 — 값 타입(복사 가능). World 는 비소유 포인터.
struct LuaEntity {
    ecs::Entity  e{};
    ecs::World*  world = nullptr;

    bool IsValid() const { return world && world->Valid(e); }
};

// SpriteAnimator 파라미터 편의 접근(널 안전).
anim::SpriteAnimator* GetAnimator(const LuaEntity& le) {
    if (!le.IsValid()) return nullptr;
    return le.world->TryGet<anim::SpriteAnimator>(le.e);
}

} // namespace

EcsBindingModule::EcsBindingModule(ecs::World* world) : m_world(world) {
    if (m_world) m_commands = std::make_unique<ecs::CommandBuffer>(*m_world);
}

EcsBindingModule::~EcsBindingModule() = default;

void EcsBindingModule::Register(sol::state& lua) {
    ecs::World* world = m_world;   // 캡처용 로컬(비소유).
    sol::table mye = lua["mye"].get_or_create<sol::table>();

    // ---- mye.Entity usertype ------------------------------------------
    mye.new_usertype<LuaEntity>(
        "Entity",
        // Lua 에서 직접 생성 금지(핸들은 엔진이 발급) — 기본 생성자만(null 핸들).
        sol::no_constructor,

        "is_valid", [](const LuaEntity& le) { return le.IsValid(); },
        "packed", [](const LuaEntity& le) { return le.e.Packed(); },
        sol::meta_function::equal_to,
        [](const LuaEntity& a, const LuaEntity& b) { return a.e == b.e; },
        sol::meta_function::to_string,
        [](const LuaEntity& le) {
            return "Entity(" + std::to_string(le.e.index) + ":" +
                   std::to_string(le.e.generation) + ")";
        },

        // ---- Transform 위치 (LocalTransform.position, XY 평면) ----
        "get_position",
        [](const LuaEntity& le) -> Vec2 {
            if (!le.IsValid()) return Vec2{};
            auto* t = le.world->TryGet<scene::LocalTransform>(le.e);
            return t ? Vec2{t->position.x, t->position.y} : Vec2{};
        },
        "set_position",
        [](const LuaEntity& le, const Vec2& p) {
            if (!le.IsValid()) return;
            auto* t = le.world->TryGet<scene::LocalTransform>(le.e);
            if (!t) return;
            t->position.x = p.x;
            t->position.y = p.y;
            t->dirty = true;   // TransformSystem 증분 갱신 트리거.
        },

        // ---- KinematicBody2D 속도 ----
        "get_velocity",
        [](const LuaEntity& le) -> Vec2 {
            if (!le.IsValid()) return Vec2{};
            auto* b = le.world->TryGet<phys::KinematicBody2D>(le.e);
            return b ? b->velocity : Vec2{};
        },
        "set_velocity",
        [](const LuaEntity& le, const Vec2& v) {
            if (!le.IsValid()) return;
            auto* b = le.world->TryGet<phys::KinematicBody2D>(le.e);
            if (b) b->velocity = v;
        },
        "hit_wall",
        [](const LuaEntity& le) {
            if (!le.IsValid()) return false;
            auto* b = le.world->TryGet<phys::KinematicBody2D>(le.e);
            return b ? b->hitWall : false;
        },

        // ---- SpriteAnimator 파라미터·방향 ----
        "set_bool",
        [](const LuaEntity& le, const std::string& name, bool v) {
            if (auto* a = GetAnimator(le)) a->SetBool(name, v);
        },
        "set_float",
        [](const LuaEntity& le, const std::string& name, float v) {
            if (auto* a = GetAnimator(le)) a->SetFloat(name, v);
        },
        "set_trigger",
        [](const LuaEntity& le, const std::string& name) {
            if (auto* a = GetAnimator(le)) a->SetTrigger(name);
        },
        "get_float",
        [](const LuaEntity& le, const std::string& name) -> float {
            auto* a = GetAnimator(le);
            return a ? a->GetFloat(name) : 0.0f;
        },
        "get_bool",
        [](const LuaEntity& le, const std::string& name) -> bool {
            auto* a = GetAnimator(le);
            return a ? a->GetBool(name) : false;
        },
        // 방향 벡터로 facing 갱신(입력 방향 → 8방향 스냅). 이전 방향 유지(영벡터 시).
        "face_move",
        [](const LuaEntity& le, const Vec2& dir) {
            auto* a = GetAnimator(le);
            if (a) a->facing = anim::Dir8FromVector(dir, a->facing);
        },
        // 현재 방향의 단위 벡터 조회(월드, +Y 업).
        "facing_vector",
        [](const LuaEntity& le) -> Vec2 {
            auto* a = GetAnimator(le);
            return a ? anim::Dir8Vector(a->facing) : Vec2{0.0f, -1.0f};
        },
        "facing_index",
        [](const LuaEntity& le) -> int {
            auto* a = GetAnimator(le);
            return a ? static_cast<int>(a->facing) : 0;
        },

        // ---- 존재 질의 ----
        "has_animator",
        [](const LuaEntity& le) {
            return le.IsValid() && le.world->Has<anim::SpriteAnimator>(le.e);
        },
        "has_body",
        [](const LuaEntity& le) {
            return le.IsValid() && le.world->Has<phys::KinematicBody2D>(le.e);
        },

        // ---- 파괴(지연: CommandBuffer 경유) ----
        "destroy",
        [](const LuaEntity& le) {
            if (le.IsValid()) le.world->Destroy(le.e);
        });

    // ---- mye.world 테이블 ---------------------------------------------
    sol::table worldTbl = mye["world"].get_or_create<sol::table>();

    // 무효/비소유 world 면 안전 no-op·null 핸들 반환.
    worldTbl.set_function("valid", [world](const LuaEntity& le) {
        return world && le.world && world->Valid(le.e);
    });

    // 즉시 유효 핸들 반환(World 가 index/generation 예약). 저장소 반영은 Flush(비순회 경로).
    // NOTE: ScriptSystem/앱이 Update 페이즈 경계에서 EcsBindingModule::FlushDeferred() 를
    //   호출해야 실제 반영된다(순회 중 구조변경 금지 규약 준수).
    worldTbl.set_function("spawn", [this]() -> LuaEntity {
        if (!m_world) return LuaEntity{};
        ecs::Entity e = m_commands ? m_commands->CreateDeferred() : m_world->Create();
        return LuaEntity{e, m_world};
    });
    worldTbl.set_function("destroy", [this](const LuaEntity& le) {
        if (!m_world || le.e.IsNull()) return;
        if (m_commands) m_commands->Destroy(le.e);
        else if (m_world->Valid(le.e)) m_world->Destroy(le.e);
    });
    // 패킹된 64비트 핸들 → mye.Entity(직렬화·이벤트 페이로드 경계에서 복원).
    worldTbl.set_function("entity_from_packed", [world](uint64_t packed) -> LuaEntity {
        return LuaEntity{ecs::Entity::FromPacked(packed), world};
    });
}

// 앱/ScriptSystem 이 Update 페이즈 경계에서 호출 — 스크립트가 기록한 spawn/destroy 반영.
void EcsBindingModule::FlushDeferred() {
    if (m_commands) m_commands->Flush();
}

} // namespace mye::script
