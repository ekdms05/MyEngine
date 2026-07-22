// SceneSerializer.cpp — World ↔ JSON 씬 직렬화·저장/로드 (docs/07 §3, 오픈이슈 1)
//
// 07 §3·오픈이슈1: 엔티티·컴포넌트(리플렉션)·계층(Parent/Children)·Entity 참조 안정화를
//   JSON에 왕복시킨다. 04 SerializeDynamic·JsonArchive·json::Value 를 소비한다.
//
// [컴포넌트 타입 결정]
//   대상 = "World에 등록되었고 리플렉션 메타가 있는 컴포넌트". World는 풀/타입 열거 공개 API가
//   없으므로 refl::TypeRegistry::All() 의 Struct 타입을 훑어 그 ComponentTypeId 가 World에
//   등록(IsRegistered)돼 있으면 대상으로 삼는다. ComponentTypeId 정본은 04 규약대로
//   refl::TypeId(=HashFnv1a64(리플렉션 이름))이며, 03 MYE_COMPONENT 도 이 값의 별칭이다.
//   (리플렉션 이름과 컴포넌트 이름이 같으면 두 해시가 일치 — 등록 시 이 규약을 지킬 것.)
//
// [Entity 참조 안정화]
//   파일 내부에서는 런타임 Entity(index/generation) 대신 1부터의 안정 로컬 ID를 부여한다.
//   Parent 및 리플렉션 필드 안의 EntityRef(아래 규약)는 이 로컬 ID로 저장하고, 로드 시
//   로컬 ID→새 Entity 매핑으로 재결선한다. Children/WorldTransform 같은 파생·캐시 컴포넌트는
//   직렬화하지 않는다(로드 후 시스템이 재구성).
#include "mye/editor/SceneSerializer.h"

#include "mye/core/Log.h"
#include "mye/ecs/ComponentPool.h"
#include "mye/ecs/ComponentType.h"
#include "mye/ecs/World.h"
#include "mye/refl/TypeId.h"
#include "mye/refl/TypeInfo.h"
#include "mye/refl/TypeRegistry.h"
#include "mye/scene/Transform.h"
#include "mye/ser/JsonArchive.h"
#include "mye/ser/Serialize.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mye::editor {

namespace {

constexpr std::int64_t kSceneVersion = 1;

// 리플렉션 이름으로부터 정본 ComponentTypeId(=refl::TypeId). 03/04 규약 인용.
ecs::ComponentTypeId ComponentIdOf(const refl::TypeInfo& t) {
    return static_cast<ecs::ComponentTypeId>(refl::TypeIdFromName(t.Name()));
}

// EntityRef로 취급할 리플렉션 struct 이름(index:u32, generation:u32 레이아웃 미러).
//   씬 파일에서는 index=로컬 ID, generation=0으로 저장되고 로드 시 실제 Entity로 치환된다.
bool IsEntityRefType(const refl::TypeInfo& t) {
    const std::string_view n = t.Name();
    return n == "mye::ecs::Entity" || n == "ecs::Entity" || n == "Entity";
}

// 파생·캐시 컴포넌트(직렬화 제외) — ComponentTypeId로 판정. 로드 후 TransformSystem이 재구성.
bool IsDerivedComponentId(ecs::ComponentTypeId id) {
    return id == scene::WorldTransform::kComponentTypeId ||
           id == scene::Children::kComponentTypeId;
}

// World에 등록된 "리플렉션 있는 컴포넌트" 타입 목록(파생 제외). 순서 안정(이름 정렬).
std::vector<const refl::TypeInfo*> CollectComponentTypes(const ecs::World& world) {
    std::vector<const refl::TypeInfo*> types;
    for (const refl::TypeInfo* t : refl::TypeRegistry::Get().All()) {
        if (!t || t->GetKind() != refl::Kind::Struct) continue;
        const ecs::ComponentTypeId cid = ComponentIdOf(*t);
        if (IsDerivedComponentId(cid)) continue;
        if (!world.IsRegistered(cid)) continue;
        types.push_back(t);
    }
    std::sort(types.begin(), types.end(), [](const refl::TypeInfo* a, const refl::TypeInfo* b) {
        return a->Name() < b->Name();
    });
    return types;
}

// 안정 로컬 ID 할당기. Entity(index/generation) ↔ 1부터의 로컬 ID.
class LocalIdMap {
public:
    std::uint32_t IdFor(ecs::Entity e) {
        if (e.IsNull()) return 0;
        auto it = m_toLocal.find(e);
        if (it != m_toLocal.end()) return it->second;
        const std::uint32_t id = m_next++;
        m_toLocal.emplace(e, id);
        return id;
    }

private:
    std::unordered_map<ecs::Entity, std::uint32_t> m_toLocal;
    std::uint32_t m_next = 1;
};

// World의 모든 엔티티 열거 — 등록 컴포넌트 풀들의 dense 엔티티 index 합집합.
//   (World가 전역 엔티티 열거 API를 노출하지 않으므로 풀에서 역산한다.)
std::vector<ecs::Entity> EnumerateEntities(const ecs::World& world,
                                           const std::vector<const refl::TypeInfo*>& types) {
    std::vector<std::uint32_t> indices;
    auto addPool = [&](ecs::ComponentTypeId id) {
        const ecs::ComponentPool* pool = world.Pool(id);
        if (!pool) return;
        for (std::uint32_t idx : pool->DenseEntities()) indices.push_back(idx);
    };
    for (const refl::TypeInfo* t : types) addPool(ComponentIdOf(*t));
    // 계층 풀도 포함(직렬화 컴포넌트가 없는 순수 그룹/부모 노드도 반드시 열거해야
    //   자식이 고아가 되지 않는다). Parent=부모 링크 확보, Children=직렬화 컴포넌트 없는
    //   순수 그룹 노드(부모도 없음) 확보. 둘 다 파생/캐시라 직렬화 대상은 아니지만
    //   엔티티 존재 자체는 이 풀들로 역산한다.
    addPool(scene::Parent::kComponentTypeId);
    addPool(scene::Children::kComponentTypeId);

    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

    std::vector<ecs::Entity> out;
    out.reserve(indices.size());
    for (std::uint32_t idx : indices) {
        const std::uint32_t gen = world.EntityGeneration(idx);
        if (gen == 0) continue;
        out.push_back(ecs::Entity{idx, gen});
    }
    return out;
}

// EntityRef(로컬 ID로 저장된 index) → 새 Entity 재결선. 컴포넌트/중첩/벡터 재귀.
void RelinkEntityRefs(const refl::TypeInfo& type, void* instance,
                      const std::unordered_map<std::uint32_t, ecs::Entity>& localToEntity) {
    if (!instance) return;
    switch (type.GetKind()) {
    case refl::Kind::Struct: {
        if (IsEntityRefType(type)) {
            auto* e = static_cast<ecs::Entity*>(instance);
            const std::uint32_t localId = e->index;
            if (localId != 0) {
                auto it = localToEntity.find(localId);
                *e = (it != localToEntity.end()) ? it->second : ecs::Entity::Null();
            } else {
                *e = ecs::Entity::Null();
            }
            return;
        }
        for (const auto& f : type.Fields())
            RelinkEntityRefs(f.Type(), f.GetPtr(instance), localToEntity);
        break;
    }
    case refl::Kind::Vector: {
        const refl::TypeInfo* elem = type.ElementType();
        if (!elem) break;
        const std::size_t n = type.VectorSize(instance);
        for (std::size_t i = 0; i < n; ++i)
            RelinkEntityRefs(*elem, type.VectorElementAt(instance, i), localToEntity);
        break;
    }
    default:
        break;
    }
}

// 쓰기 직후 컴포넌트 json 안의 EntityRef 필드(값이 index=런타임/generation)를 로컬 ID로 치환.
//   SerializeDynamic은 Entity를 struct{index,generation}로 그대로 쓰므로, 여기서 index를
//   로컬 ID로, generation을 0으로 바꾼 json으로 재구성한다.
json::Value RemapEntityRefsToLocal(const refl::TypeInfo& type, const json::Value& value,
                                   const ecs::World& world, LocalIdMap& ids);

json::Value RemapStructRefs(const refl::TypeInfo& type, const json::Value& value,
                            const ecs::World& world, LocalIdMap& ids) {
    if (IsEntityRefType(type)) {
        // {index, generation, __version} → 로컬 ID로 치환.
        std::uint32_t idx = 0, gen = 0;
        if (const json::Value* iv = value.Find("index")) idx = static_cast<std::uint32_t>(iv->AsInt());
        if (const json::Value* gv = value.Find("generation")) gen = static_cast<std::uint32_t>(gv->AsInt());
        ecs::Entity e{idx, gen};
        const std::uint32_t local = (gen != 0 && world.Valid(e)) ? ids.IdFor(e) : 0;
        json::Value::Object o;
        o["__version"] = json::Value(static_cast<std::int64_t>(type.Version()));
        o["index"] = json::Value(static_cast<std::int64_t>(local));
        o["generation"] = json::Value(static_cast<std::int64_t>(0));
        return json::Value(std::move(o));
    }
    if (!value.IsObject()) return value;
    json::Value::Object obj = value.AsObject();
    for (const auto& f : type.Fields()) {
        auto it = obj.find(std::string(f.Name()));
        if (it == obj.end()) continue;
        it->second = RemapEntityRefsToLocal(f.Type(), it->second, world, ids);
    }
    return json::Value(std::move(obj));
}

json::Value RemapEntityRefsToLocal(const refl::TypeInfo& type, const json::Value& value,
                                   const ecs::World& world, LocalIdMap& ids) {
    switch (type.GetKind()) {
    case refl::Kind::Struct:
        return RemapStructRefs(type, value, world, ids);
    case refl::Kind::Vector: {
        const refl::TypeInfo* elem = type.ElementType();
        if (!elem || !value.IsArray()) return value;
        json::Value::Array arr = value.AsArray();
        for (auto& e : arr) e = RemapEntityRefsToLocal(*elem, e, world, ids);
        return json::Value(std::move(arr));
    }
    default:
        return value;
    }
}

// 한 엔티티를 json 오브젝트로. id·parent(로컬 ID)·components{name→값}.
json::Value WriteEntity(const ecs::World& world, ecs::Entity e, LocalIdMap& ids,
                        const std::vector<const refl::TypeInfo*>& types) {
    json::Value::Object obj;
    obj["id"] = json::Value(static_cast<std::int64_t>(ids.IdFor(e)));

    // 실제 런타임 핸들(index/generation)을 부가 기록. 씬 파일 로드는 무시하지만,
    //   DestroyEntityCommand undo(preserveHandles=true)가 원래 핸들 복원에 소비한다.
    {
        json::Value::Array handle;
        handle.push_back(json::Value(static_cast<std::int64_t>(e.index)));
        handle.push_back(json::Value(static_cast<std::int64_t>(e.generation)));
        obj["__handle"] = json::Value(std::move(handle));
    }

    if (const auto* parent = static_cast<const scene::Parent*>(
            world.TryGetDynamic(e, scene::Parent::kComponentTypeId))) {
        if (!parent->parent.IsNull())
            obj["parent"] = json::Value(static_cast<std::int64_t>(ids.IdFor(parent->parent)));
    }

    json::Value::Object comps;
    for (const refl::TypeInfo* t : types) {
        const ecs::ComponentTypeId cid = ComponentIdOf(*t);
        const void* raw = world.TryGetDynamic(e, cid);
        if (!raw) continue;
        auto wr = ser::JsonArchive::ForWrite();
        // 쓰기 모드에서는 값을 변경하지 않는다(const_cast 안전).
        auto r = ser::SerializeDynamic(wr, *t, const_cast<void*>(raw));
        if (!r) continue;
        json::Value remapped = RemapEntityRefsToLocal(*t, wr.Root(), world, ids);
        comps.emplace(std::string(t->Name()), std::move(remapped));
    }
    obj["components"] = json::Value(std::move(comps));
    return json::Value(std::move(obj));
}

struct PendingEntity {
    ecs::Entity        entity;
    std::uint32_t      parentLocalId = 0;   // 0 = 루트
    const json::Value* components = nullptr;
};

} // namespace

// ---- World → JSON ----
Expected<json::Value, Error> SceneSerializer::WriteWorld(const ecs::World& world) const {
    const std::vector<const refl::TypeInfo*> types = CollectComponentTypes(world);
    LocalIdMap ids;
    json::Value::Array entities;
    for (ecs::Entity e : EnumerateEntities(world, types))
        entities.push_back(WriteEntity(world, e, ids, types));

    json::Value::Object root;
    root["__version"] = json::Value(kSceneVersion);
    root["entities"] = json::Value(std::move(entities));
    return json::Value(std::move(root));
}

Expected<json::Value, Error>
SceneSerializer::WriteSubtree(const ecs::World& world, ecs::Entity root) const {
    if (!world.Valid(root))
        return Error{"WriteSubtree: invalid root entity", 1};

    const std::vector<const refl::TypeInfo*> types = CollectComponentTypes(world);

    // root와 자손 수집(Children 캐시 기반 DFS).
    std::vector<ecs::Entity> subtree;
    std::vector<ecs::Entity> stack{root};
    while (!stack.empty()) {
        ecs::Entity cur = stack.back();
        stack.pop_back();
        subtree.push_back(cur);
        if (const auto* ch = static_cast<const scene::Children*>(
                world.TryGetDynamic(cur, scene::Children::kComponentTypeId))) {
            for (ecs::Entity kid : ch->list)
                if (world.Valid(kid)) stack.push_back(kid);
        }
    }

    LocalIdMap ids;
    ids.IdFor(root);   // 루트를 로컬 ID 1로 고정.

    json::Value::Array entities;
    for (ecs::Entity e : subtree) {
        json::Value ev = WriteEntity(world, e, ids, types);
        // 루트의 부모(서브트리 밖) 참조는 끊는다.
        if (e == root && ev.IsObject()) {
            json::Value::Object obj = ev.AsObject();
            obj.erase("parent");
            ev = json::Value(std::move(obj));
        }
        entities.push_back(std::move(ev));
    }

    json::Value::Object obj;
    obj["__version"] = json::Value(kSceneVersion);
    obj["entities"] = json::Value(std::move(entities));
    return json::Value(std::move(obj));
}

// ---- JSON → World ----
Expected<std::vector<ecs::Entity>, Error>
SceneSerializer::ReadInto(ecs::World& world, const json::Value& in, bool preserveHandles) const {
    if (!in.IsObject())
        return Error{"ReadInto: root is not an object", 1};
    const json::Value* entsV = in.Find("entities");
    if (!entsV || !entsV->IsArray())
        return Error{"ReadInto: missing entities array", 2};

    std::unordered_map<std::uint32_t, ecs::Entity> localToEntity;
    std::vector<PendingEntity> pending;
    pending.reserve(entsV->AsArray().size());

    // 패스 1: 엔티티 생성 + 로컬 ID 매핑.
    for (const json::Value& ev : entsV->AsArray()) {
        if (!ev.IsObject()) continue;
        const json::Value* idv = ev.Find("id");
        const std::uint32_t localId = idv ? static_cast<std::uint32_t>(idv->AsInt()) : 0;
        if (localId == 0) continue;   // 유효 로컬 ID는 1부터

        // preserveHandles: "__handle":[index,generation] 로 원래 핸들 복원(CreateWithId).
        //   실패(핸들 없음/슬롯이 살아있음/세대 0) 시 Create() 폴백 → 항상 유효 엔티티 확보.
        ecs::Entity e = ecs::Entity::Null();
        if (preserveHandles) {
            const json::Value* hv = ev.Find("__handle");
            if (hv && hv->IsArray() && hv->AsArray().size() == 2) {
                const auto idx = static_cast<std::uint32_t>(hv->AsArray()[0].AsInt());
                const auto gen = static_cast<std::uint32_t>(hv->AsArray()[1].AsInt());
                e = world.CreateWithId(idx, gen);
            }
        }
        if (e.IsNull()) e = world.Create();
        localToEntity.emplace(localId, e);

        PendingEntity pe;
        pe.entity = e;
        if (const json::Value* p = ev.Find("parent"))
            pe.parentLocalId = static_cast<std::uint32_t>(p->AsInt());
        pe.components = ev.Find("components");
        pending.push_back(pe);
    }

    // 패스 2: 컴포넌트 역직렬화 + EntityRef/Parent 재결선.
    std::vector<ecs::Entity> roots;
    for (const PendingEntity& pe : pending) {
        if (pe.components && pe.components->IsObject()) {
            for (const auto& [name, compValue] : pe.components->AsObject()) {
                const refl::TypeInfo* t = refl::TypeRegistry::Get().Find(std::string_view(name));
                if (!t) {
                    // 스킵 = 조용한 데이터 손실. 리플렉션 이름 ↔ MYE_COMPONENT 이름 해시
                    //   불일치(03/04 정본 규약 위반)일 수 있으므로 반드시 로그로 드러낸다.
                    MYE_LOG_WARN("SceneSerializer",
                                 "ReadInto: 미등록 컴포넌트 '{}' 스킵(데이터 손실) — "
                                 "리플렉션 이름/등록 규약 확인",
                                 name);
                    continue;
                }
                void* raw = world.AddDynamic(pe.entity, ComponentIdOf(*t));
                if (!raw) continue;
                auto rd = ser::JsonArchive::ForRead(compValue);
                (void)ser::SerializeDynamic(rd, *t, raw);
                RelinkEntityRefs(*t, raw, localToEntity);
            }
        }

        // Parent 재결선(로컬 ID → 새 Entity). Children 캐시는 TransformSystem이 재구성.
        if (pe.parentLocalId != 0) {
            auto it = localToEntity.find(pe.parentLocalId);
            if (it != localToEntity.end()) {
                auto* parent = static_cast<scene::Parent*>(
                    world.AddDynamic(pe.entity, scene::Parent::kComponentTypeId));
                if (parent) parent->parent = it->second;
            }
        } else {
            roots.push_back(pe.entity);
        }
    }

    return roots;
}

// ---- 파일 I/O ----
Expected<void, Error>
SceneSerializer::SaveToFile(const ecs::World& world, std::string_view path) const {
    auto tree = WriteWorld(world);
    if (!tree) return tree.GetError();
    const std::string text = json::Stringify(tree.Value());
    std::ofstream out(std::string(path), std::ios::binary | std::ios::trunc);
    if (!out) return Error{"SaveToFile: cannot open '" + std::string(path) + "'", 3};
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) return Error{"SaveToFile: write failed", 4};
    return {};
}

Expected<std::vector<ecs::Entity>, Error>
SceneSerializer::LoadFromFile(ecs::World& world, std::string_view path) const {
    std::ifstream in(std::string(path), std::ios::binary);
    if (!in) return Error{"LoadFromFile: cannot open '" + std::string(path) + "'", 3};
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto parsed = json::Parse(text);
    if (!parsed) return parsed.GetError();
    return ReadInto(world, parsed.Value());
}

// ---- 메모리 스냅샷 ----
Expected<std::string, Error> SceneSerializer::Snapshot(const ecs::World& world) const {
    auto tree = WriteWorld(world);
    if (!tree) return tree.GetError();
    return json::Stringify(tree.Value(), 0);
}

Expected<void, Error> SceneSerializer::Restore(ecs::World& world, std::string_view blob) const {
    auto parsed = json::Parse(blob);
    if (!parsed) return parsed.GetError();
    auto r = ReadInto(world, parsed.Value());
    if (!r) return r.GetError();
    return {};
}

} // namespace mye::editor
