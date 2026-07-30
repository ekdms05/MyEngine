// mye/plugin/PluginHost.h — 플러그인 로드/언로드·틱 오케스트레이션 (엔진 확장성)
//
// 플러그인을 엔진버전 호환 검사 후 로드(OnLoad)하고, 등록된 시스템을 매 프레임 틱하며,
// 언로드 시 OnUnload + 등록 타입 TypeRegistry 해제까지 대칭 정리한다. 인프로세스 1단계.
#pragma once

#include "mye/plugin/Plugin.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mye::plugin {

class PluginHost {
public:
    explicit PluginHost(uint32_t engineVersion) : m_engineVersion(engineVersion) {}
    ~PluginHost();
    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;

    // 플러그인 로드: 버전 호환·이름 중복 검사 → OnLoad(등록 누적). 실패 시 Error.
    Expected<void, Error> Load(std::unique_ptr<IPlugin> plugin);

    // 네이티브 DLL 플러그인 로드: LoadLibrary(path) → MyeCreatePlugin() → Load. 언로드 시 FreeLibrary.
    //   (이름은 Windows LoadLibrary 매크로 충돌 회피 위해 LoadDll.)
    //   [주의] DLL 이 엔진을 정적 링크하면 리플렉션 TypeRegistry 싱글턴이 분리된다 — DLL 에서 등록한
    //   타입은 호스트에서 안 보인다(시스템/직접 등록은 안전). 완전한 타입 공유는 서비스 게이트웨이 후속.
    Expected<void, Error> LoadDll(std::string_view path);

    // 언로드: OnUnload → 등록 타입 TypeRegistry 해제 → 시스템 제거(+ DLL 이면 FreeLibrary). 없으면 false.
    bool Unload(std::string_view name);

    // 등록된 모든 플러그인 시스템을 orderKey 순으로 틱.
    void Tick(float dt);

    bool     IsLoaded(std::string_view name) const;
    size_t   Count() const { return m_loaded.size(); }
    size_t   SystemCount() const;
    std::vector<std::string> Names() const;

private:
    struct Loaded {
        IPlugin*        plugin = nullptr;
        void          (*destroy)(IPlugin*) = nullptr;   // null → delete(인프로세스). DLL 은 MyeDestroyPlugin.
        void*           dll = nullptr;                   // HMODULE(void* 은닉) — 언로드 시 FreeLibrary
        PluginInfo      info;
        PluginContext   ctx;   // 등록 내역(타입·시스템) 보관
    };
    // 공용 로드 경로(인프로세스·DLL 공통): 검증 → OnLoad → 등록.
    Expected<void, Error> LoadInternal(IPlugin* plugin, void (*destroy)(IPlugin*), void* dll);

    uint32_t                             m_engineVersion;
    std::vector<std::unique_ptr<Loaded>> m_loaded;
    bool                                 m_ticking = false;   // 틱 중 로드/언로드 재진입 가드
};

} // namespace mye::plugin
