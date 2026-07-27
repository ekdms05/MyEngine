// mye/plugin/PluginHost.cpp — 플러그인 호스트 구현 (PluginHost.h 참조)
#include "mye/plugin/PluginHost.h"

#include "mye/core/Log.h"

#include <algorithm>

namespace mye::plugin {

PluginHost::~PluginHost() {
    // 역순 언로드(로드 대칭). Unload 가 벡터를 수정하므로 뒤에서부터.
    while (!m_loaded.empty()) {
        Unload(m_loaded.back()->info.name);
    }
}

Expected<void, Error> PluginHost::Load(std::unique_ptr<IPlugin> plugin) {
    if (!plugin) return Error{"PluginHost::Load: null 플러그인", 1};
    if (m_ticking) return Error{"PluginHost::Load: 틱 중에는 로드 불가", 2};

    const PluginInfo info = plugin->Info();
    if (info.name.empty()) return Error{"PluginHost::Load: 이름이 비었습니다", 3};
    if (IsLoaded(info.name)) return Error{"PluginHost::Load: 이미 로드된 플러그인 '" + info.name + "'", 4};
    if (info.minEngineVersion > m_engineVersion)
        return Error{"PluginHost::Load: 엔진버전 부족(필요 " + std::to_string(info.minEngineVersion) +
                     " > 현재 " + std::to_string(m_engineVersion) + ") '" + info.name + "'", 5};

    auto loaded = std::make_unique<Loaded>(Loaded{ std::move(plugin), info, PluginContext(m_engineVersion) });
    loaded->plugin->OnLoad(loaded->ctx);
    MYE_LOG_INFO("Plugin", "로드 '{}' v{} (타입 {}, 시스템 {})",
                 info.name, info.version, loaded->ctx.Types().size(), loaded->ctx.Systems().size());
    m_loaded.push_back(std::move(loaded));
    return {};
}

bool PluginHost::Unload(std::string_view name) {
    if (m_ticking) { MYE_LOG_WARN("Plugin", "틱 중 언로드 무시 '{}'", std::string(name)); return false; }
    for (auto it = m_loaded.begin(); it != m_loaded.end(); ++it) {
        if ((*it)->info.name != name) continue;
        Loaded& L = **it;
        L.plugin->OnUnload(L.ctx);
        // 플러그인이 등록한 리플렉션 타입 해제(dangling 방지는 호출자 책임 계약).
        if (!L.ctx.Types().empty())
            refl::TypeRegistry::Get().Unregister(L.ctx.Types());
        MYE_LOG_INFO("Plugin", "언로드 '{}'", L.info.name);
        m_loaded.erase(it);
        return true;
    }
    return false;
}

void PluginHost::Tick(float dt) {
    // orderKey 순 실행을 위해 (plugin순, system orderKey순) 평탄화 정렬.
    struct Entry { int order; std::function<void(float)>* fn; };
    std::vector<Entry> entries;
    for (const auto& L : m_loaded)
        for (const PluginSystem& s : L->ctx.Systems())
            entries.push_back(Entry{ s.orderKey, const_cast<std::function<void(float)>*>(&s.tick) });
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b){ return a.order < b.order; });

    m_ticking = true;
    for (const Entry& e : entries) if (*e.fn) (*e.fn)(dt);
    m_ticking = false;
}

bool PluginHost::IsLoaded(std::string_view name) const {
    for (const auto& L : m_loaded) if (L->info.name == name) return true;
    return false;
}

size_t PluginHost::SystemCount() const {
    size_t n = 0;
    for (const auto& L : m_loaded) n += L->ctx.Systems().size();
    return n;
}

std::vector<std::string> PluginHost::Names() const {
    std::vector<std::string> out;
    out.reserve(m_loaded.size());
    for (const auto& L : m_loaded) out.push_back(L->info.name);
    return out;
}

} // namespace mye::plugin
