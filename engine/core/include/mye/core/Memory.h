// mye/core/Memory.h — 얼로케이터 인터페이스 + FrameAllocator (docs/01 §메모리)
//
// M1 토대 범위: 렌더러가 프레임 임시 데이터(렌더 커맨드·임시 문자열)를 담을
// FrameAllocator(더블 버퍼 선형)만 확정한다. ArenaAllocator/PoolAllocator/추적은 후속.
#pragma once

#include "mye/core/Base.h"

#include <cstddef>
#include <memory>

namespace mye {

enum class MemoryTag : uint8_t {
    General, Core, Renderer, Audio, Scene, Asset, Script, Editor, Plugin, Count
};

class IAllocator {
public:
    virtual ~IAllocator() = default;
    virtual void* Allocate(size_t size, size_t alignment, MemoryTag tag) = 0;
    virtual void  Free(void* ptr) = 0;   // 선형 얼로케이터는 no-op(일괄 해제)
};

// 더블 버퍼 선형 할당자.
//
// 계약(중요 — 병렬 구현 에이전트 동결):
//  - 프레임 N의 할당은 Tick 시작 시 SwapAndReset() 호출로 버퍼가 스왑되기 전까지 유효.
//  - **2프레임 참조 금지**: 프레임 N에서 받은 포인터를 프레임 N+2 이후 접근하면 UB.
//    (더블 버퍼이므로 N과 N+1은 서로 다른 버퍼 → N+1 동안에는 N의 데이터가 살아있음.
//     N+2에서 N의 버퍼가 리셋되어 재사용된다.) 프레임 경계를 넘겨 살릴 데이터는
//    다른 얼로케이터(Arena/System)로 복사할 것.
//  - Free()는 no-op. 해제는 오직 SwapAndReset()의 일괄 리셋으로만 일어난다.
//  - 용량 초과 시 nullptr 반환(예외 불사용) + 로그.
class FrameAllocator final : public IAllocator {
public:
    MYE_SERVICE(FrameAllocator);   // EngineContext 서비스로 노출(프레임 임시 할당 소비자용)

    explicit FrameAllocator(size_t capacityPerBuffer);
    ~FrameAllocator() override;
    FrameAllocator(const FrameAllocator&) = delete;
    FrameAllocator& operator=(const FrameAllocator&) = delete;

    void* Allocate(size_t size, size_t alignment, MemoryTag tag) override;
    void  Free(void* ptr) override;      // no-op

    // 프레임 경계(메인 루프가 Tick 시작 시 호출): 활성 버퍼를 반대편으로 스왑하고
    // 새 활성 버퍼의 오프셋을 0으로 리셋한다.
    void SwapAndReset();

    size_t Capacity() const;             // 버퍼당 용량
    size_t Used() const;                 // 현재 활성 버퍼 사용량

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mye
