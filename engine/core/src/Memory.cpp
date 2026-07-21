// mye/core/Memory.cpp — FrameAllocator: 더블 버퍼 선형(범프) 할당자 구현 (docs/01 §메모리)
//
// 구조: 크기 capacityPerBuffer의 버퍼 2개. 활성 버퍼에서 정렬 범프 할당만 수행.
// SwapAndReset()이 활성 버퍼를 토글하고 새 활성 버퍼의 오프셋을 0으로 되돌린다.
// 계약: Free는 no-op, 용량 초과 시 nullptr+로그, 2프레임 참조 금지(버퍼 2개 = N/N+1만 공존).
#include "mye/core/Memory.h"
#include "mye/core/Assert.h"
#include "mye/core/Log.h"

#include <cstdlib>
#include <new>

namespace mye {

namespace {

// 2의 거듭제곱 정렬로 올림. alignment는 반드시 2의 거듭제곱이어야 한다.
constexpr size_t AlignUp(size_t value, size_t alignment) {
    return (value + (alignment - 1)) & ~(alignment - 1);
}

constexpr bool IsPowerOfTwo(size_t v) { return v != 0 && (v & (v - 1)) == 0; }

// 버퍼 자체를 강하게 정렬해 확보한다. 오프셋 기반 정렬(AlignUp)은 버퍼 base가 요청 정렬 이상으로
// 정렬돼 있을 때만 절대 주소 정렬을 보장하므로, base를 max 지원 정렬로 잡는다.
// kMaxSupportedAlignment까지의 Allocate(alignment) 요청은 절대 주소 정렬을 보장한다.
constexpr size_t kBufferAlignment = 256;

std::byte* AllocateAlignedBuffer(size_t size) {
    if (size == 0) return nullptr;
#if defined(_MSC_VER)
    return static_cast<std::byte*>(::_aligned_malloc(size, kBufferAlignment));
#else
    // size는 alignment의 배수여야 한다(std::aligned_alloc 요구).
    return static_cast<std::byte*>(::std::aligned_alloc(kBufferAlignment, AlignUp(size, kBufferAlignment)));
#endif
}

void FreeAlignedBuffer(std::byte* ptr) {
    if (ptr == nullptr) return;
#if defined(_MSC_VER)
    ::_aligned_free(ptr);
#else
    ::std::free(ptr);
#endif
}

} // namespace

struct FrameAllocator::Impl {
    size_t     capacity = 0;      // 버퍼당 용량(바이트)
    std::byte* buffers[2] = {nullptr, nullptr};
    size_t     offset = 0;        // 현재 활성 버퍼의 사용량(바이트)
    int        active = 0;        // 활성 버퍼 인덱스(0/1)
    bool       overflowed = false; // 이번 프레임 오버플로 로그 1회 억제용
};

FrameAllocator::FrameAllocator(size_t capacityPerBuffer)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->capacity = capacityPerBuffer;
    m_impl->buffers[0] = AllocateAlignedBuffer(capacityPerBuffer);
    m_impl->buffers[1] = AllocateAlignedBuffer(capacityPerBuffer);
    if (capacityPerBuffer != 0 && (m_impl->buffers[0] == nullptr || m_impl->buffers[1] == nullptr)) {
        MYE_LOG_FATAL("Memory", "FrameAllocator 버퍼 확보 실패 (capacityPerBuffer={})", capacityPerBuffer);
    }
}

FrameAllocator::~FrameAllocator() {
    if (m_impl) {
        FreeAlignedBuffer(m_impl->buffers[0]);
        FreeAlignedBuffer(m_impl->buffers[1]);
    }
}

void* FrameAllocator::Allocate(size_t size, size_t alignment, MemoryTag /*tag*/) {
    if (alignment == 0) alignment = alignof(std::max_align_t);
    MYE_ASSERT(IsPowerOfTwo(alignment), "FrameAllocator: alignment는 2의 거듭제곱이어야 한다");
    // 버퍼 base 정렬(kBufferAlignment)보다 강한 요청은 오프셋 정렬만으론 절대 주소를 보장 못 함.
    MYE_ASSERT(alignment <= kBufferAlignment,
               "FrameAllocator: 지원 최대 정렬(256B) 초과 요청");
    if (size == 0) return nullptr;

    std::byte* base = m_impl->buffers[m_impl->active];
    if (base == nullptr) return nullptr;

    const size_t alignedOffset = AlignUp(m_impl->offset, alignment);
    // 오버플로 안전: alignedOffset + size가 capacity를 넘거나 산술 오버플로면 실패.
    if (alignedOffset > m_impl->capacity || size > m_impl->capacity - alignedOffset) {
        if (!m_impl->overflowed) {   // 프레임당 1회만 로그 (스팸 방지)
            MYE_LOG_ERROR("Memory",
                          "FrameAllocator 용량 초과: 요청 {}B(align {}), 활성 버퍼 잔여 {}B/{}B",
                          size, alignment, m_impl->capacity - m_impl->offset, m_impl->capacity);
            m_impl->overflowed = true;
        }
        return nullptr;
    }

    void* result = base + alignedOffset;
    m_impl->offset = alignedOffset + size;
    return result;
}

void FrameAllocator::Free(void* /*ptr*/) { /* no-op: 일괄 리셋(SwapAndReset) 전용 */ }

void FrameAllocator::SwapAndReset() {
    m_impl->active = 1 - m_impl->active;   // 버퍼 토글: N+2에서 N의 버퍼가 재사용됨
    m_impl->offset = 0;
    m_impl->overflowed = false;
}

size_t FrameAllocator::Capacity() const { return m_impl->capacity; }
size_t FrameAllocator::Used() const { return m_impl->offset; }

} // namespace mye
