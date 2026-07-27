// mye/persist/CharacterStore.cpp — 캐릭터 영속 구현 (CharacterStore.h 참조)
#include "mye/persist/CharacterStore.h"

#include "mye/core/Json.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace mye::persist {

Expected<CharacterId, Error> CharacterStore::Create(AccountId accountId, std::string_view name) {
    if (accountId == 0)  return Error{"Create: 유효하지 않은 계정", 1};
    if (name.empty())    return Error{"Create: 캐릭터명이 비었습니다", 2};
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

Expected<void, Error> CharacterStore::SaveToFile(std::string_view path) const {
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
    const std::string text = json::Stringify(json::Value(std::move(root)));

    // 원자적 쓰기(임시 → rename).
    const std::string tmp = std::string(path) + ".tmp";
    { std::ofstream os(tmp, std::ios::binary | std::ios::trunc);
      if (!os) return Error{"SaveToFile: 열기 실패 '" + std::string(path) + "'", 1};
      os.write(text.data(), static_cast<std::streamsize>(text.size()));
      if (!os) return Error{"SaveToFile: 쓰기 실패", 2}; }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) return Error{"SaveToFile: rename 실패", 3};
    return {};
}

Expected<void, Error> CharacterStore::LoadFromFile(std::string_view path) {
    std::ifstream in(std::string(path), std::ios::binary);
    if (!in) return Error{"LoadFromFile: 열기 실패 '" + std::string(path) + "'", 1};
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto parsed = json::Parse(text);
    if (!parsed) return parsed.GetError();
    const json::Value& root = parsed.Value();

    m_chars.clear();
    const json::Value* arr = root.Find("characters");
    if (arr && arr->IsArray()) {
        for (const json::Value& v : arr->AsArray()) {
            if (!v.IsObject()) continue;
            CharacterRecord c;
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
                    if (!sv.IsObject()) continue;
                    ItemStackRecord s;
                    if (const auto* q = sv.Find("itemId")) s.itemId = static_cast<uint32_t>(q->AsInt());
                    if (const auto* q = sv.Find("count"))  s.count  = static_cast<int32_t>(q->AsInt());
                    c.items.push_back(s);
                }
            }
            m_chars.push_back(std::move(c));
        }
    }
    if (const auto* p = root.Find("nextId")) m_nextId = static_cast<CharacterId>(p->AsInt());
    if (m_nextId < 1) m_nextId = 1;
    RebuildIndex();
    return {};
}

} // namespace mye::persist
