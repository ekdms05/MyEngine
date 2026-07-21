// MemoryTests.cpp — FrameAllocator 정렬·오버플로 정책·더블버퍼 스왑 검증 (docs/01 §메모리)
#include "TestFramework.h"

#include "mye/core/Memory.h"

#include <cstdint>

using namespace mye;

namespace {
bool IsAligned(const void* p, size_t alignment) {
    return (reinterpret_cast<uintptr_t>(p) % alignment) == 0;
}
} // namespace

MYE_TEST(FrameAllocBasicBump) {
    FrameAllocator alloc(1024);
    MYE_EXPECT(alloc.Capacity() == 1024);
    MYE_EXPECT(alloc.Used() == 0);

    void* a = alloc.Allocate(100, 16, MemoryTag::General);
    MYE_EXPECT(a != nullptr);
    MYE_EXPECT(IsAligned(a, 16));
    MYE_EXPECT(alloc.Used() == 100);

    void* b = alloc.Allocate(50, 16, MemoryTag::Renderer);
    MYE_EXPECT(b != nullptr);
    MYE_EXPECT(IsAligned(b, 16));
    // 두 번째 할당은 첫 할당 뒤(정렬 패딩 포함)로 겹치지 않는다.
    MYE_EXPECT(b >= static_cast<std::byte*>(a) + 100);
    MYE_EXPECT(alloc.Used() >= 150);
}

MYE_TEST(FrameAllocAlignmentPadding) {
    FrameAllocator alloc(4096);
    // 1바이트 할당으로 오프셋을 홀수로 만든 뒤, 큰 정렬 요청이 올바르게 올림되는지.
    void* one = alloc.Allocate(1, 1, MemoryTag::General);
    MYE_EXPECT(one != nullptr);
    void* aligned64 = alloc.Allocate(8, 64, MemoryTag::General);
    MYE_EXPECT(aligned64 != nullptr);
    MYE_EXPECT(IsAligned(aligned64, 64));
    void* aligned256 = alloc.Allocate(8, 256, MemoryTag::General);
    MYE_EXPECT(aligned256 != nullptr);
    MYE_EXPECT(IsAligned(aligned256, 256));
}

MYE_TEST(FrameAllocOverflowReturnsNull) {
    FrameAllocator alloc(256);
    void* big = alloc.Allocate(200, 16, MemoryTag::General);
    MYE_EXPECT(big != nullptr);
    // 남은 용량보다 큰 요청 → nullptr (예외 불사용 정책).
    void* overflow = alloc.Allocate(100, 16, MemoryTag::General);
    MYE_EXPECT(overflow == nullptr);
    // 실패 후에도 작은 요청은 여전히 성공(오프셋 미변경).
    void* small = alloc.Allocate(16, 16, MemoryTag::General);
    MYE_EXPECT(small != nullptr);
}

MYE_TEST(FrameAllocExactCapacity) {
    FrameAllocator alloc(128);
    void* full = alloc.Allocate(128, 1, MemoryTag::General);
    MYE_EXPECT(full != nullptr);
    MYE_EXPECT(alloc.Used() == 128);
    // 정확히 꽉 찬 상태에서 1바이트 더는 실패.
    MYE_EXPECT(alloc.Allocate(1, 1, MemoryTag::General) == nullptr);
}

MYE_TEST(FrameAllocZeroSize) {
    FrameAllocator alloc(128);
    // 0바이트 할당은 nullptr(계약: 의미 없는 할당은 실패로 취급, 오프셋 미변경).
    MYE_EXPECT(alloc.Allocate(0, 16, MemoryTag::General) == nullptr);
    MYE_EXPECT(alloc.Used() == 0);
}

MYE_TEST(FrameAllocSwapResetsAndDoubleBuffers) {
    FrameAllocator alloc(256);
    void* frame0 = alloc.Allocate(200, 16, MemoryTag::General);
    MYE_EXPECT(frame0 != nullptr);
    MYE_EXPECT(alloc.Used() == 200);

    // 스왑 → 반대편 버퍼로. Used는 0으로 리셋.
    alloc.SwapAndReset();
    MYE_EXPECT(alloc.Used() == 0);
    void* frame1 = alloc.Allocate(200, 16, MemoryTag::General);
    MYE_EXPECT(frame1 != nullptr);
    // 프레임 N과 N+1은 서로 다른 버퍼여야 한다(N 데이터가 N+1 동안 살아있음 = 2프레임 계약).
    MYE_EXPECT(frame1 != frame0);

    // 한 번 더 스왑 → 다시 frame0 버퍼 재사용(N+2에서 N 버퍼 재사용 = 계약).
    alloc.SwapAndReset();
    void* frame2 = alloc.Allocate(200, 16, MemoryTag::General);
    MYE_EXPECT(frame2 == frame0);
}

MYE_TEST(FrameAllocOverflowFlagResetsOnSwap) {
    FrameAllocator alloc(64);
    MYE_EXPECT(alloc.Allocate(100, 1, MemoryTag::General) == nullptr);  // 오버플로 로그 1회
    alloc.SwapAndReset();
    // 스왑 후 오버플로 억제 플래그가 리셋되어 새 프레임에서 다시 정상 할당 가능.
    MYE_EXPECT(alloc.Allocate(32, 1, MemoryTag::General) != nullptr);
}
