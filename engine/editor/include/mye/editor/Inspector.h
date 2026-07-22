// mye/editor/Inspector.h — 리플렉션 자동 인스펙터 + 커스텀 드로어 훅 (docs/07 인스펙터)
//
// 07 인스펙터: refl::TypeInfo 순회로 컴포넌트 UI 자동 생성(기본형·enum·Vec·중첩·AssetRef).
//   편집 시 PropertyEditCommand 발행(패널이 값을 직접 쓰지 않는다). 커스텀 드로어(타입 단위
//   IPropertyDrawer, 컴포넌트 단위 IComponentEditor)를 EditorExtensionRegistry에 등록 —
//   내장 드로어와 플러그인 드로어가 동일 메커니즘. UI 힌트 어트리뷰트(Range/Tooltip 등)는 04 소유.
#pragma once

#include "mye/editor/EditorTypes.h"
#include "mye/refl/PropertyPath.h"

#include <memory>
#include <string>

namespace mye::refl { class FieldInfo; }

namespace mye::editor {

// 한 프로퍼티(리프/중첩)를 그릴 때 드로어가 받는 컨텍스트.
struct PropertyDrawContext {
    EditorContext*        editor = nullptr;
    ObjectRef             target;               // 편집 대상(커맨드 발행에 사용)
    refl::PropertyPath    path;                 // 루트 컴포넌트로부터의 경로
    const refl::TypeInfo* type = nullptr;       // 이 프로퍼티 값의 타입
    const refl::FieldInfo* field = nullptr;     // 리프 필드 메타(어트리뷰트 조회). 원소면 null
    void*                 instance = nullptr;   // 값 포인터(읽어서 위젯 초기화)
    std::string           label;                // 표시 라벨(필드 이름/한국어)
};

// 타입 단위 커스텀 드로어(07 §확장 3, 예: Curve 편집기).
//   OnGui는 값이 변경되면 true 반환 — PropertyEditCommand는 프레임워크가 발행한다.
class IPropertyDrawer {
public:
    virtual ~IPropertyDrawer() = default;
    virtual bool OnGui(PropertyDrawContext& ctx) = 0;
};

// 컴포넌트 단위 에디터(컴포넌트 전체 UI 교체, 예: 타일맵의 "Edit Tilemap" 버튼).
struct ComponentEditContext {
    EditorContext*        editor = nullptr;
    ecs::Entity           entity{};
    const refl::TypeInfo* componentType = nullptr;
    void*                 component = nullptr;   // 컴포넌트 인스턴스 포인터
};
class IComponentEditor {
public:
    virtual ~IComponentEditor() = default;
    virtual void OnGui(ComponentEditContext& ctx) = 0;
};

// 리플렉션 자동 인스펙터 렌더러. 선택 엔티티의 컴포넌트를 열거해 UI를 자동 생성한다.
//   커스텀 드로어/에디터가 등록돼 있으면 우선 사용(EditorExtensionRegistry 조회).
class InspectorRenderer {
public:
    InspectorRenderer();
    ~InspectorRenderer();

    // 엔티티 하나의 전체 컴포넌트 UI(헤더 접기·[+ Component]·컴포넌트 우클릭 메뉴).
    void DrawEntity(EditorContext& ctx, ecs::Entity entity);

    // 임의 리플렉션 인스턴스 하나(컴포넌트/중첩 struct)를 필드 순회로 그린다.
    //   내부 재귀 진입점 — struct 필드·enum·vector·AssetRef·중첩을 위젯에 매핑한다.
    //   변경 발생 시 PropertyEditCommand를 ctx.commands에 발행한다.
    void DrawReflected(EditorContext& ctx, const ObjectRef& target,
                       const refl::TypeInfo& type, void* instance,
                       const refl::PropertyPath& basePath);

    // 디버그 모드(07): 어트리뷰트 무시하고 원시 리플렉션 값 전부 표시.
    void SetDebugMode(bool on) { m_debugMode = on; }
    bool DebugMode() const { return m_debugMode; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_debugMode = false;
};

} // namespace mye::editor
