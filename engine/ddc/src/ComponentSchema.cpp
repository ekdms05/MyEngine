// mye/ddc/ComponentSchema.cpp — 데이터드리븐 컴포넌트 구현 (ComponentSchema.h 참조)
#include "mye/ddc/ComponentSchema.h"

#include "mye/core/Json.h"

namespace mye::ddc {

std::string_view FieldTypeName(FieldType t) {
    switch (t) {
        case FieldType::I32:    return "i32";
        case FieldType::I64:    return "i64";
        case FieldType::F32:    return "f32";
        case FieldType::F64:    return "f64";
        case FieldType::Bool:   return "bool";
        case FieldType::String: return "string";
        default:                return "i32";
    }
}

bool FieldTypeFromName(std::string_view n, FieldType& out) {
    if (n == "i32")    { out = FieldType::I32;    return true; }
    if (n == "i64")    { out = FieldType::I64;    return true; }
    if (n == "f32")    { out = FieldType::F32;    return true; }
    if (n == "f64")    { out = FieldType::F64;    return true; }
    if (n == "bool")   { out = FieldType::Bool;   return true; }
    if (n == "string") { out = FieldType::String; return true; }
    return false;
}

namespace {
bool IsIntKind(FieldType t)   { return t == FieldType::I32 || t == FieldType::I64 || t == FieldType::Bool; }
bool IsFloatKind(FieldType t) { return t == FieldType::F32 || t == FieldType::F64; }
}

// ---- DynamicComponent ----
bool DynamicComponent::Has(std::string_view field) const {
    return m_schema && m_schema->FieldIndex(field) >= 0;
}

bool DynamicComponent::SetInt(std::string_view field, int64_t v) {
    if (!m_schema) return false;
    const int idx = m_schema->FieldIndex(field);
    if (idx < 0) return false;
    const FieldType ft = m_schema->Fields()[idx].type;
    if (IsIntKind(ft))        m_values[idx].i = v;
    else if (IsFloatKind(ft)) m_values[idx].d = static_cast<double>(v);
    else return false;   // string
    return true;
}
int64_t DynamicComponent::GetInt(std::string_view field, int64_t fallback) const {
    if (!m_schema) return fallback;
    const int idx = m_schema->FieldIndex(field);
    if (idx < 0) return fallback;
    const FieldType ft = m_schema->Fields()[idx].type;
    if (IsIntKind(ft))   return m_values[idx].i;
    if (IsFloatKind(ft)) return static_cast<int64_t>(m_values[idx].d);
    return fallback;
}

bool DynamicComponent::SetFloat(std::string_view field, double v) {
    if (!m_schema) return false;
    const int idx = m_schema->FieldIndex(field);
    if (idx < 0) return false;
    const FieldType ft = m_schema->Fields()[idx].type;
    if (IsFloatKind(ft))    m_values[idx].d = v;
    else if (IsIntKind(ft)) m_values[idx].i = static_cast<int64_t>(v);
    else return false;
    return true;
}
double DynamicComponent::GetFloat(std::string_view field, double fallback) const {
    if (!m_schema) return fallback;
    const int idx = m_schema->FieldIndex(field);
    if (idx < 0) return fallback;
    const FieldType ft = m_schema->Fields()[idx].type;
    if (IsFloatKind(ft)) return m_values[idx].d;
    if (IsIntKind(ft))   return static_cast<double>(m_values[idx].i);
    return fallback;
}

bool DynamicComponent::SetBool(std::string_view field, bool v) {
    if (!m_schema) return false;
    const int idx = m_schema->FieldIndex(field);
    if (idx < 0 || m_schema->Fields()[idx].type != FieldType::Bool) return false;
    m_values[idx].i = v ? 1 : 0;
    return true;
}
bool DynamicComponent::GetBool(std::string_view field, bool fallback) const {
    if (!m_schema) return fallback;
    const int idx = m_schema->FieldIndex(field);
    if (idx < 0 || m_schema->Fields()[idx].type != FieldType::Bool) return fallback;
    return m_values[idx].i != 0;
}

bool DynamicComponent::SetString(std::string_view field, std::string_view v) {
    if (!m_schema) return false;
    const int idx = m_schema->FieldIndex(field);
    if (idx < 0 || m_schema->Fields()[idx].type != FieldType::String) return false;
    m_values[idx].s.assign(v.begin(), v.end());
    return true;
}
std::string DynamicComponent::GetString(std::string_view field, std::string_view fallback) const {
    if (!m_schema) return std::string(fallback);
    const int idx = m_schema->FieldIndex(field);
    if (idx < 0 || m_schema->Fields()[idx].type != FieldType::String) return std::string(fallback);
    return m_values[idx].s;
}

json::Value DynamicComponent::ToJson() const {
    if (!m_schema) return json::Value();
    json::Value::Object o;
    for (const FieldDef& f : m_schema->Fields()) {
        if (f.type == FieldType::String)      o[f.name] = json::Value(GetString(f.name));
        else if (IsFloatKind(f.type))         o[f.name] = json::Value(GetFloat(f.name));
        else if (f.type == FieldType::Bool)   o[f.name] = json::Value(GetBool(f.name));
        else                                  o[f.name] = json::Value(static_cast<std::int64_t>(GetInt(f.name)));
    }
    return json::Value(std::move(o));
}

void DynamicComponent::ApplyJson(const json::Value& obj) {
    if (!m_schema || !obj.IsObject()) return;
    for (const FieldDef& f : m_schema->Fields()) {
        const json::Value* v = obj.Find(f.name);
        if (!v) continue;
        if (f.type == FieldType::String)      SetString(f.name, v->AsString());
        else if (IsFloatKind(f.type))         SetFloat(f.name, v->AsDouble());
        else if (f.type == FieldType::Bool)   SetBool(f.name, v->AsBool());
        else                                  SetInt(f.name, v->AsInt());
    }
}

// ---- ComponentSchema ----
ComponentSchema::ComponentSchema(std::string name)
    : m_name(std::move(name)), m_id(HashFnv1a64(m_name)) {}

ComponentSchema& ComponentSchema::AddField(const FieldDef& f) {
    if (FieldIndex(f.name) < 0) m_fields.push_back(f);
    return *this;
}

int ComponentSchema::FieldIndex(std::string_view name) const {
    for (size_t i = 0; i < m_fields.size(); ++i)
        if (m_fields[i].name == name) return static_cast<int>(i);
    return -1;
}

const FieldDef* ComponentSchema::FindField(std::string_view name) const {
    const int idx = FieldIndex(name);
    return idx < 0 ? nullptr : &m_fields[idx];
}

DynamicComponent ComponentSchema::Instantiate() const {
    DynamicComponent c;
    c.m_schema = this;
    c.m_values.resize(m_fields.size());
    for (size_t i = 0; i < m_fields.size(); ++i) {
        const FieldDef& f = m_fields[i];
        if (f.type == FieldType::String)      c.m_values[i].s = f.defS;
        else if (IsFloatKind(f.type))         c.m_values[i].d = f.defD;
        else                                  c.m_values[i].i = f.defI;   // int/bool
    }
    return c;
}

Expected<ComponentSchema, Error> ComponentSchema::FromJson(const json::Value& v) {
    if (!v.IsObject()) return Error{"ComponentSchema::FromJson: object 아님", 1};
    const json::Value* nameV = v.Find("name");
    if (!nameV || !nameV->IsString()) return Error{"ComponentSchema::FromJson: name 없음", 2};
    ComponentSchema s{ std::string(nameV->AsString()) };

    const json::Value* fields = v.Find("fields");
    if (fields && fields->IsArray()) {
        for (const json::Value& fv : fields->AsArray()) {
            if (!fv.IsObject()) continue;
            const json::Value* fn = fv.Find("name");
            const json::Value* ft = fv.Find("type");
            if (!fn || !fn->IsString() || !ft || !ft->IsString()) continue;
            FieldDef def;
            def.name = std::string(fn->AsString());
            if (!FieldTypeFromName(ft->AsString(), def.type))
                return Error{"ComponentSchema::FromJson: 알 수 없는 필드 타입 '" + std::string(ft->AsString()) + "'", 3};
            if (const json::Value* d = fv.Find("default")) {
                if (def.type == FieldType::String)      def.defS = std::string(d->AsString());
                else if (IsFloatKind(def.type))         def.defD = d->AsDouble();
                else if (def.type == FieldType::Bool)   def.defI = d->AsBool() ? 1 : 0;
                else                                    def.defI = d->AsInt();
            }
            s.AddField(def);
        }
    }
    return s;
}

json::Value ComponentSchema::ToJson() const {
    json::Value::Array fields;
    for (const FieldDef& f : m_fields) {
        json::Value::Object o;
        o["name"] = json::Value(f.name);
        o["type"] = json::Value(std::string(FieldTypeName(f.type)));
        if (f.type == FieldType::String)      o["default"] = json::Value(f.defS);
        else if (IsFloatKind(f.type))         o["default"] = json::Value(f.defD);
        else if (f.type == FieldType::Bool)   o["default"] = json::Value(f.defI != 0);
        else                                  o["default"] = json::Value(static_cast<std::int64_t>(f.defI));
        fields.push_back(json::Value(std::move(o)));
    }
    json::Value::Object root;
    root["name"] = json::Value(m_name);
    root["fields"] = json::Value(std::move(fields));
    return json::Value(std::move(root));
}

} // namespace mye::ddc
