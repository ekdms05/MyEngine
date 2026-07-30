// mye/plugin/PluginHost.cpp — 플러그인 호스트 구현 (PluginHost.h 참조)
#include "mye/plugin/PluginHost.h"

#include "mye/core/Log.h"

#include <algorithm>

#if defined(_WIN32)
    #include <windows.h>
#endif

namespace mye::plugin {

namespace { void DeleteInProcess(IPlugin* p) { delete p; } }

PluginHost::~PluginHost() {
    // 역순 언로드(로드 대칭). Unload 가 벡터를 수정하므로 뒤에서부터.
    while (!m_loaded.empty()) {
        Unload(m_loaded.back()->info.name);
    }
}

Expected<void, Error> PluginHost::LoadInternal(IPlugin* plugin, void (*destroy)(IPlugin*), void* dll) {
    // 실패 시 호출자 자원(plugin/dll) 정리는 각 진입점이 담당(여기선 등록만).
    const PluginInfo info = plugin->Info();
    if (info.name.empty()) return Error{"PluginHost::Load: 이름이 비었습니다", 3};
    if (IsLoaded(info.name)) return Error{"PluginHost::Load: 이미 로드된 플러그인 '" + info.name + "'", 4};
    if (info.minEngineVersion > m_engineVersion)
        return Error{"PluginHost::Load: 엔진버전 부족(필요 " + std::to_string(info.minEngineVersion) +
                     " > 현재 " + std::to_string(m_engineVersion) + ") '" + info.name + "'", 5};

    auto loaded = std::make_unique<Loaded>(Loaded{ plugin, destroy, dll, info, PluginContext(m_engineVersion) });
    loaded->plugin->OnLoad(loaded->ctx);
    MYE_LOG_INFO("Plugin", "로드 '{}' v{}{} (타입 {}, 시스템 {})",
                 info.name, info.version, dll ? " [DLL]" : "",
                 loaded->ctx.Types().size(), loaded->ctx.Systems().size());
    m_loaded.push_back(std::move(loaded));
    return {};
}

Expected<void, Error> PluginHost::Load(std::unique_ptr<IPlugin> plugin) {
    if (!plugin) return Error{"PluginHost::Load: null 플러그인", 1};
    if (m_ticking) return Error{"PluginHost::Load: 틱 중에는 로드 불가", 2};
    IPlugin* raw = plugin.release();
    auto r = LoadInternal(raw, &DeleteInProcess, nullptr);
    if (!r) DeleteInProcess(raw);   // 등록 실패 → 정리
    return r;
}

Expected<void, Error> PluginHost::LoadDll(std::string_view path) {
    if (m_ticking) return Error{"PluginHost::LoadDll: 틱 중에는 로드 불가", 2};
#if defined(_WIN32)
    const std::string p(path);
    HMODULE mod = ::LoadLibraryA(p.c_str());
    if (!mod) return Error{"PluginHost::LoadDll: DLL 로드 실패 '" + p + "'", 10};

    auto create  = reinterpret_cast<CreatePluginFn>(reinterpret_cast<void*>(::GetProcAddress(mod, kCreatePluginSymbol)));
    auto destroy = reinterpret_cast<DestroyPluginFn>(reinterpret_cast<void*>(::GetProcAddress(mod, kDestroyPluginSymbol)));
    if (!create || !destroy) { ::FreeLibrary(mod); return Error{"PluginHost::LoadLibrary: 엔트리 심볼 없음 '" + p + "'", 11}; }

    IPlugin* plugin = create();
    if (!plugin) { ::FreeLibrary(mod); return Error{"PluginHost::LoadLibrary: MyeCreatePlugin 이 null", 12}; }

    auto r = LoadInternal(plugin, destroy, reinterpret_cast<void*>(mod));
    if (!r) { destroy(plugin); ::FreeLibrary(mod); }   // 등록 실패 → DLL 이 파괴 + 언로드
    return r;
#else
    (void)path;
    return Error{"PluginHost::LoadLibrary: 이 플랫폼 미지원(Windows 전용)", 13};
#endif
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
        // 파괴: DLL 이면 DLL 의 destroy 로(힙 분리), 아니면 delete.
        if (L.destroy) L.destroy(L.plugin); else delete L.plugin;
        void* dll = L.dll;
        MYE_LOG_INFO("Plugin", "언로드 '{}'", L.info.name);
        m_loaded.erase(it);
#if defined(_WIN32)
        if (dll) ::FreeLibrary(static_cast<HMODULE>(dll));   // 벡터에서 뺀 뒤 라이브러리 해제
#else
        (void)dll;
#endif
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
