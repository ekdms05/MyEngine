// mye/ecs/World.h — ECS 저장소(희소집합) + 런타임 타입 등록 (docs/03 §1, §핵심 API)
//
// 씬과 독립적인 ECS 저장소(에디터 프리뷰용 다중 인스턴스 허용). EnTT식 희소집합을 mye::ecs
// 공개 API로 래핑한다 — EnTT 타입은 외부에 노출하지 않는다(래핑 원칙, docs/03 §1).
// M2-A는 자체 sparse-set(ComponentPool) 기반 기준 구현으로 계약을 만족한다.
//
// 구조 변경 규칙: 순회 중 Create/Destroy/Add/Remove 직접 호출 금지 — CommandBuffer 경유,
// 페이즈 경계 Flush. World의 직접 변경 메서드는 부트스트랩·비순회 컨텍스트(에디터 편집 등)용.
#pragma once

#include "mye/ecs/ComponentPool.h"
#include "mye/ecs/ComponentType.h"
#include "mye/ecs/Entity.h"

#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mye { class EventBus; }

namespace mye::ecs {

template <typename... Cs> class View;

class World {
public:
    World();
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // ---- 엔티티 수명 ----
    Entity Create();
    void   Destroy(Entity e);                  // 즉시(비순회). 순회 중엔 CommandBuffer 경유.
    bool   Valid(Entity e) const;              // == docs의 alive()

    // 지정 핸들(index, generation)로 엔티티를 재생성한다 — 에디터 Undo가 파괴된 엔티티를
    // 원래 핸들 그대로 복원하기 위한 경로(SceneSerializer/구조 커맨드가 소비, M4 리뷰 요청).
    // 계약:
    //   - 성공하면 정확히 {index, generation}인 Entity를 반환한다(핸들 보존).
    //   - 대상 index가 살아있으면(이미 유효한 엔티티 점유) 실패 → Entity::Null() 반환.
    //   - generation == 0(null 세대)이면 실패 → Entity::Null().
    //   - 세대 테이블이 index보다 짧으면 그 사이 슬롯을 죽은(0) 세대로 채워 확장하고,
    //     확장으로 생긴 빈 슬롯들은 freelist에 등록해 이후 Create가 재사용하게 한다.
    //   - 대상 index가 현재 freelist에 있으면 제거한 뒤 그 index로 되살린다.
    Entity CreateWithId(uint32_t index, uint32_t generation);

    // ---- 컴포넌트 타입 등록 (네이티브·플러그인·Lua 공용) ----
    // 네이티브 타입은 최초 Add에서 자동 등록되지만, 명시 등록도 가능(직렬화 순서 고정 등).
    ComponentTypeId RegisterComponent(const ComponentTypeDesc& desc);
    template <typename C>
    ComponentTypeId RegisterComponent(const char* name) {
        return RegisterComponent(MakeComponentTypeDesc<C>(name));
    }
    bool IsRegistered(ComponentTypeId type) const;

    // ---- 컴포넌트 조작 (템플릿 경로) ----
    template <typename C, typename... Args>
    C& Add(Entity e, Args&&... args) {
        EnsureRegistered<C>();
        void* p = AddDynamic(e, C::kComponentTypeId);
        C* comp = static_cast<C*>(p);
        *comp = C(std::forward<Args>(args)...);
        return *comp;
    }
    template <typename C>
    C* TryGet(Entity e) {
        return static_cast<C*>(TryGetDynamic(e, C::kComponentTypeId));
    }
    template <typename C>
    bool Has(Entity e) const {
        return HasDynamic(e, C::kComponentTypeId);
    }
    template <typename C>
    void Remove(Entity e) {
        RemoveDynamic(e, C::kComponentTypeId);
    }

    // ---- 동적(비템플릿) 경로 — 플러그인·Lua·에디터·직렬화 ----
    void* AddDynamic(Entity e, ComponentTypeId type);       // 기본 생성 후 포인터 반환
    void* TryGetDynamic(Entity e, ComponentTypeId type);
    const void* TryGetDynamic(Entity e, ComponentTypeId type) const;
    bool  HasDynamic(Entity e, ComponentTypeId type) const;
    void  RemoveDynamic(Entity e, ComponentTypeId type);

    // ---- 쿼리 ----
    // 다중 컴포넌트 교집합 순회(최소 풀 기준). View는 경량 값(복사 가능).
    template <typename... Cs>
    View<Cs...> Query();

    // ---- 월드-로컬 이벤트 버스 (게임플레이 이벤트 전용, docs/03 §2) ----
    // Flush는 페이즈 스케줄러가 각 페이즈 경계 CommandBuffer 플러시 직후 수행.
    void      SetEventBus(EventBus* bus) { m_events = bus; }
    EventBus* Events() { return m_events; }

    // ---- 순회 내부 접근(View·시스템이 사용) ----
    ComponentPool*       Pool(ComponentTypeId type);
    const ComponentPool* Pool(ComponentTypeId type) const;
    uint32_t EntityGeneration(uint32_t index) const;   // Valid 확인용

private:
    template <typename C>
    void EnsureRegistered() {
        if (!IsRegistered(C::kComponentTypeId))
            RegisterComponent(MakeComponentTypeDesc<C>("<component>"));
    }

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    EventBus* m_events = nullptr;   // 비소유. SceneManager/SceneModule이 배선.
};

} // namespace mye::ecs

#include "mye/ecs/View.inl"
