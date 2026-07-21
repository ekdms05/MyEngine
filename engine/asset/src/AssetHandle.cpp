// mye/asset/AssetHandle.cpp — AssetHandle<T> refcount 위임 스텁 + 명시적 인스턴스화
//
// 슬롯 조작은 전부 AssetManager로 위임한다(슬롯 소유자). M0 스텁은 위임 배관만
// 세우고 실제 refcount 갱신은 AssetManager 구현(M1)에 맡긴다.
#include "mye/asset/AssetHandle.h"
#include "mye/asset/AssetManager.h"
#include "mye/asset/Texture.h"
#include "mye/asset/Mesh.h"

namespace mye::asset {

template <typename T>
AssetHandle<T>::AssetHandle(AssetManager* mgr, uint32_t slot, uint32_t gen)
    : m_manager(mgr), m_slot(slot), m_generation(gen) {
    Retain();
}

template <typename T>
AssetHandle<T>::AssetHandle(const AssetHandle& o)
    : m_manager(o.m_manager), m_slot(o.m_slot), m_generation(o.m_generation) {
    Retain();
}

template <typename T>
AssetHandle<T>::AssetHandle(AssetHandle&& o) noexcept
    : m_manager(o.m_manager), m_slot(o.m_slot), m_generation(o.m_generation) {
    o.m_manager = nullptr;
}

template <typename T>
AssetHandle<T>& AssetHandle<T>::operator=(const AssetHandle& o) {
    if (this != &o) {
        Release();
        m_manager = o.m_manager; m_slot = o.m_slot; m_generation = o.m_generation;
        Retain();
    }
    return *this;
}

template <typename T>
AssetHandle<T>& AssetHandle<T>::operator=(AssetHandle&& o) noexcept {
    if (this != &o) {
        Release();
        m_manager = o.m_manager; m_slot = o.m_slot; m_generation = o.m_generation;
        o.m_manager = nullptr;
    }
    return *this;
}

template <typename T>
AssetHandle<T>::~AssetHandle() { Release(); }

template <typename T>
void AssetHandle<T>::Retain() {
    if (m_manager) m_manager->RetainSlot(m_slot, m_generation);
}

template <typename T>
void AssetHandle<T>::Release() {
    if (m_manager) m_manager->ReleaseSlot(m_slot, m_generation);
    m_manager = nullptr;
}

template <typename T>
T* AssetHandle<T>::Get() const {
    if (!m_manager) return nullptr;
    AssetSlot* s = m_manager->GetSlot(m_slot, m_generation);
    if (!s || s->state != AssetState::Loaded) return nullptr;
    return static_cast<T*>(s->object);
}

template <typename T>
AssetState AssetHandle<T>::State() const {
    if (!m_manager) return AssetState::Unloaded;
    AssetSlot* s = m_manager->GetSlot(m_slot, m_generation);
    return s ? s->state : AssetState::Unloaded;
}

template <typename T>
AssetGuid AssetHandle<T>::Guid() const {
    if (!m_manager) return {};
    AssetSlot* s = m_manager->GetSlot(m_slot, m_generation);
    return s ? s->guid : AssetGuid{};
}

template <typename T> void AssetHandle<T>::Pin() { /* TODO(M1): 지연 GC 면제 */ }
template <typename T> void AssetHandle<T>::Unpin() { /* TODO(M1) */ }

// 명시적 인스턴스화 — M0 소비 타입(추가 시 여기 등록).
template class AssetHandle<Texture>;
template class AssetHandle<Mesh>;   // M2-C: bridge_demo가 LoadSync<Mesh> 소비

} // namespace mye::asset
