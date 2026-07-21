// mye/core/Events.h — 이벤트 버스 (docs/01 소유 주제)
//
// 계약:
//  - 이벤트 = 평범한 struct(trivially copyable 권장). MYE_EVENT(TypeName)로 타입 ID(FNV-1a 64) 부여.
//  - Publish: 즉시 디스패치, 메인 스레드 전용. 재귀 발행 깊이 제한(기본 8)으로 무한 루프 검출.
//  - Enqueue: 지연 디스패치, 모든 스레드 허용(락 프리 큐 목표) — 스레드 세이프 발행은 이 경로만.
//    구독자 호출은 항상 메인 스레드(Flush 시점).
//  - 핸들러가 true 반환 = consumed(후순위 구독자 전달 중단). priority 낮을수록 먼저.
#pragma once

#include "mye/core/Base.h"

#include <concepts>
#include <functional>
#include <memory>
#include <type_traits>

namespace mye {

template <typename E>
concept EventType = requires { { E::kEventTypeId } -> std::convertible_to<EventTypeId>; };

enum class EventChannel : uint8_t { PreUpdate, PostUpdate, Count };

struct SubscriptionHandle {
    uint64_t opaque = 0;
    bool IsValid() const { return opaque != 0; }
};

class EventBus {
public:
    MYE_SERVICE(EventBus);

    EventBus();
    ~EventBus();
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    // ---- 타입 세이프 표면 (템플릿 → Raw 위임) ----
    template <EventType E>
    SubscriptionHandle Subscribe(std::function<bool(const E&)> handler, int priority = 0) {
        return SubscribeRaw(
            E::kEventTypeId,
            [h = std::move(handler)](const void* payload, size_t /*size*/) -> bool {
                return h(*static_cast<const E*>(payload));
            },
            priority);
    }

    template <EventType E>
    void Publish(const E& e) {                       // 즉시, 메인 스레드 전용
        PublishRaw(E::kEventTypeId, &e, sizeof(E));
    }

    template <EventType E>
    void Enqueue(const E& e, EventChannel channel = EventChannel::PostUpdate) {
        static_assert(std::is_trivially_copyable_v<E>,
                      "Enqueue 이벤트는 trivially copyable이어야 한다 (큐에 바이트 복사)");
        EnqueueRaw(E::kEventTypeId, &e, sizeof(E), channel);
    }

    void Unsubscribe(SubscriptionHandle h);
    void Flush(EventChannel channel);                // 메인 루프가 호출 (PreUpdate/PostUpdate)

    // ---- 타입 소거 표면 (플러그인·Lua 바인딩이 사용, 05) ----
    using RawHandler = std::function<bool(const void* payload, size_t size)>;
    SubscriptionHandle SubscribeRaw(EventTypeId type, RawHandler handler, int priority = 0);
    void PublishRaw(EventTypeId type, const void* payload, size_t size);
    void EnqueueRaw(EventTypeId type, const void* payload, size_t size,
                    EventChannel channel = EventChannel::PostUpdate);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// RAII 구독 해제 — 모듈·패널·핸들러의 표준 보관 방식
class ScopedSubscription {
public:
    ScopedSubscription() = default;
    ScopedSubscription(EventBus& bus, SubscriptionHandle h) : m_bus(&bus), m_handle(h) {}
    ~ScopedSubscription() { Reset(); }

    ScopedSubscription(const ScopedSubscription&) = delete;
    ScopedSubscription& operator=(const ScopedSubscription&) = delete;
    ScopedSubscription(ScopedSubscription&& o) noexcept
        : m_bus(o.m_bus), m_handle(o.m_handle) { o.m_bus = nullptr; o.m_handle = {}; }
    ScopedSubscription& operator=(ScopedSubscription&& o) noexcept {
        if (this != &o) { Reset(); m_bus = o.m_bus; m_handle = o.m_handle; o.m_bus = nullptr; o.m_handle = {}; }
        return *this;
    }

    void Reset() {
        if (m_bus && m_handle.IsValid()) m_bus->Unsubscribe(m_handle);
        m_bus = nullptr; m_handle = {};
    }

private:
    EventBus*          m_bus = nullptr;
    SubscriptionHandle m_handle{};
};

} // namespace mye
