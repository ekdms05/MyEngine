// Prefab.cpp — 프리팹 캡처·인스턴스화·오버라이드 (docs/07 §3, 03 프리팹)
//
// 07 §3: PrefabAsset은 SceneSerializer 서브트리 포맷으로 캡처된다. 인스턴스화 = 캡처 JSON을
//   World에 ReadInto + PropertyPath 오버라이드 적용. v1 오버라이드는 리프 값 치환(리플렉션
//   PropertyPath 기반) — 인스턴스화 전에 캡처 JSON을 패치하는 방식으로 적용한다(로컬 ID 매핑을
//   ReadInto 밖으로 노출하지 않아도 되도록).
#include "mye/editor/Prefab.h"
#include "mye/editor/SceneSerializer.h"

#include "mye/ecs/World.h"
#include "mye/refl/PropertyPath.h"
#include "mye/refl/TypeInfo.h"
#include "mye/refl/TypeRegistry.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>

namespace mye::editor {

namespace {

// 컴포넌트 json(오브젝트) 안의 PropertyPath 리프에 valueJson(파싱된 json)을 써넣는다.
//   경로 첫 세그먼트는 필드/인덱스 규약(04 PropertyPath)을 그대로 따른다.
bool ApplyPathToJson(json::Value& node, const refl::PropertyPath& path, std::size_t segIdx,
                     const json::Value& newValue) {
    if (segIdx >= path.segments.size()) {
        node = newValue;
        return true;
    }
    const refl::PathSegment& seg = path.segments[segIdx];
    if (seg.IsField()) {
        if (!node.IsObject()) return false;
        const std::string key = std::get<std::string>(seg.value);
        json::Value::Object obj = node.AsObject();
        auto it = obj.find(key);
        if (it == obj.end()) return false;
        if (!ApplyPathToJson(it->second, path, segIdx + 1, newValue)) return false;
        node = json::Value(std::move(obj));
        return true;
    }
    // 인덱스.
    if (!node.IsArray()) return false;
    const std::size_t idx = std::get<std::size_t>(seg.value);
    json::Value::Array arr = node.AsArray();
    if (idx >= arr.size()) return false;
    if (!ApplyPathToJson(arr[idx], path, segIdx + 1, newValue)) return false;
    node = json::Value(std::move(arr));
    return true;
}

// 한 오버라이드를 캡처 JSON에 적용. path 첫 세그먼트 = 컴포넌트 타입 이름, 나머지 = 컴포넌트 내부.
//   예: "mye::scene::LocalTransform.position.x". 대상 엔티티는 targetLocalId.
bool ApplyOverride(json::Value::Array& entities, const PrefabOverride& ov) {
    if (ov.path.segments.empty() || !ov.path.segments.front().IsField()) return false;
    const std::string compName = std::get<std::string>(ov.path.segments.front().value);

    auto parsedVal = json::Parse(ov.valueJson);
    if (!parsedVal) return false;

    // 컴포넌트 내부 경로(첫 세그먼트 제거).
    refl::PropertyPath inner;
    inner.segments.assign(ov.path.segments.begin() + 1, ov.path.segments.end());

    for (json::Value& ev : entities) {
        if (!ev.IsObject()) continue;
        const json::Value* idv = ev.Find("id");
        if (!idv || static_cast<std::uint32_t>(idv->AsInt()) != ov.targetLocalId) continue;

        json::Value::Object entObj = ev.AsObject();
        auto compsIt = entObj.find("components");
        if (compsIt == entObj.end() || !compsIt->second.IsObject()) return false;
        json::Value::Object comps = compsIt->second.AsObject();
        auto cIt = comps.find(compName);
        if (cIt == comps.end()) return false;

        if (inner.segments.empty()) {
            cIt->second = parsedVal.Value();
        } else if (!ApplyPathToJson(cIt->second, inner, 0, parsedVal.Value())) {
            return false;
        }
        compsIt->second = json::Value(std::move(comps));
        ev = json::Value(std::move(entObj));
        return true;
    }
    return false;
}

} // namespace

Expected<PrefabAsset, Error>
PrefabAsset::CaptureFrom(const ecs::World& world, ecs::Entity root) {
    SceneSerializer ser;
    auto sub = ser.WriteSubtree(world, root);
    if (!sub) return sub.GetError();
    PrefabAsset asset;
    asset.SetRoot(std::move(sub.Value()));
    return asset;
}

Expected<ecs::Entity, Error>
PrefabAsset::Instantiate(ecs::World& world, std::span<const PrefabOverride> overrides) const {
    // 오버라이드 적용을 위해 캡처 JSON을 복제·패치한 뒤 ReadInto.
    if (!m_root.IsObject()) return Error{"Prefab::Instantiate: empty prefab", 1};
    json::Value::Object rootObj = m_root.AsObject();
    auto entsIt = rootObj.find("entities");
    if (entsIt == rootObj.end() || !entsIt->second.IsArray())
        return Error{"Prefab::Instantiate: malformed prefab (no entities)", 2};

    json::Value::Array entities = entsIt->second.AsArray();
    for (const PrefabOverride& ov : overrides)
        ApplyOverride(entities, ov);   // 실패는 무시(견고성 — 경로/타입 불일치 오버라이드 스킵)
    entsIt->second = json::Value(std::move(entities));
    json::Value patched(std::move(rootObj));

    SceneSerializer ser;
    auto roots = ser.ReadInto(world, patched);
    if (!roots) return roots.GetError();
    if (roots.Value().empty()) return ecs::Entity::Null();
    return roots.Value().front();
}

Expected<void, Error> PrefabAsset::SaveToFile(std::string_view path) const {
    const std::string text = json::Stringify(m_root);
    std::ofstream out(std::string(path), std::ios::binary | std::ios::trunc);
    if (!out) return Error{"Prefab::SaveToFile: cannot open '" + std::string(path) + "'", 3};
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) return Error{"Prefab::SaveToFile: write failed", 4};
    return {};
}

Expected<PrefabAsset, Error> PrefabAsset::LoadFromFile(std::string_view path) {
    std::ifstream in(std::string(path), std::ios::binary);
    if (!in) return Error{"Prefab::LoadFromFile: cannot open '" + std::string(path) + "'", 3};
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto parsed = json::Parse(text);
    if (!parsed) return parsed.GetError();
    PrefabAsset asset;
    asset.SetRoot(std::move(parsed.Value()));
    return asset;
}

} // namespace mye::editor
