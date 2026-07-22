// mye/editor/Command.h — 커맨드 기반 Undo/Redo 기반 계약 (docs/07 §4)
//
// 07 §4 원칙: 패널이 World를 직접 만지는 것은 금지 — 모든 편집은 IEditorCommand 경유.
//   프로퍼티 변경은 리플렉션 기반 제네릭 PropertyEditCommand 하나로 처리(컴포넌트마다 커맨드
//   불필요, 플러그인 컴포넌트도 자동 Undo). 구조 변경은 전용 커맨드.
//
// 값 blob은 04 직렬화(MemoryStream/JSON)로 만든다 — 06 세이브·03 프리팹과 동일 프레임워크.
// 이 헤더는 두 구현 에이전트의 동결 계약이다(시그니처 변경 금지).
#pragma once

#include "mye/editor/EditorTypes.h"
#include "mye/refl/PropertyPath.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mye::editor {

// ---- 값 blob — Undo 이전/이후 값 스냅샷 (04 직렬화 산출물) ----
// 07 §4: PropertyEditCommand{ oldValueBlob, newValueBlob }. 리프 프로퍼티 하나의 직렬화 형태.
//   v1 구현은 refl::ReadValue/WriteValue를 JsonArchive(json::Value 텍스트)로 왕복해 채운다.
//   포맷은 구현 세부(바이너리 MemoryStream 전환 가능) — 소비자는 불투명 blob으로만 다룬다.
struct ValueBlob {
    std::string json;   // 리프 값의 직렬화 표현(v1: JSON 텍스트). 비어있으면 "값 없음".
    bool Empty() const { return json.empty(); }
};

// 커맨드 종류 태그 — dynamic_cast 금지 규약(07 §4)에 따라 TryMerge 등이 커맨드 종류를
//   RTTI 없이 판정하는 데 쓴다. 각 구현이 Kind()를 오버라이드해 자신의 태그를 반환한다.
enum class CommandKind : std::uint8_t {
    Custom = 0,       // 기본(플러그인 커맨드 등)
    PropertyEdit,
    CreateEntity,
    DestroyEntity,
    Reparent,
    AddComponent,
    RemoveComponent,
    Transaction,      // CommandStack이 트랜잭션을 하나로 묶는 복합 커맨드
};

// ---- 커맨드 인터페이스 ----
// 07 API 스케치와 동일. execute/undo는 EditorContext를 통해 activeWorld·리플렉션에 접근한다.
class IEditorCommand {
public:
    virtual ~IEditorCommand() = default;

    // 최초 push 시 execute가 호출되어 편집이 적용된다(패널은 값을 직접 쓰지 않는다).
    virtual void Execute(EditorContext& ctx) = 0;
    virtual void Undo(EditorContext& ctx) = 0;

    // History 패널·툴팁 표시용 라벨("Move Entity", "Add Component" 등).
    virtual std::string_view Label() const = 0;

    // 종류 태그(dynamic_cast 대체). 기본은 Custom — 병합·특수 처리 불필요한 커맨드용.
    virtual CommandKind Kind() const { return CommandKind::Custom; }

    // 드래그 병합(coalescing): 같은 (target,path)의 연속 편집을 하나로 합친다.
    //   true 반환 = next를 흡수함(next는 스택에 쌓이지 않음). 기본은 병합 안 함.
    virtual bool TryMerge(const IEditorCommand& /*next*/) { return false; }
};

using CommandPtr = std::unique_ptr<IEditorCommand>;

// ---- 리플렉션 제네릭 프로퍼티 커맨드 (04 TypeInfo/PropertyPath/직렬화 소비) ----
// 07 §4: 대부분의 편집이 이 하나로 처리된다. 인스펙터가 값 변경 시 이 커맨드를 발행한다.
//   Execute = newValue 적용, Undo = oldValue 적용. 둘 다 refl::WriteValue로 target에 쓴다.
class PropertyEditCommand final : public IEditorCommand {
public:
    PropertyEditCommand(ObjectRef target, refl::PropertyPath path,
                        ValueBlob oldValue, ValueBlob newValue,
                        std::string label = "Edit Property");

    void Execute(EditorContext& ctx) override;
    void Undo(EditorContext& ctx) override;
    std::string_view Label() const override { return m_label; }
    CommandKind Kind() const override { return CommandKind::PropertyEdit; }
    // 같은 (target,path)이고 병합 창(기본 400ms) 내면 newValue를 갱신하며 흡수.
    bool TryMerge(const IEditorCommand& next) override;

    const ObjectRef&          Target() const { return m_target; }
    const refl::PropertyPath& Path() const { return m_path; }

private:
    ObjectRef          m_target;
    refl::PropertyPath m_path;
    ValueBlob          m_oldValue;
    ValueBlob          m_newValue;
    std::string        m_label;
    std::chrono::steady_clock::time_point m_stamp;  // 병합 시간 창 판정
};

// ---- 구조 변경 커맨드 (07 §4) ----
// 엔티티 생성. Undo 시 생성된 엔티티를 파괴한다. 생성된 핸들은 Created()로 조회.
class CreateEntityCommand final : public IEditorCommand {
public:
    explicit CreateEntityCommand(ecs::Entity parent = ecs::Entity::Null(),
                                 std::string label = "Create Entity");
    void Execute(EditorContext& ctx) override;
    void Undo(EditorContext& ctx) override;
    std::string_view Label() const override { return m_label; }
    CommandKind Kind() const override { return CommandKind::CreateEntity; }
    ecs::Entity Created() const { return m_created; }

private:
    ecs::Entity m_parent;
    ecs::Entity m_created = ecs::Entity::Null();
    std::string m_label;
};

// 엔티티 삭제. Undo 복원을 위해 파괴 전 전체 서브트리를 blob으로 스냅샷한다(07 §4).
class DestroyEntityCommand final : public IEditorCommand {
public:
    explicit DestroyEntityCommand(ecs::Entity target, std::string label = "Delete Entity");
    void Execute(EditorContext& ctx) override;
    void Undo(EditorContext& ctx) override;   // 스냅샷 blob에서 서브트리 재구성
    std::string_view Label() const override { return m_label; }
    CommandKind Kind() const override { return CommandKind::DestroyEntity; }

private:
    ecs::Entity m_target;
    std::string m_subtreeSnapshot;   // 04 직렬화 서브트리 blob(JSON). Undo가 소비.
    std::string m_label;
};

// 재부모화. keepWorld=월드 트랜스폼 유지(03 ApplyReparent 옵션 인용).
class ReparentCommand final : public IEditorCommand {
public:
    ReparentCommand(ecs::Entity child, ecs::Entity newParent, bool keepWorld = true,
                    std::string label = "Reparent");
    void Execute(EditorContext& ctx) override;
    void Undo(EditorContext& ctx) override;
    std::string_view Label() const override { return m_label; }
    CommandKind Kind() const override { return CommandKind::Reparent; }

private:
    ecs::Entity m_child;
    ecs::Entity m_newParent;
    ecs::Entity m_oldParent = ecs::Entity::Null();  // Execute가 기록(Undo 복원용)
    bool        m_keepWorld = true;
    std::string m_label;
};

// 컴포넌트 추가. Undo 시 제거. componentType은 리플렉션 등록 타입(플러그인 포함).
class AddComponentCommand final : public IEditorCommand {
public:
    AddComponentCommand(ecs::Entity target, const refl::TypeInfo& componentType,
                        std::string label = "Add Component");
    void Execute(EditorContext& ctx) override;
    void Undo(EditorContext& ctx) override;
    std::string_view Label() const override { return m_label; }
    CommandKind Kind() const override { return CommandKind::AddComponent; }

private:
    ecs::Entity           m_target;
    const refl::TypeInfo* m_type = nullptr;
    std::string           m_label;
};

// 컴포넌트 제거. Undo 복원을 위해 제거 전 컴포넌트 값을 blob으로 보관.
class RemoveComponentCommand final : public IEditorCommand {
public:
    RemoveComponentCommand(ecs::Entity target, const refl::TypeInfo& componentType,
                           std::string label = "Remove Component");
    void Execute(EditorContext& ctx) override;
    void Undo(EditorContext& ctx) override;
    std::string_view Label() const override { return m_label; }
    CommandKind Kind() const override { return CommandKind::RemoveComponent; }

private:
    ecs::Entity           m_target;
    const refl::TypeInfo* m_type = nullptr;
    std::string           m_valueSnapshot;   // 제거 전 컴포넌트 값 blob(Undo 복원)
    std::string           m_label;
};

} // namespace mye::editor
