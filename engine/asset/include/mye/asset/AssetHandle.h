// mye/asset/AssetHandle.h — 강한 참조 핸들 + 슬롯 테이블 (docs/04 §식별자·핸들)
//
// 핸들은 에셋 데이터를 직접 가리키지 않고 간접 슬롯(refcount·상태·데이터 포인터)을
// 가리킨다. 리로드(M1)는 슬롯의 데이터 포인터만 스왑하므로 소비자는 자동으로 새
// 데이터를 본다. M0 범위: 동기 로드·refcount·Get. 지연 GC·Pin은 M1에서 채운다.
#pragma once

#include "mye/asset/AssetGuid.h"

#include <atomic>
#include <cstdint>

namespace mye::asset {

enum class AssetState : uint8_t { Unloaded, Queued, Loading, Loaded, Failed };

// 간접 테이블 슬롯(AssetManager 소유). 핸들은 이 슬롯을 index+gen으로 참조한다.
struct AssetSlot {
    AssetGuid            guid;
    AssetTypeId          type = 0;
    void*                object = nullptr;      // 로드된 에셋 객체(타입 T*)
    std::atomic<int32_t> refCount{0};
    std::atomic<uint32_t> generation{1};        // 슬롯 재사용 dangling 검출
    AssetState           state = AssetState::Unloaded;
    bool                 pinned = false;
};

class AssetManager;

// 강한 참조. 복사 = refcount+1, 소멸 = refcount-1.
template <typename T>
class AssetHandle {
public:
    AssetHandle() = default;
    AssetHandle(AssetManager* mgr, uint32_t slot, uint32_t gen);
    AssetHandle(const AssetHandle& o);
    AssetHandle(AssetHandle&& o) noexcept;
    AssetHandle& operator=(const AssetHandle& o);
    AssetHandle& operator=(AssetHandle&& o) noexcept;
    ~AssetHandle();

    T*         Get() const;             // Loaded가 아니면 nullptr
    AssetState State() const;
    AssetGuid  Guid() const;
    bool       IsLoaded() const { return State() == AssetState::Loaded; }
    bool       IsValid() const { return m_manager != nullptr; }

    void Pin();                         // 지연 GC 면제(상주) — M1
    void Unpin();

private:
    void Retain();
    void Release();

    AssetManager* m_manager = nullptr;
    uint32_t      m_slot = 0;
    uint32_t      m_generation = 0;
};

} // namespace mye::asset
