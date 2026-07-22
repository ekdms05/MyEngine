// Command.cpp — 커맨드 기반 Undo/Redo 구현 (docs/07 §4)
//
// 07 §4: 패널이 World를 직접 만지지 않고 모든 편집이 IEditorCommand를 경유한다.
//   - PropertyEditCommand: 리플렉션 PropertyPath + old/new 값 blob(04 JsonArchive)으로 임의
//     컴포넌트를 제네릭 Undo. dynamic_cast 금지 — 커맨드 종류 태그(Kind())로 병합 판정.
//   - 구조 커맨드: 엔티티 생성/삭제/재부모화/컴포넌트 add·remove. World 직접 변경 경로 사용
//     (에디터 편집은 비순회 컨텍스트 — World.h 규약이 직접 변경 허용). 삭제는 서브트리 blob
//     스냅샷(SceneSerializer)으로 Undo 복원.
#include "mye/editor/Command.h"
#include "mye/editor/EditorContext.h"
#include "mye/editor/SceneSerializer.h"
#include "mye/editor/Selection.h"

#include "mye/core/Json.h"
#include "mye/ecs/World.h"
#include "mye/refl/TypeInfo.h"
#include "mye/scene/Transform.h"
#include "mye/ser/JsonArchive.h"
#include "mye/ser/Serialize.h"

#include <vector>

namespace mye::editor {

namespace {

// 편집 대상 World — 07 §3: 언제나 activeWorld() 경유(Edit=편집 World, Play=Play World).
ecs::World* WorldOf(EditorContext& ctx) { return ctx.activeWorld(); }

// TypeInfo 기반 풀 등록 보장(add/remove 동적 경로).
//
// 정상 경로에서는 컴포넌트 타입 풀이 World 구성 시(SceneModule/사용자의
//   RegisterComponent<C>(name), 완전한 construct/destruct/moveConstruct 포함) 이미 등록돼
//   있어 이 함수는 즉시 반환한다. 미등록 타입에만 쓰이는 최소 fallback은 POD 컴포넌트를
//   가정한다: 풀 storage는 resize 시 0으로 값초기화되므로 construct 생략이 곧 0-초기화이고,
//   직후 값 blob 역직렬화가 실제 값을 채운다. destruct/moveConstruct는 null(스왑-리무브 시
//   trivially-relocatable 가정). 리소스 소유 컴포넌트는 반드시 사전에 완전한 desc로 등록할 것.
void EnsurePool(ecs::World& world, const refl::TypeInfo& type) {
    const auto id = static_cast<ecs::ComponentTypeId>(type.Id());
    if (world.IsRegistered(id)) return;
    ecs::ComponentTypeDesc d;
    d.name = "<reflected-component>";
    d.id = id;
    d.size = type.Size();
    d.align = type.Align();
    d.reflection = &type;
    world.RegisterComponent(d);
}

// 컴포넌트 인스턴스 → 값 blob(JSON 텍스트). 리플렉션 전체 순회.
std::string SnapshotComponent(const refl::TypeInfo& type, const void* instance) {
    auto ar = ser::JsonArchive::ForWrite();
    // SerializeDynamic은 void*(비상수) — 스냅샷은 읽기 전용이라 const 제거는 안전(쓰지 않음).
    (void)ser::SerializeDynamic(ar, type, const_cast<void*>(instance));
    return json::Stringify(ar.Root());
}

// 값 blob → 컴포넌트 인스턴스. 실패는 무시(부분 복원 허용).
void RestoreComponent(const refl::TypeInfo& type, void* instance, std::string_view blob) {
    if (blob.empty()) return;
    auto parsed = json::Parse(blob);
    if (!parsed) return;
    auto ar = ser::JsonArchive::ForRead(parsed.Value());
    (void)ser::SerializeDynamic(ar, type, instance);
}

} // namespace

// ---- PropertyEditCommand ----
PropertyEditCommand::PropertyEditCommand(ObjectRef target, refl::PropertyPath path,
                                         ValueBlob oldValue, ValueBlob newValue,
                                         std::string label)
    : m_target(std::move(target)),
      m_path(std::move(path)),
      m_oldValue(std::move(oldValue)),
      m_newValue(std::move(newValue)),
      m_label(std::move(label)),
      m_stamp{} {}   // 스탬프는 Execute/Push 시점에 찍는다(생성→push 지연이 병합 창을 갉지 않도록).

namespace {
// 값 blob을 target(엔티티 컴포넌트)의 path 리프에 기록. 성공 여부 반환.
bool ApplyBlob(EditorContext& ctx, const ObjectRef& target, const refl::PropertyPath& path,
               const ValueBlob& blob) {
    if (!target.componentType) return false;
    ecs::World* world = WorldOf(ctx);
    if (!world) return false;
    const ecs::Entity e = target.Entity();
    if (!world->Valid(e)) return false;
    void* comp = world->TryGetDynamic(
        e, static_cast<ecs::ComponentTypeId>(target.componentType->Id()));
    if (!comp) return false;
    if (blob.Empty()) return false;
    auto parsed = json::Parse(blob.json);
    if (!parsed) return false;
    auto ar = ser::JsonArchive::ForRead(parsed.Value());
    auto r = refl::WriteValue(*target.componentType, comp, path, ar);
    return static_cast<bool>(r);
}
} // namespace

void PropertyEditCommand::Execute(EditorContext& ctx) {
    // 병합 창은 "적용 시각" 기준이어야 한다 — 생성자 시각이면 construct→push 지연만큼
    //   창이 줄고 다음 편집이 조기에 병합 실패한다. push 경로에서 Execute가 불리므로 여기서 찍는다.
    m_stamp = std::chrono::steady_clock::now();
    (void)ApplyBlob(ctx, m_target, m_path, m_newValue);
}
void PropertyEditCommand::Undo(EditorContext& ctx) { (void)ApplyBlob(ctx, m_target, m_path, m_oldValue); }

bool PropertyEditCommand::TryMerge(const IEditorCommand& next) {
    // dynamic_cast 금지 — Kind 태그로 같은 커맨드 종류인지 판정한 뒤 target/path 비교.
    if (next.Kind() != CommandKind::PropertyEdit) return false;
    const auto& other = static_cast<const PropertyEditCommand&>(next);
    if (!(other.m_target == m_target)) return false;
    if (!(other.m_path == m_path)) return false;
    // 병합 창(400ms) 안이면 흡수: newValue만 갱신(oldValue는 최초 것 유지 → 한 번의 Undo로 원복).
    const auto now = std::chrono::steady_clock::now();
    const auto gap = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_stamp);
    if (gap > std::chrono::milliseconds(400)) return false;
    m_newValue = other.m_newValue;
    m_stamp = now;
    return true;
}

// ---- CreateEntityCommand ----
CreateEntityCommand::CreateEntityCommand(ecs::Entity parent, std::string label)
    : m_parent(parent), m_label(std::move(label)) {}

void CreateEntityCommand::Execute(EditorContext& ctx) {
    ecs::World* world = WorldOf(ctx);
    if (!world) return;
    // Redo 시 이전에 만든 엔티티는 이미 파괴됐으므로 매번 새로 생성한다(핸들 갱신).
    m_created = world->Create();
    // 기본 컴포넌트: 편집 대상이 되도록 LocalTransform 부여(에디터 신규 엔티티 규약).
    world->Add<scene::LocalTransform>(m_created);
    world->Add<scene::WorldTransform>(m_created);
    if (!m_parent.IsNull() && world->Valid(m_parent))
        scene::ApplyReparent(*world, m_created, m_parent, /*keepWorld*/ false);
}

void CreateEntityCommand::Undo(EditorContext& ctx) {
    ecs::World* world = WorldOf(ctx);
    if (!world || m_created.IsNull()) return;
    if (world->Valid(m_created)) world->Destroy(m_created);
    m_created = ecs::Entity::Null();
}

// ---- DestroyEntityCommand ----
DestroyEntityCommand::DestroyEntityCommand(ecs::Entity target, std::string label)
    : m_target(target), m_label(std::move(label)) {}

namespace {
// 서브트리(root+자손) 파괴. 자식 먼저 파괴하도록 후위 순회.
void DestroySubtree(ecs::World& world, ecs::Entity root) {
    if (!world.Valid(root)) return;
    if (scene::Children* ch = world.TryGet<scene::Children>(root)) {
        // 복사 후 순회(파괴가 부모 Children 캐시를 건드림).
        std::vector<ecs::Entity> kids = ch->list;
        for (ecs::Entity k : kids) DestroySubtree(world, k);
    }
    // 부모 Children 캐시에서 자신 제거(재부모화 헬퍼 재사용).
    scene::ApplyReparent(world, root, ecs::Entity::Null(), /*keepWorld*/ false);
    world.Destroy(root);
}
} // namespace

void DestroyEntityCommand::Execute(EditorContext& ctx) {
    ecs::World* world = WorldOf(ctx);
    if (!world || !world->Valid(m_target)) return;
    // 파괴 전 루트의 부모를 기록한다. WriteSubtree는 서브트리를 자족적으로 만들기 위해 루트의
    //   parent 필드를 지운다(프리팹/스냅샷 규약) — 그대로 Undo하면 루트가 최상위로 되살아나
    //   재부모화가 유실된다. 여기서 부모를 보관해 Undo 후 재적용한다.
    if (const auto* p = static_cast<const scene::Parent*>(
            world->TryGetDynamic(m_target, scene::Parent::kComponentTypeId)))
        m_oldParent = p->parent;
    else
        m_oldParent = ecs::Entity::Null();

    // Undo 복원용 서브트리 스냅샷(07 §4). SceneSerializer가 로컬 ID·참조 재결선을 책임진다.
    SceneSerializer ser;
    auto snap = ser.WriteSubtree(*world, m_target);
    if (snap) m_subtreeSnapshot = json::Stringify(snap.Value());
    DestroySubtree(*world, m_target);
}

void DestroyEntityCommand::Undo(EditorContext& ctx) {
    ecs::World* world = WorldOf(ctx);
    if (!world || m_subtreeSnapshot.empty()) return;
    auto parsed = json::Parse(m_subtreeSnapshot);
    if (!parsed) return;
    SceneSerializer ser;
    const ecs::Entity oldRoot = m_target;
    // preserveHandles=true: 파괴된 슬롯을 원래 핸들(index/generation)로 되살린다(World::CreateWithId).
    //   → 파괴 전 EntityRef/선택이 그대로 유효(M4 리뷰 후속 실 해결). 루트/자손 모두 옛 핸들 유지.
    auto roots = ser.ReadInto(*world, parsed.Value(), /*preserveHandles=*/true);
    // 복원된 루트 핸들 — preserveHandles가 성공하면 oldRoot와 동일, 실패 폴백 시에만 새 핸들.
    if (roots && !roots.Value().empty()) {
        const ecs::Entity newRoot = roots.Value().front();
        m_target = newRoot;
        // 루트의 원래 부모 재적용(WriteSubtree가 끊었던 서브트리 밖 부모 링크 복원).
        if (!m_oldParent.IsNull() && world->Valid(m_oldParent))
            scene::ApplyReparent(*world, newRoot, m_oldParent, /*keepWorld*/ false);
        // 파괴로 dangling된 선택 상태를 새 루트 핸들로 재결선(선택이 파괴된 루트를 가리켰던 경우).
        //   자손·EntityRef 필드까지 완전 재결선하려면 엔진의 핸들-보존 재생성 API가 필요하나(M5),
        //   가장 흔한 dangling 케이스인 "삭제된 엔티티가 선택돼 있던 경우"는 여기서 복구한다.
        if (ctx.selection) {
            const SelectableRef oldRef = SelectableRef::OfEntity(oldRoot);
            if (ctx.selection->IsSelected(oldRef))
                ctx.selection->Select(SelectableRef::OfEntity(newRoot), SelectMode::Replace);
        }
    }
}

// ---- ReparentCommand ----
ReparentCommand::ReparentCommand(ecs::Entity child, ecs::Entity newParent, bool keepWorld,
                                 std::string label)
    : m_child(child), m_newParent(newParent), m_keepWorld(keepWorld), m_label(std::move(label)) {}

void ReparentCommand::Execute(EditorContext& ctx) {
    ecs::World* world = WorldOf(ctx);
    if (!world || !world->Valid(m_child)) return;
    // Undo 복원을 위해 현재 부모 기록.
    if (scene::Parent* p = world->TryGet<scene::Parent>(m_child))
        m_oldParent = p->parent;
    else
        m_oldParent = ecs::Entity::Null();
    scene::ApplyReparent(*world, m_child, m_newParent, m_keepWorld);
}

void ReparentCommand::Undo(EditorContext& ctx) {
    ecs::World* world = WorldOf(ctx);
    if (!world || !world->Valid(m_child)) return;
    scene::ApplyReparent(*world, m_child, m_oldParent, m_keepWorld);
}

// ---- AddComponentCommand ----
AddComponentCommand::AddComponentCommand(ecs::Entity target, const refl::TypeInfo& componentType,
                                         std::string label)
    : m_target(target), m_type(&componentType), m_label(std::move(label)) {}

void AddComponentCommand::Execute(EditorContext& ctx) {
    ecs::World* world = WorldOf(ctx);
    if (!world || !m_type || !world->Valid(m_target)) return;
    EnsurePool(*world, *m_type);
    world->AddDynamic(m_target, static_cast<ecs::ComponentTypeId>(m_type->Id()));
}

void AddComponentCommand::Undo(EditorContext& ctx) {
    ecs::World* world = WorldOf(ctx);
    if (!world || !m_type || !world->Valid(m_target)) return;
    world->RemoveDynamic(m_target, static_cast<ecs::ComponentTypeId>(m_type->Id()));
}

// ---- RemoveComponentCommand ----
RemoveComponentCommand::RemoveComponentCommand(ecs::Entity target,
                                               const refl::TypeInfo& componentType,
                                               std::string label)
    : m_target(target), m_type(&componentType), m_label(std::move(label)) {}

void RemoveComponentCommand::Execute(EditorContext& ctx) {
    ecs::World* world = WorldOf(ctx);
    if (!world || !m_type || !world->Valid(m_target)) return;
    const auto id = static_cast<ecs::ComponentTypeId>(m_type->Id());
    if (void* comp = world->TryGetDynamic(m_target, id))
        m_valueSnapshot = SnapshotComponent(*m_type, comp);   // Undo 복원 값 blob.
    world->RemoveDynamic(m_target, id);
}

void RemoveComponentCommand::Undo(EditorContext& ctx) {
    ecs::World* world = WorldOf(ctx);
    if (!world || !m_type || !world->Valid(m_target)) return;
    EnsurePool(*world, *m_type);
    void* comp = world->AddDynamic(m_target, static_cast<ecs::ComponentTypeId>(m_type->Id()));
    if (comp) RestoreComponent(*m_type, comp, m_valueSnapshot);
}

} // namespace mye::editor
