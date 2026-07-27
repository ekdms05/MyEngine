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

    // 언로드: OnUnload → 등록 타입 TypeRegistry 해제 → 시스템 제거. 없으면 false.
    bool Unload(std::string_view name);

    // 등록된 모든 플러그인 시스템을 orderKey 순으로 틱.
    void Tick(float dt);

    bool     IsLoaded(std::string_view name) const;
    size_t   Count() const { return m_loaded.size(); }
    size_t   SystemCount() const;
    std::vector<std::string> Names() const;

private:
    struct Loaded {
        std::unique_ptr<IPlugin> plugin;
        PluginInfo               info;
        PluginContext            ctx;   // 등록 내역(타입·시스템) 보관
    };

    uint32_t                             m_engineVersion;
    std::vector<std::unique_ptr<Loaded>> m_loaded;
    bool                                 m_ticking = false;   // 틱 중 로드/언로드 재진입 가드
};

} // namespace mye::plugin
