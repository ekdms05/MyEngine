// mye/plugin/Plugin.h — 플러그인 확장 인터페이스 + 등록 컨텍스트 (엔진 확장성)
//
// 플러그인은 엔진에 "포함되지 않는" 확장 단위다. 게임 로직(예: MMORPG 규칙)·툴·콘텐츠는
// 플러그인으로 붙는다. 플러그인은 OnLoad 에서 리플렉션 타입·시스템(틱 콜백)을 등록하고,
// OnUnload 에서 대칭 해제한다. 호스트가 엔진버전 호환을 게이팅하고 언로드 시 정리한다.
//
// 1단계: 인프로세스(정적) 플러그인. 네이티브 DLL 로더(LoadLibrary + 엔트리 심볼)는 후속 —
//   IPlugin/PluginContext 계약은 DLL 경계에서도 동일하게 쓰이도록 설계했다.
#pragma once

#include "mye/core/Base.h"
#include "mye/refl/TypeRegistry.h"   // refl::TypeId, GetType<T>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace mye::plugin {

// 플러그인 식별·버전. minEngineVersion 이 호스트 엔진버전보다 크면 로드 거부(ABI/기능 가드).
struct PluginInfo {
    std::string name;
    uint32_t    version = 1;
    uint32_t    minEngineVersion = 0;
    std::string description;
};

// 플러그인이 제공하는 per-frame 시스템(틱 콜백). 이름은 진단·중복 방지용.
struct PluginSystem {
    std::string                name;
    std::function<void(float)> tick;
    int                        orderKey = 0;   // 작을수록 먼저
};

// OnLoad/OnUnload 에 전달 — 플러그인이 등록하는 것을 누적(호스트가 대칭 해제에 사용).
class PluginContext {
public:
    explicit PluginContext(uint32_t engineVersion) : m_engineVersion(engineVersion) {}

    uint32_t EngineVersion() const { return m_engineVersion; }

    // 리플렉션 타입 등록(언로드 시 TypeRegistry 에서 해제되도록 추적).
    template <typename T>
    void RegisterType() { RegisterTypeInfo(refl::GetType<T>()); }
    void RegisterTypeInfo(const refl::TypeInfo* t) { if (t) m_types.push_back(t->Id()); }

    // per-frame 시스템 등록(플러그인이 동작을 주입). orderKey 로 실행 순서 결정.
    void RegisterSystem(std::string name, std::function<void(float)> tick, int orderKey = 0) {
        m_systems.push_back(PluginSystem{ std::move(name), std::move(tick), orderKey });
    }

    // 조회(테스트/진단).
    const std::vector<refl::TypeId>&   Types()   const { return m_types; }
    const std::vector<PluginSystem>&   Systems() const { return m_systems; }

private:
    uint32_t                  m_engineVersion;
    std::vector<refl::TypeId> m_types;
    std::vector<PluginSystem> m_systems;
};

// 확장 단위. 정적/DLL 공통 계약.
class IPlugin {
public:
    virtual ~IPlugin() = default;
    virtual PluginInfo Info() const = 0;
    virtual void OnLoad(PluginContext& ctx) = 0;     // 타입·시스템 등록
    virtual void OnUnload(PluginContext& ctx) = 0;   // 대칭 해제(호스트가 타입 Unregister 는 자동)
};

} // namespace mye::plugin
