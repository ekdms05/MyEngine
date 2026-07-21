// Events.cpp — 이벤트 버스 구현 (docs/01 "이벤트 버스 — 모듈 디커플링의 중심")
//
// 구현 노트:
//  - 타입별 버킷(unordered_map<EventTypeId, Bucket>) + priority 오름차순 정렬 유지
//    (같은 priority 사이에서는 구독 순서 보존 — upper_bound 삽입).
//  - Publish/Flush/Subscribe/Unsubscribe는 메인 스레드 전용(생성 스레드 기준 어서트).
//    스레드 세이프 발행은 Enqueue 경로만 — 큐는 뮤텍스 보호(락 프리 큐는 M1+ 목표).
//  - consumed: 핸들러가 true 반환 시 후순위 구독자로의 전달 중단.
//  - 재귀 발행 깊이 제한(kMaxPublishDepth=8) — 초과 시 에러 로그 후 이벤트 드롭.
//  - 디스패치 중 구독 변경 안전성: 디스패치 중 Unsubscribe는 pendingRemoval 마킹(지연 삭제),
//    Subscribe는 pending 목록에 지연 삽입. 최외곽 디스패치 종료 시 일괄 반영 —
//    디스패치 중에는 버킷 벡터가 절대 재배치되지 않는다.
//  - Flush 중 Enqueue된 이벤트는 스왑 방식으로 다음 Flush로 이월.
#include "mye/core/Events.h"

#include "mye/core/Assert.h"
#include "mye/core/Log.h"

#include <algorithm>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace mye {

namespace {
constexpr int kMaxPublishDepth = 8;   // 구독자 내부 재귀 Publish 허용 깊이 (무한 루프 검출)
} // namespace

struct EventBus::Impl {
    struct Subscription {
        uint64_t    id = 0;
        int         priority = 0;
        RawHandler  handler;
        bool        pendingRemoval = false;   // 디스패치 중 Unsubscribe → 지연 삭제 마킹
    };
    struct Bucket {
        std::vector<Subscription> sorted;     // priority 오름차순(낮을수록 먼저), 동순위는 구독 순서
    };
    struct PendingSubscription {
        EventTypeId  type = 0;
        Subscription sub;
    };
    struct QueuedEvent {
        EventTypeId type = 0;
        std::vector<std::byte> payload;       // trivially copyable 이벤트의 바이트 복사
    };

    // ---- 구독 테이블 (메인 스레드 전용) ----
    std::unordered_map<EventTypeId, Bucket>   buckets;
    std::unordered_map<uint64_t, EventTypeId> handleToType;   // Unsubscribe용 역색인
    std::vector<PendingSubscription>          pendingSubscriptions;
    bool                                      hasPendingRemovals = false;
    uint64_t                                  nextSubscriptionId = 1;
    int                                       dispatchDepth = 0;

    // ---- 지연 큐 (모든 스레드 발행 허용 — 뮤텍스 보호) ----
    std::mutex               queueMutex;
    std::vector<QueuedEvent> queues[static_cast<size_t>(EventChannel::Count)];

    // 버스를 생성한 스레드 = 메인 스레드로 간주 (GuardedMain이 메인 스레드에서 생성)
    std::thread::id mainThreadId = std::this_thread::get_id();

    bool IsMainThread() const { return std::this_thread::get_id() == mainThreadId; }

    void InsertSorted(EventTypeId type, Subscription&& sub) {
        auto& vec = buckets[type].sorted;
        const auto pos = std::upper_bound(
            vec.begin(), vec.end(), sub.priority,
            [](int priority, const Subscription& s) { return priority < s.priority; });
        vec.insert(pos, std::move(sub));
    }

    // 최외곽 디스패치 종료 시(dispatchDepth==0) 지연된 구독 변경을 일괄 반영
    void ApplyPendingMutations() {
        if (hasPendingRemovals) {
            for (auto& [type, bucket] : buckets) {
                (void)type;
                std::erase_if(bucket.sorted,
                              [](const Subscription& s) { return s.pendingRemoval; });
            }
            hasPendingRemovals = false;
        }
        if (!pendingSubscriptions.empty()) {
            std::vector<PendingSubscription> pending = std::move(pendingSubscriptions);
            pendingSubscriptions.clear();
            for (auto& p : pending)
                InsertSorted(p.type, std::move(p.sub));
        }
    }
};

EventBus::EventBus() : m_impl(std::make_unique<Impl>()) {}
EventBus::~EventBus() = default;

SubscriptionHandle EventBus::SubscribeRaw(EventTypeId type, RawHandler handler, int priority) {
    auto& s = *m_impl;
    MYE_ASSERT(s.IsMainThread(), "EventBus::Subscribe는 메인 스레드 전용");
    if (!MYE_VERIFY(static_cast<bool>(handler), "EventBus::Subscribe: 빈 핸들러"))
        return {};

    const uint64_t id = s.nextSubscriptionId++;
    s.handleToType.emplace(id, type);

    Impl::Subscription sub{id, priority, std::move(handler), false};
    if (s.dispatchDepth > 0) {
        // 디스패치 중 삽입은 버킷 벡터를 재배치시키므로 지연 (현재 디스패치에는 참여하지 않음)
        s.pendingSubscriptions.push_back({type, std::move(sub)});
    } else {
        s.InsertSorted(type, std::move(sub));
    }
    return {id};
}

void EventBus::Unsubscribe(SubscriptionHandle h) {
    auto& s = *m_impl;
    MYE_ASSERT(s.IsMainThread(), "EventBus::Unsubscribe는 메인 스레드 전용");
    if (!h.IsValid())
        return;

    const auto typeIt = s.handleToType.find(h.opaque);
    if (typeIt == s.handleToType.end())
        return;   // 이미 해제됨 — 중복 해제는 무해한 no-op
    const EventTypeId type = typeIt->second;
    s.handleToType.erase(typeIt);

    // 아직 버킷에 반영되지 않은 지연 구독일 수 있다
    if (std::erase_if(s.pendingSubscriptions,
                      [&](const Impl::PendingSubscription& p) { return p.sub.id == h.opaque; }) > 0)
        return;

    const auto bucketIt = s.buckets.find(type);
    if (bucketIt == s.buckets.end())
        return;
    auto& vec = bucketIt->second.sorted;
    const auto subIt = std::find_if(vec.begin(), vec.end(),
                                    [&](const Impl::Subscription& sub) { return sub.id == h.opaque; });
    if (subIt == vec.end())
        return;

    if (s.dispatchDepth > 0) {
        // 디스패치 중 삭제는 핸들러 수명·순회 안전을 위해 마킹만 (이후 호출은 건너뜀)
        subIt->pendingRemoval = true;
        s.hasPendingRemovals = true;
    } else {
        vec.erase(subIt);
    }
}

void EventBus::PublishRaw(EventTypeId type, const void* payload, size_t size) {
    auto& s = *m_impl;
    MYE_ASSERT(s.IsMainThread(),
               "EventBus::Publish는 메인 스레드 전용 — 다른 스레드에서는 Enqueue를 사용하라");

    if (s.dispatchDepth >= kMaxPublishDepth) {
        MYE_LOG_ERROR("Events",
                      "재귀 발행 깊이 제한({}) 초과 — 이벤트 드롭 (type=0x{:016x}). 무한 발행 루프 의심",
                      kMaxPublishDepth, type);
        MYE_ASSERT(false, "EventBus 재귀 발행 깊이 제한 초과");
        return;
    }

    const auto bucketIt = s.buckets.find(type);
    if (bucketIt == s.buckets.end())
        return;   // 구독자 없음

    ++s.dispatchDepth;
    auto& subs = bucketIt->second.sorted;
    const size_t count = subs.size();   // 디스패치 중 삽입은 지연되므로 크기·저장소 불변
    for (size_t i = 0; i < count; ++i) {
        Impl::Subscription& sub = subs[i];
        if (sub.pendingRemoval)
            continue;
        if (sub.handler(payload, size))
            break;   // consumed — 후순위 구독자 전달 중단
    }
    --s.dispatchDepth;

    if (s.dispatchDepth == 0)
        s.ApplyPendingMutations();
}

void EventBus::EnqueueRaw(EventTypeId type, const void* payload, size_t size, EventChannel channel) {
    auto& s = *m_impl;
    const size_t channelIndex = static_cast<size_t>(channel);
    if (!MYE_VERIFY(channelIndex < static_cast<size_t>(EventChannel::Count),
                    "EventBus::Enqueue: 잘못된 채널"))
        return;
    if (!MYE_VERIFY(payload != nullptr && size > 0, "EventBus::Enqueue: 빈 페이로드"))
        return;

    Impl::QueuedEvent e;
    e.type = type;
    const auto* bytes = static_cast<const std::byte*>(payload);
    e.payload.assign(bytes, bytes + size);   // 복사는 락 밖에서 수행

    {
        std::lock_guard<std::mutex> lock(s.queueMutex);
        s.queues[channelIndex].push_back(std::move(e));
    }
}

void EventBus::Flush(EventChannel channel) {
    auto& s = *m_impl;
    MYE_ASSERT(s.IsMainThread(), "EventBus::Flush는 메인 스레드(메인 루프) 전용");
    const size_t channelIndex = static_cast<size_t>(channel);
    if (!MYE_VERIFY(channelIndex < static_cast<size_t>(EventChannel::Count),
                    "EventBus::Flush: 잘못된 채널"))
        return;

    // 스왑 후 배출 — Flush 중 Enqueue된 이벤트는 라이브 큐에 쌓여 다음 Flush로 이월
    std::vector<Impl::QueuedEvent> draining;
    {
        std::lock_guard<std::mutex> lock(s.queueMutex);
        draining.swap(s.queues[channelIndex]);
    }
    for (const auto& e : draining)
        PublishRaw(e.type, e.payload.data(), e.payload.size());
}

} // namespace mye
