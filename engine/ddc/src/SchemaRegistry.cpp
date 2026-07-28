// mye/ddc/SchemaRegistry.cpp — 스키마 레지스트리 구현 (SchemaRegistry.h 참조)
#include "mye/ddc/SchemaRegistry.h"

#include "mye/core/Json.h"

namespace mye::ddc {

Expected<const ComponentSchema*, Error> SchemaRegistry::Register(const ComponentSchema& schema) {
    if (schema.Name().empty()) return Error{"SchemaRegistry::Register: 이름이 비었습니다", 1};
    if (Find(schema.Id())) return Error{"SchemaRegistry::Register: 이미 존재하는 스키마 '" + schema.Name() + "'", 2};
    m_schemas.push_back(std::make_unique<ComponentSchema>(schema));
    return m_schemas.back().get();
}

const ComponentSchema* SchemaRegistry::Find(std::string_view name) const {
    for (const auto& s : m_schemas) if (s->Name() == name) return s.get();
    return nullptr;
}

const ComponentSchema* SchemaRegistry::Find(SchemaId id) const {
    for (const auto& s : m_schemas) if (s->Id() == id) return s.get();
    return nullptr;
}

Expected<int, Error> SchemaRegistry::LoadFromJson(const json::Value& arr) {
    if (!arr.IsArray()) return Error{"SchemaRegistry::LoadFromJson: array 아님", 1};
    int count = 0;
    for (const json::Value& v : arr.AsArray()) {
        auto s = ComponentSchema::FromJson(v);
        if (!s) return s.GetError();
        auto r = Register(s.Value());
        if (!r) return r.GetError();
        ++count;
    }
    return count;
}

DynamicComponent SchemaRegistry::Instantiate(std::string_view name) const {
    const ComponentSchema* s = Find(name);
    return s ? s->Instantiate() : DynamicComponent{};
}

} // namespace mye::ddc
