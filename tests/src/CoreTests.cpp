// CoreTests.cpp — 해시·이벤트 버스·Expected 스캐폴드 검증
#include "TestFramework.h"

#include "mye/core/Base.h"
#include "mye/core/Events.h"
#include "mye/rhi/RhiTypes.h"

using namespace mye;

MYE_TEST(Fnv1a64KnownValues) {
    // FNV-1a 64 표준 벡터
    static_assert(HashFnv1a64("") == 14695981039346656037ull);
    static_assert(HashFnv1a64("a") == 0xaf63dc4c8601ec8cull);
    static_assert(HashFnv1a64("foobar") == 0x85944171f73967e8ull);
    MYE_EXPECT(HashFnv1a64("foobar") == 0x85944171f73967e8ull);
}

namespace {
struct TestEvent {
    MYE_EVENT(TestEvent);
    int value;
};
} // namespace

MYE_TEST(EventTypeIdIsNameHash) {
    static_assert(EventType<TestEvent>);
    static_assert(TestEvent::kEventTypeId == HashFnv1a64("TestEvent"));
    MYE_EXPECT(TestEvent::kEventTypeId != 0);
}

MYE_TEST(EventBusPublishSubscribe) {
    EventBus bus;
    int received = 0;
    auto handle = bus.Subscribe<TestEvent>([&](const TestEvent& e) {
        received = e.value;
        return false;   // not consumed
    });
    bus.Publish(TestEvent{.value = 42});
    MYE_EXPECT(received == 42);
    bus.Unsubscribe(handle);
    bus.Publish(TestEvent{.value = 7});
    MYE_EXPECT(received == 42);   // 해제 후 미수신
}

MYE_TEST(EventBusEnqueueFlush) {
    EventBus bus;
    int received = 0;
    ScopedSubscription sub{bus, bus.Subscribe<TestEvent>([&](const TestEvent& e) {
        received = e.value;
        return true;
    })};
    bus.Enqueue(TestEvent{.value = 5}, EventChannel::PostUpdate);
    MYE_EXPECT(received == 0);            // Flush 전 미배출
    bus.Flush(EventChannel::PostUpdate);
    MYE_EXPECT(received == 5);
}

MYE_TEST(ExpectedBasics) {
    Expected<int> ok = 3;
    MYE_EXPECT(ok.HasValue());
    MYE_EXPECT(ok.Value() == 3);

    Expected<int> bad = Error{"boom", 7};
    MYE_EXPECT(!bad);
    MYE_EXPECT(bad.GetError().code == 7);
}

MYE_TEST(RhiHandleValidity) {
    rhi::BufferHandle invalid;
    MYE_EXPECT(!invalid.IsValid());
    rhi::BufferHandle h{.index = 0, .gen = 1};
    MYE_EXPECT(h.IsValid());
    static_assert(rhi::kBindGroupSlotPerDraw == 3 && rhi::kMaxBindGroups == 4);
}
