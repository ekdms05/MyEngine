// mye/editor/SceneSerializer.h — World ↔ JSON 씬 직렬화·저장/로드 (docs/07 §3, §오픈이슈 1)
//
// World의 엔티티·컴포넌트(리플렉션)·계층(Parent/Children)·Entity 참조를 안정적으로 JSON에
// 왕복시킨다. 07 오픈이슈 1: 씬은 텍스트(JSON) 저장 권장(VCS diff). 04 리플렉션·직렬화를 소비.
//
// Entity 참조 안정화: 파일 내부에서는 런타임 Entity(index/generation) 대신 안정적 로컬 ID를
//   부여하고, EntityRef 필드는 그 로컬 ID로 저장한다. 로드 시 로컬 ID → 새 Entity 매핑을 만들어
//   참조를 재결선한다(07 §3 "Entity 참조 안정화").
//
// 플레이 스냅샷(07 §3)도 이 직렬화기를 인메모리(json::Value 또는 MemoryStream)로 재사용한다.
#pragma once

#include "mye/core/Base.h"
#include "mye/core/Json.h"
#include "mye/ecs/Entity.h"

#include <string>
#include <string_view>
#include <vector>

namespace mye::ecs { class World; }

namespace mye::editor {

// 직렬화가 저장/복원할 컴포넌트 타입 집합의 결정 규약:
//   World에 등록된 컴포넌트 중 refl::TypeInfo(리플렉션 메타)가 있는 것만 대상(ComponentTypeDesc
//   ::reflection != nullptr). 리플렉션 미등록 런타임 전용 컴포넌트(WorldTransform 캐시 등)는 스킵.
class SceneSerializer {
public:
    SceneSerializer() = default;

    // ---- World → JSON ----
    // 전체 World를 json::Value(오브젝트: version·entities[])로 직렬화.
    Expected<json::Value, Error> WriteWorld(const ecs::World& world) const;

    // 서브트리(root와 그 자손)만 직렬화 — DestroyEntityCommand undo 스냅샷·프리팹 캡처용.
    Expected<json::Value, Error> WriteSubtree(const ecs::World& world, ecs::Entity root) const;

    // ---- JSON → World ----
    // json을 world에 인스턴스화(기존 내용에 추가 로드). 생성된 루트 엔티티들을 반환.
    //
    // preserveHandles=true 이면 각 엔티티의 "__handle":[index,generation] 필드를 소비해
    //   World::CreateWithId 로 원래 핸들 그대로 복원한다(DestroyEntityCommand undo 전용 —
    //   파괴→Undo 후 옛 EntityRef/선택이 그대로 유효해야 하는 M4 리뷰 후속). 핸들이 없거나
    //   재생성 실패(슬롯이 이미 살아있음 등) 시 해당 엔티티는 Create() 로 폴백한다.
    //   씬 파일 로드(WriteWorld 결과)는 항상 preserveHandles=false(핸들 충돌 방지).
    Expected<std::vector<ecs::Entity>, Error>
    ReadInto(ecs::World& world, const json::Value& in, bool preserveHandles = false) const;

    // ---- 파일 I/O (저장/로드) ----
    // JSON 텍스트를 <path>에 저장(UTF-8). 04 파일시스템/VFS 규약 준수.
    Expected<void, Error> SaveToFile(const ecs::World& world, std::string_view path) const;
    // 파일을 읽어 새 내용으로 로드(호출자가 world를 비운 상태 전제). 루트 엔티티 반환.
    Expected<std::vector<ecs::Entity>, Error>
    LoadFromFile(ecs::World& world, std::string_view path) const;

    // ---- 메모리 스냅샷 (플레이 모드·Undo, 07 §3) ----
    // World 전체를 불투명 blob(JSON 텍스트)으로. Snapshot/Restore 대칭.
    Expected<std::string, Error> Snapshot(const ecs::World& world) const;
    Expected<void, Error>        Restore(ecs::World& world, std::string_view blob) const;
};

} // namespace mye::editor
