// mye/persist/CharacterStore.cpp — 캐릭터 영속 구현 (CharacterStore.h 참조)
#include "mye/persist/CharacterStore.h"

#include "JsonFile.h"
#include <cmath>
#include <limits>

namespace mye::persist {

Expected<CharacterId, Error> CharacterStore::Create(AccountId accountId, std::string_view name) {
    if (accountId == 0 || accountId >= INT64_MAX || m_nextId >= INT64_MAX)
        return Error{"Create: identity out of range", 1};
    if (name.empty() || name.size() > 64) return Error{"Create: invalid character name", 2};
    const std::string cname(name);
    if (m_byName.find(cname) != m_byName.end())
        return Error{"Create: 이미 존재하는 캐릭터명 '" + cname + "'", 3};

    CharacterRecord rec;
    rec.id = m_nextId++;
    rec.accountId = accountId;
    rec.name = cname;
    m_byName.emplace(cname, rec.id);
    m_chars.push_back(std::move(rec));
    return m_chars.back().id;
}

const CharacterRecord* CharacterStore::Get(CharacterId id) const {
    for (const CharacterRecord& c : m_chars) if (c.id == id) return &c;
    return nullptr;
}

CharacterRecord* CharacterStore::GetMutable(CharacterId id) {
    for (CharacterRecord& c : m_chars) if (c.id == id) return &c;
    return nullptr;
}

const CharacterRecord* CharacterStore::FindByName(std::string_view name) const {
    auto it = m_byName.find(std::string(name));
    return it == m_byName.end() ? nullptr : Get(it->second);
}

std::vector<CharacterId> CharacterStore::ListByAccount(AccountId accountId) const {
    std::vector<CharacterId> out;
    for (const CharacterRecord& c : m_chars)
        if (c.accountId == accountId) out.push_back(c.id);
    return out;
}

Expected<void, Error> CharacterStore::Upsert(const CharacterRecord& rec) {
    if (rec.id == 0) return Error{"Upsert: id=0 은 허용되지 않음(Create 사용)", 1};
    if (CharacterRecord* existing = GetMutable(rec.id)) {
        // 이름 변경 시 인덱스 조정(중복 방지).
        if (existing->name != rec.name) {
            if (!rec.name.empty() && m_byName.find(rec.name) != m_byName.end())
                return Error{"Upsert: 캐릭터명 충돌 '" + rec.name + "'", 2};
            m_byName.erase(existing->name);
            if (!rec.name.empty()) m_byName.emplace(rec.name, rec.id);
        }
        *existing = rec;
    } else {
        if (!rec.name.empty()) {
            if (m_byName.find(rec.name) != m_byName.end())
                return Error{"Upsert: 캐릭터명 충돌 '" + rec.name + "'", 2};
            m_byName.emplace(rec.name, rec.id);
        }
        m_chars.push_back(rec);
        if (rec.id >= m_nextId) m_nextId = rec.id + 1;
    }
    return {};
}

bool CharacterStore::Delete(CharacterId id) {
    for (auto it = m_chars.begin(); it != m_chars.end(); ++it) {
        if (it->id == id) {
            m_byName.erase(it->name);
            m_chars.erase(it);
            return true;
        }
    }
    return false;
}

void CharacterStore::RebuildIndex() {
    m_byName.clear();
    for (const CharacterRecord& c : m_chars)
        if (!c.name.empty()) m_byName.emplace(c.name, c.id);
}

json::Value CharacterStore::ToJson() const {
    json::Value::Array arr;
    for (const CharacterRecord& c : m_chars) {
        json::Value::Object o;
        o["id"]        = json::Value(static_cast<std::int64_t>(c.id));
        o["accountId"] = json::Value(static_cast<std::int64_t>(c.accountId));
        o["name"]      = json::Value(c.name);
        o["sceneId"]   = json::Value(c.sceneId);
        o["posX"]      = json::Value(static_cast<double>(c.posX));
        o["posY"]      = json::Value(static_cast<double>(c.posY));
        o["level"]     = json::Value(static_cast<std::int64_t>(c.level));
        o["xp"]        = json::Value(static_cast<std::int64_t>(c.xp));
        o["str"]       = json::Value(static_cast<std::int64_t>(c.strength));
        o["agi"]       = json::Value(static_cast<std::int64_t>(c.agility));
        o["int"]       = json::Value(static_cast<std::int64_t>(c.intellect));
        o["vit"]       = json::Value(static_cast<std::int64_t>(c.vitality));
        o["hp"]        = json::Value(static_cast<std::int64_t>(c.hp));
        o["mp"]        = json::Value(static_cast<std::int64_t>(c.mp));
        o["gold"]      = json::Value(static_cast<std::int64_t>(c.gold));

        json::Value::Array items;
        for (const ItemStackRecord& s : c.items) {
            json::Value::Object so;
            so["itemId"] = json::Value(static_cast<std::int64_t>(s.itemId));
            so["count"]  = json::Value(static_cast<std::int64_t>(s.count));
            items.push_back(json::Value(std::move(so)));
        }
        o["items"] = json::Value(std::move(items));
        arr.push_back(json::Value(std::move(o)));
    }
    json::Value::Object root;
    root["characters"] = json::Value(std::move(arr));
    root["nextId"]     = json::Value(static_cast<std::int64_t>(m_nextId));
    return json::Value(std::move(root));
}

Expected<void, Error> CharacterStore::SaveToFile(std::string_view path) const {
    return detail::WriteJsonFile(detail::Utf8Path(path), ToJson());
}

Expected<void, Error> CharacterStore::LoadFromFile(std::string_view path) {
    auto parsed = detail::ReadJsonFile(detail::Utf8Path(path));
    if (!parsed) return parsed.GetError();
    return LoadJson(parsed.Value());
}

Expected<void, Error> CharacterStore::LoadJson(const json::Value& root) {
    CharacterStore candidate;
    candidate.m_chars.clear();
    const json::Value* arr = root.Find("characters");
    if (!arr || !arr->IsArray()) return Error{"CharacterStore: missing characters array", 1};
    {
        for (const json::Value& v : arr->AsArray()) {
            if (!v.IsObject()) return Error{"CharacterStore: invalid record", 1};
            CharacterRecord c;
            if (!detail::IntegerInRange(v, "id", 1, INT64_MAX - 1) ||
                !detail::IntegerInRange(v, "accountId", 1, INT64_MAX - 1))
                return Error{"CharacterStore: invalid identity", 1};
            for (const char* key : {"level", "str", "agi", "int", "vit", "hp", "mp"})
                if (!detail::IntegerInRange(v, key, 0, INT32_MAX, false))
                    return Error{"CharacterStore: invalid stat", 1};
            for (const char* key : {"xp", "gold"})
                if (!detail::IntegerInRange(v, key, 0, INT64_MAX, false))
                    return Error{"CharacterStore: invalid balance", 1};
            for (const char* key : {"posX", "posY"}) {
                const auto* p = v.Find(key);
                if (p && (!p->IsNumber() || !std::isfinite(p->AsDouble()) ||
                          std::abs(p->AsDouble()) > std::numeric_limits<float>::max()))
                    return Error{"CharacterStore: invalid position", 1};
            }
            if (const auto* p = v.Find("sceneId"); p && !p->IsString())
                return Error{"CharacterStore: invalid scene id", 1};
            if (const auto* p = v.Find("items"); p && !p->IsArray())
                return Error{"CharacterStore: invalid items array", 1};
            if (const auto* p = v.Find("id"))        c.id        = static_cast<CharacterId>(p->AsInt());
            if (const auto* p = v.Find("accountId")) c.accountId = static_cast<AccountId>(p->AsInt());
            if (const auto* p = v.Find("name"))      c.name      = std::string(p->AsString());
            if (const auto* p = v.Find("sceneId"))   c.sceneId   = std::string(p->AsString());
            if (const auto* p = v.Find("posX"))      c.posX      = static_cast<float>(p->AsDouble());
            if (const auto* p = v.Find("posY"))      c.posY      = static_cast<float>(p->AsDouble());
            if (const auto* p = v.Find("level"))     c.level     = static_cast<int32_t>(p->AsInt());
            if (const auto* p = v.Find("xp"))        c.xp        = p->AsInt();
            if (const auto* p = v.Find("str"))       c.strength  = static_cast<int32_t>(p->AsInt());
            if (const auto* p = v.Find("agi"))       c.agility   = static_cast<int32_t>(p->AsInt());
            if (const auto* p = v.Find("int"))       c.intellect = static_cast<int32_t>(p->AsInt());
            if (const auto* p = v.Find("vit"))       c.vitality  = static_cast<int32_t>(p->AsInt());
            if (const auto* p = v.Find("hp"))        c.hp        = static_cast<int32_t>(p->AsInt());
            if (const auto* p = v.Find("mp"))        c.mp        = static_cast<int32_t>(p->AsInt());
            if (const auto* p = v.Find("gold"))      c.gold      = p->AsInt();
            if (const auto* p = v.Find("items"); p && p->IsArray()) {
                for (const json::Value& sv : p->AsArray()) {
                    if (!sv.IsObject() || !detail::IntegerInRange(sv, "itemId", 1, UINT32_MAX) ||
                        !detail::IntegerInRange(sv, "count", 1, INT32_MAX))
                        return Error{"CharacterStore: invalid item stack", 1};
                    ItemStackRecord s;
                    if (const auto* q = sv.Find("itemId")) s.itemId = static_cast<uint32_t>(q->AsInt());
                    if (const auto* q = sv.Find("count"))  s.count  = static_cast<int32_t>(q->AsInt());
                    c.items.push_back(s);
                }
            }
            if (c.name.empty() || c.name.size() > 64 || candidate.Get(c.id) ||
                !candidate.m_byName.emplace(c.name, c.id).second)
                return Error{"CharacterStore: empty or duplicate identity", 1};
            candidate.m_chars.push_back(std::move(c));
        }
    }
    if (const auto* p = root.Find("nextId")) candidate.m_nextId = static_cast<CharacterId>(p->AsInt());
    if (!detail::IntegerInRange(root, "nextId", 1, INT64_MAX))
        return Error{"CharacterStore: invalid nextId", 1};
    for (const auto& character : candidate.m_chars)
        if (character.id >= candidate.m_nextId) return Error{"CharacterStore: reused nextId", 1};
    candidate.RebuildIndex();
    *this = std::move(candidate);
    return {};
}

} // namespace mye::persist
