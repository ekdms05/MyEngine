// mye/ecs/CommandBuffer.h — 지연 구조변경 (docs/03 §2, §핵심 API 스케치)
//
// 구조 변경(생성·파괴·컴포넌트 추가/제거·reparent)은 순회 중 금지 — 반드시 CommandBuffer에
// 기록하고 페이즈 경계에서 Flush한다. 이터레이터 무효화와 병렬화 안전성을 동시에 해결한다.
//
// createDeferred는 즉시 유효한 Entity 핸들을 반환하되(World가 예약), 실제 저장소 반영은
// Flush 시점 — 같은 프레임에서 반환 핸들에 컴포넌트를 add하는 참조 연결이 가능하다.
#pragma once

#include "mye/ecs/ComponentType.h"
#include "mye/ecs/Entity.h"
#include "mye/ecs/World.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace mye::ecs {

class World;

class CommandBuffer {
public:
    explicit CommandBuffer(World& world);
    ~CommandBuffer();
    CommandBuffer(const CommandBuffer&) = delete;
    CommandBuffer& operator=(const CommandBuffer&) = delete;

    // 즉시 유효 핸들 반환(World가 index/generation 예약). 저장소 반영은 Flush.
    Entity CreateDeferred();
    void   Destroy(Entity e);

    // 네이티브 타입 add(값 이동). 컴포넌트 값은 CommandBuffer가 소유하다 Flush 시 이관.
    template <typename C>
    void Add(Entity e, C&& value) {
        using T = std::decay_t<C>;
        auto stored = std::make_shared<T>(std::forward<C>(value));
        RecordAddDynamic(e, T::kComponentTypeId,
                         [stored](World& w, Entity ent) { AddToWorld<T>(w, ent, *stored); });
    }

    template <typename C>
    void Remove(Entity e) {
        RecordRemoveDynamic(e, C::kComponentTypeId);
    }

    // 동적(비템플릿) 경로 — 플러그인·Lua·직렬화. valueBytes는 Flush까지 유효해야 하므로
    // 내부에서 복사 저장한다(desc.size·moveConstruct 사용).
    void AddDynamic(Entity e, const ComponentTypeDesc& desc, const void* valueBytes);
    void RemoveDynamic(Entity e, ComponentTypeId type);

    // reparent — 월드 위치 유지 옵션(TransformSystem이 처리).
    void SetParent(Entity child, Entity parent, bool keepWorld = true);

    // reparent 후크 — ecs 계층은 scene(Transform)에 직접 의존하지 않으므로, Flush 시
    // SetParent 명령을 실제 Parent/Children 갱신으로 잇는 콜백을 scene 계층이 설치한다.
    // (scene::ApplyReparent 시그니처와 동일. 미설치 시 SetParent는 no-op.)
    using ReparentHook = void (*)(World&, Entity child, Entity parent, bool keepWorld);
    static void SetReparentHook(ReparentHook hook);

    // 페이즈 경계에서 스케줄러가 호출 — 기록된 명령을 순서대로 적용.
    void Flush();
    bool Empty() const;

private:
    template <typename T>
    static void AddToWorld(World& w, Entity e, const T& value) {
        w.Add<T>(e) = value;
    }

    void RecordAddDynamic(Entity e, ComponentTypeId type,
                          std::function<void(World&, Entity)> apply);
    void RecordRemoveDynamic(Entity e, ComponentTypeId type);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mye::ecs
