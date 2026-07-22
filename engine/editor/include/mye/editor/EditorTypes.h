// mye/editor/EditorTypes.h — 에디터 공용 값 타입·전방선언 (docs/07)
//
// mye_editor(L5) 전역에서 공유하는 경량 값 타입과 컨텍스트 전방선언을 모은다.
// 다른 에디터 헤더가 순환 없이 서로를 참조할 수 있도록 하는 최소 공통 기반이다.
//
// 소비 규약: 좌표계·PPU는 02, 리플렉션·직렬화·PropertyPath는 04, 이벤트·라이프사이클은
//   01이 확정한 것을 그대로 인용한다. 이 헤더는 새 도메인 개념을 정의하지 않고 식별자만 정의.
#pragma once

#include "mye/core/Base.h"
#include "mye/ecs/Entity.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace mye {
class EventBus;
class EngineContext;
}
namespace mye::ecs { class World; }
namespace mye::refl { class TypeInfo; }

namespace mye::editor {

// ---- 전방선언 (에디터 상호 참조 최소 결합) ----
class EditorApp;
class PanelManager;
class SelectionManager;
class CommandStack;
class PlayModeController;
class EditorExtensionRegistry;
class ProjectContext;
class Document;
struct EditorContext;

// ---- 패널·문서 식별자 ----
// 패널 종류 ID(문자열, 예: "mye.hierarchy"). 팩토리 등록 키 = Window 메뉴 자동 항목 키.
using PanelTypeId = std::string;
// 열린 패널 인스턴스 하나의 런타임 핸들(다중 인스턴스 패널 구분).
struct PanelInstanceId {
    std::uint64_t value = 0;
    bool IsValid() const { return value != 0; }
    auto operator<=>(const PanelInstanceId&) const = default;
};
// 문서(열린 씬/에셋) 하나의 런타임 핸들. 문서별 CommandStack·dirty가 여기에 매인다.
struct DocumentId {
    std::uint64_t value = 0;
    bool IsValid() const { return value != 0; }
    auto operator<=>(const DocumentId&) const = default;
};

// ---- 선택 대상(이종) ----
// 07 §5: 선택은 엔티티만이 아니라 에셋·타일 셀·애니키 등 이종. kind로 분기한다.
enum class SelectableKind : std::uint8_t {
    None = 0,
    Entity,      // id = Entity::Packed()
    Asset,       // id = AssetGuid 하위 해시(04 GUID 안정 참조는 후속 확장)
    TileRegion,  // id = 타일맵 편집 영역 핸들(P1)
    AnimKey,     // id = 애니메이션 키프레임 핸들(P1.5)
};

// 이종 선택 참조 — SelectionManager의 원소.
struct SelectableRef {
    SelectableKind kind = SelectableKind::None;
    std::uint64_t  id = 0;
    auto operator<=>(const SelectableRef&) const = default;

    static SelectableRef OfEntity(ecs::Entity e) {
        return SelectableRef{SelectableKind::Entity, e.Packed()};
    }
    ecs::Entity AsEntity() const { return ecs::Entity::FromPacked(id); }
    bool IsEntity() const { return kind == SelectableKind::Entity; }
};

// 선택 모드(07 §5: 다중 선택).
enum class SelectMode : std::uint8_t {
    Replace,  // 기존 선택 해제 후 교체
    Add,      // 기존 선택에 추가(Shift 범위/추가)
    Toggle,   // 개별 토글(Ctrl)
};

// ---- 리플렉션 대상 참조 (PropertyEditCommand·인스펙터가 쓰는 "무엇을 편집하나") ----
// 07 §4: PropertyEditCommand{ targetId, propertyPath, ... }의 target. v1은 엔티티+컴포넌트
//   타입으로 리플렉션 인스턴스를 해석한다(에셋 문서·서브에셋은 후속 kind로 확장).
struct ObjectRef {
    SelectableKind        kind = SelectableKind::Entity;
    std::uint64_t         id = 0;             // Entity::Packed() 등
    const refl::TypeInfo* componentType = nullptr;  // 엔티티 컴포넌트 인스턴스일 때

    static ObjectRef Component(ecs::Entity e, const refl::TypeInfo& comp) {
        return ObjectRef{SelectableKind::Entity, e.Packed(), &comp};
    }
    ecs::Entity Entity() const { return ecs::Entity::FromPacked(id); }
    auto operator<=>(const ObjectRef&) const = default;
};

} // namespace mye::editor
