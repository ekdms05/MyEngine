// mye/ddc/DynamicComponentStore.cpp — 엔티티↔동적 컴포넌트 스토어 구현 (헤더 참조)
#include "mye/ddc/DynamicComponentStore.h"

#include "mye/core/Json.h"

namespace mye::ddc {

DynamicComponent* DynamicComponentStore::Add(EntityId e, std::string_view schemaName, const SchemaRegistry& reg) {
    const ComponentSchema* s = reg.Find(schemaName);
    if (!s) return nullptr;
    auto& comps = m_entities[e];
    auto it = comps.find(s->Id());
    if (it != comps.end()) return &it->second;   // 이미 있음
    auto [ins, ok] = comps.emplace(s->Id(), s->Instantiate());
    return &ins->second;
}

DynamicComponent* DynamicComponentStore::Get(EntityId e, std::string_view schemaName) {
    auto ei = m_entities.find(e);
    if (ei == m_entities.end()) return nullptr;
    const SchemaId id = HashFnv1a64(schemaName);
    auto ci = ei->second.find(id);
    return ci == ei->second.end() ? nullptr : &ci->second;
}

const DynamicComponent* DynamicComponentStore::Get(EntityId e, std::string_view schemaName) const {
    auto ei = m_entities.find(e);
    if (ei == m_entities.end()) return nullptr;
    const SchemaId id = HashFnv1a64(schemaName);
    auto ci = ei->second.find(id);
    return ci == ei->second.end() ? nullptr : &ci->second;
}

bool DynamicComponentStore::Has(EntityId e, std::string_view schemaName) const {
    return Get(e, schemaName) != nullptr;
}

bool DynamicComponentStore::Remove(EntityId e, std::string_view schemaName) {
    auto ei = m_entities.find(e);
    if (ei == m_entities.end()) return false;
    const bool erased = ei->second.erase(HashFnv1a64(schemaName)) > 0;
    if (ei->second.empty()) m_entities.erase(ei);
    return erased;
}

void DynamicComponentStore::RemoveEntity(EntityId e) {
    m_entities.erase(e);
}

size_t DynamicComponentStore::ComponentCount() const {
    size_t n = 0;
    for (const auto& [e, comps] : m_entities) n += comps.size();
    return n;
}

void DynamicComponentStore::ForEach(std::string_view schemaName,
                                    const std::function<void(EntityId, DynamicComponent&)>& fn) {
    const SchemaId id = HashFnv1a64(schemaName);
    for (auto& [e, comps] : m_entities) {
        auto ci = comps.find(id);
        if (ci != comps.end()) fn(e, ci->second);
    }
}

json::Value DynamicComponentStore::ToJson() const {
    json::Value::Array arr;
    for (const auto& [e, comps] : m_entities) {
        for (const auto& [sid, comp] : comps) {
            const ComponentSchema* s = comp.Schema();
            if (!s) continue;
            json::Value::Object o;
            o["e"]      = json::Value(static_cast<std::int64_t>(e));
            o["schema"] = json::Value(s->Name());
            o["data"]   = comp.ToJson();
            arr.push_back(json::Value(std::move(o)));
        }
    }
    return json::Value(std::move(arr));
}

Expected<int, Error> DynamicComponentStore::FromJson(const json::Value& arr, const SchemaRegistry& reg) {
    if (!arr.IsArray()) return Error{"DynamicComponentStore::FromJson: array 아님", 1};
    int count = 0;
    for (const json::Value& v : arr.AsArray()) {
        if (!v.IsObject()) continue;
        const json::Value* eV = v.Find("e");
        const json::Value* sV = v.Find("schema");
        if (!eV || !sV || !sV->IsString()) continue;
        const EntityId e = static_cast<EntityId>(eV->AsInt());
        DynamicComponent* c = Add(e, sV->AsString(), reg);
        if (!c) continue;   // 스키마 미등록 → 건너뜀
        if (const json::Value* d = v.Find("data")) c->ApplyJson(*d);
        ++count;
    }
    return count;
}

} // namespace mye::ddc
