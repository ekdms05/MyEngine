// mye/liveops/ServerConfig.cpp — 서버 CVar·피처플래그 구현 (ServerConfig.h 참조)
#include "mye/liveops/ServerConfig.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace mye::liveops {

int64_t ServerConfig::GetInt(std::string_view key, int64_t fallback) const {
    auto it = m_values.find(key);
    return it == m_values.end() ? fallback : it->second.AsInt(fallback);
}

double ServerConfig::GetFloat(std::string_view key, double fallback) const {
    auto it = m_values.find(key);
    return it == m_values.end() ? fallback : it->second.AsDouble(fallback);
}

bool ServerConfig::GetBool(std::string_view key, bool fallback) const {
    auto it = m_values.find(key);
    return it == m_values.end() ? fallback : it->second.AsBool(fallback);
}

std::string ServerConfig::GetString(std::string_view key, std::string_view fallback) const {
    auto it = m_values.find(key);
    return it == m_values.end() ? std::string(fallback) : std::string(it->second.AsString(fallback));
}

bool ServerConfig::Has(std::string_view key) const {
    return m_values.find(key) != m_values.end();
}

void ServerConfig::SetInt(std::string_view key, int64_t v)    { m_values[std::string(key)] = json::Value(v); }
void ServerConfig::SetFloat(std::string_view key, double v)   { m_values[std::string(key)] = json::Value(v); }
void ServerConfig::SetBool(std::string_view key, bool v)      { m_values[std::string(key)] = json::Value(v); }
void ServerConfig::SetString(std::string_view key, std::string_view v) { m_values[std::string(key)] = json::Value(std::string(v)); }

bool ServerConfig::HasFlag(std::string_view name) const {
    auto it = m_flags.find(std::string(name));
    return it != m_flags.end() && it->second;
}

void ServerConfig::SetFlag(std::string_view name, bool on) {
    m_flags[std::string(name)] = on;
}

Expected<void, Error> ServerConfig::LoadFromFile(std::string_view path) {
    std::ifstream in(std::string(path), std::ios::binary);
    if (!in) return Error{"ServerConfig::LoadFromFile: 열기 실패 '" + std::string(path) + "'", 1};
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto parsed = json::Parse(text);
    if (!parsed) return parsed.GetError();
    const json::Value& root = parsed.Value();

    // 핫 리로드: 기존 값 대체.
    m_values.clear();
    m_flags.clear();
    if (const auto* cv = root.Find("cvars"); cv && cv->IsObject()) {
        for (const auto& [k, v] : cv->AsObject())
            if (v.IsNumber() || v.IsString() || v.IsBool()) m_values[k] = v;
    }
    if (const auto* fl = root.Find("flags"); fl && fl->IsObject()) {
        for (const auto& [k, v] : fl->AsObject())
            m_flags[k] = v.AsBool(false);
    }
    return {};
}

Expected<void, Error> ServerConfig::SaveToFile(std::string_view path) const {
    json::Value::Object cvars;
    for (const auto& [k, v] : m_values) cvars[k] = v;
    json::Value::Object flags;
    for (const auto& [k, v] : m_flags) flags[k] = json::Value(v);

    json::Value::Object root;
    root["cvars"] = json::Value(std::move(cvars));
    root["flags"] = json::Value(std::move(flags));
    const std::string outText = json::Stringify(json::Value(std::move(root)));

    const std::string tmp = std::string(path) + ".tmp";
    { std::ofstream os(tmp, std::ios::binary | std::ios::trunc);
      if (!os) return Error{"ServerConfig::SaveToFile: 열기 실패 '" + std::string(path) + "'", 1};
      os.write(outText.data(), static_cast<std::streamsize>(outText.size()));
      if (!os) return Error{"ServerConfig::SaveToFile: 쓰기 실패", 2}; }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) return Error{"ServerConfig::SaveToFile: rename 실패", 3};
    return {};
}

} // namespace mye::liveops
