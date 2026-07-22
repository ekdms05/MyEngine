// mye/editor/Prefab.h — 프리팹 에셋·인스턴스화·오버라이드 (docs/07 §3, 인스펙터 프리팹 상태)
//
// 07: 프리팹(PrefabAsset·인스턴스화·오버라이드는 PropertyPath 기반). M4-A v1은 오버라이드 최소
//   — 인스턴스화 + PropertyPath 오버라이드 목록의 데이터 계약까지. Apply/Revert UI 고도화는 P2.
//
// 프리팹 = 하나의 엔티티 서브트리를 SceneSerializer 포맷으로 캡처한 에셋. 인스턴스화는 캡처된
//   서브트리를 World에 ReadInto한 뒤 오버라이드를 적용한다.
#pragma once

#include "mye/core/Base.h"
#include "mye/core/Json.h"
#include "mye/ecs/Entity.h"
#include "mye/refl/PropertyPath.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mye::ecs { class World; }

namespace mye::editor {

// 인스턴스별 오버라이드 하나(07: PropertyPath 기반). targetLocalId=프리팹 내부 로컬 엔티티 ID.
struct PrefabOverride {
    std::uint32_t      targetLocalId = 0;   // 프리팹 서브트리 내 안정 로컬 ID
    refl::PropertyPath path;                // 컴포넌트 경로(예: "Transform.position.x")
    std::string        valueJson;           // 오버라이드 값(직렬화 표현)
};

// 프리팹 에셋 — 캡처된 서브트리(SceneSerializer JSON) + 메타.
class PrefabAsset {
public:
    PrefabAsset() = default;

    // 캡처된 서브트리 JSON(SceneSerializer::WriteSubtree 산출물). 정본 데이터.
    const json::Value& Root() const { return m_root; }
    void SetRoot(json::Value root) { m_root = std::move(root); }

    // ---- 캡처·인스턴스화 ----
    // World의 root 서브트리를 프리팹으로 캡처.
    static Expected<PrefabAsset, Error> CaptureFrom(const ecs::World& world, ecs::Entity root);

    // World에 인스턴스화(오버라이드 적용). 생성된 인스턴스 루트 엔티티 반환.
    Expected<ecs::Entity, Error>
    Instantiate(ecs::World& world, std::span<const PrefabOverride> overrides = {}) const;

    // ---- 파일 I/O ----
    Expected<void, Error>        SaveToFile(std::string_view path) const;
    static Expected<PrefabAsset, Error> LoadFromFile(std::string_view path);

private:
    json::Value m_root;   // 캡처 서브트리
};

} // namespace mye::editor
