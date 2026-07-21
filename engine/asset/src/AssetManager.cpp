// mye/asset/AssetManager.cpp — 슬롯 테이블·임포터 레지스트리·동기 로드 (docs/04 §AssetManager)
//
// M1 슬림 범위:
//  - RegisterImporter: 확장자 → 임포터 매핑(나중 등록이 오버라이드 + 경고).
//  - LoadSync<T>(vpath): 캐시 확인 → VFS ReadAll → 임포터 Import → 슬롯 커밋 → Loaded 핸들.
//  - 슬롯 테이블: index+generation 간접. refCount 0이면 즉시 언로드(지연 GC는 M1-B).
//  - vpath→슬롯 캐시로 중복 로드 공유(같은 경로 재요청 시 refcount만 증가).
//
// GPU 리소스 파괴 경계: 임포터의 Destroy(void*)는 device를 모르므로 GPU 핸들을 못 푼다.
// AssetManager가 device를 소유하므로, 언로드 시 Texture 타입에 한해 GPU 텍스처/샘플러를
// device.Destroy로 반납한 뒤 임포터 Destroy를 호출한다(M1 슬림: 타입별 특례).
#include "mye/asset/AssetManager.h"
#include "mye/asset/Texture.h"

#include "mye/core/Log.h"
#include "mye/rhi/Rhi.h"

#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace mye::asset {

namespace {

// 경로에서 소문자 확장자(".png" 등)를 추출. 없으면 빈 문자열.
std::string ExtractExtLower(std::string_view path) {
    // 마지막 '/' 이후에서 '.'을 찾는다(디렉터리의 '.'은 무시).
    size_t slash = path.find_last_of('/');
    size_t searchFrom = (slash == std::string_view::npos) ? 0 : slash + 1;
    size_t dot = path.find_last_of('.');
    if (dot == std::string_view::npos || dot < searchFrom) return {};
    std::string ext(path.substr(dot));
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return ext;
}

} // namespace

struct AssetManager::Impl {
    VirtualFileSystem* vfs = nullptr;
    rhi::IDevice* device = nullptr;

    std::vector<std::unique_ptr<IAssetImporter>> importers;
    // 확장자(".png") → importers 인덱스.
    std::unordered_map<std::string, size_t> extToImporter;

    // AssetSlot은 std::atomic 멤버(이동 불가) 때문에 vector 재할당이 불가하다.
    // deque는 요소 주소가 안정적이고 push_back 시 기존 요소를 이동하지 않으므로 적합하다.
    std::deque<AssetSlot> slots;
    std::vector<uint32_t> freeSlots;             // 재사용 대기 인덱스
    // vpath → 슬롯 인덱스(로드 공유). 언로드 시 제거.
    std::unordered_map<std::string, uint32_t> pathToSlot;
    // 슬롯 인덱스 → 그 슬롯을 만든 임포터(언로드 시 Destroy 호출용). null이면 파괴 스킵.
    std::vector<IAssetImporter*> slotImporter;

    IAssetImporter* FindImporterForExt(const std::string& ext) {
        auto it = extToImporter.find(ext);
        return it == extToImporter.end() ? nullptr : importers[it->second].get();
    }

    // 슬롯 확보(신규 또는 free 재사용). 초기 상태 Unloaded.
    uint32_t AllocSlot(const AssetGuid& guid, AssetTypeId type) {
        uint32_t idx;
        if (!freeSlots.empty()) {
            idx = freeSlots.back();
            freeSlots.pop_back();
            AssetSlot& s = slots[idx];
            s.guid = guid;
            s.type = type;
            s.object = nullptr;
            s.refCount.store(0);
            // generation은 재사용 시 이미 증가되어 있음(Free에서 bump).
            s.state = AssetState::Unloaded;
            s.pinned = false;
        } else {
            idx = static_cast<uint32_t>(slots.size());
            slots.emplace_back();
            slotImporter.emplace_back(nullptr);
            AssetSlot& s = slots[idx];
            s.guid = guid;
            s.type = type;
            // generation 기본 1(헤더 초기값).
        }
        return idx;
    }

    // 슬롯의 에셋 객체를 파괴하고 슬롯을 free 목록으로 되돌린다(generation bump).
    void DestroySlot(uint32_t idx) {
        AssetSlot& s = slots[idx];
        if (s.object) {
            DestroyAssetObject(idx);
            s.object = nullptr;
        }
        // pathToSlot에서 이 슬롯을 참조하는 항목 제거.
        for (auto it = pathToSlot.begin(); it != pathToSlot.end();) {
            if (it->second == idx) {
                it = pathToSlot.erase(it);
            } else {
                ++it;
            }
        }
        s.state = AssetState::Unloaded;
        s.generation.fetch_add(1);   // dangling 핸들 검출
        slotImporter[idx] = nullptr;
        freeSlots.push_back(idx);
    }

    // 타입별 GPU 리소스 반납 + 임포터 Destroy.
    void DestroyAssetObject(uint32_t idx) {
        AssetSlot& s = slots[idx];
        // Texture 타입이면 GPU 핸들을 device로 반납(임포터는 device를 모른다).
        if (s.type == Texture::kAssetTypeId && device) {
            auto* tex = static_cast<Texture*>(s.object);
            if (tex->gpuTexture.IsValid()) device->Destroy(tex->gpuTexture);
            if (tex->sampler.IsValid()) device->Destroy(tex->sampler);
        }
        if (IAssetImporter* imp = slotImporter[idx]) {
            imp->Destroy(s.object);
        }
    }
};

AssetManager::AssetManager(VirtualFileSystem& vfs, rhi::IDevice* device)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->vfs = &vfs;
    m_impl->device = device;
}

AssetManager::~AssetManager() {
    // 남은 로드 객체 정리(테스트·셧다운에서 릭 방지).
    for (uint32_t i = 0; i < m_impl->slots.size(); ++i) {
        if (m_impl->slots[i].object) {
            m_impl->DestroyAssetObject(i);
            m_impl->slots[i].object = nullptr;
        }
    }
}

void AssetManager::RegisterImporter(std::unique_ptr<IAssetImporter> importer) {
    if (!importer) return;
    const size_t idx = m_impl->importers.size();
    IAssetImporter* raw = importer.get();
    m_impl->importers.push_back(std::move(importer));

    for (std::string_view extView : raw->SourceExtensions()) {
        std::string ext(extView);
        for (char& c : ext) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        auto [it, inserted] = m_impl->extToImporter.insert_or_assign(ext, idx);
        if (!inserted) {
            MYE_LOG_WARN("Asset", "RegisterImporter: '{}' overrides existing importer for '{}'",
                         raw->Name(), ext);
        } else {
            MYE_LOG_INFO("Asset", "RegisterImporter: '{}' handles '{}'", raw->Name(), ext);
        }
    }
}

IAssetImporter* AssetManager::FindImporterForPath(std::string_view path) const {
    std::string ext = ExtractExtLower(path);
    if (ext.empty()) return nullptr;
    return m_impl->FindImporterForExt(ext);
}

uint32_t AssetManager::AcquireSlot(const AssetGuid& guid, AssetTypeId type) {
    return m_impl->AllocSlot(guid, type);
}

AssetSlot* AssetManager::GetSlot(uint32_t index, uint32_t generation) {
    if (index >= m_impl->slots.size()) return nullptr;
    AssetSlot& s = m_impl->slots[index];
    if (s.generation.load() != generation) return nullptr;   // 재사용된 슬롯 — dangling
    return &s;
}

void AssetManager::RetainSlot(uint32_t index, uint32_t generation) {
    AssetSlot* s = GetSlot(index, generation);
    if (s) s->refCount.fetch_add(1);
}

void AssetManager::ReleaseSlot(uint32_t index, uint32_t generation) {
    AssetSlot* s = GetSlot(index, generation);
    if (!s) return;
    const int32_t prev = s->refCount.fetch_sub(1);
    if (prev <= 1 && !s->pinned) {
        // refCount 0 도달 — M1 슬림은 즉시 언로드(지연 GC 예산화는 M1-B).
        m_impl->DestroySlot(index);
    }
}

template <typename T>
AssetHandle<T> AssetManager::LoadSync(std::string_view vpath) {
    // 캐시 히트: 기존 슬롯의 핸들을 반환(refcount는 핸들 생성자에서 증가).
    std::string key(vpath);
    if (auto it = m_impl->pathToSlot.find(key); it != m_impl->pathToSlot.end()) {
        const uint32_t idx = it->second;
        return AssetHandle<T>(this, idx, m_impl->slots[idx].generation.load());
    }

    IAssetImporter* importer = FindImporterForPath(vpath);
    if (!importer) {
        MYE_LOG_ERROR("Asset", "LoadSync: no importer for '{}'", key);
        // Failed 슬롯을 만들어 상태 조회가 가능한 핸들을 돌려준다.
        const uint32_t idx = AcquireSlot(AssetGuid::Generate(), T::kAssetTypeId);
        m_impl->slots[idx].state = AssetState::Failed;
        m_impl->pathToSlot.emplace(key, idx);
        return AssetHandle<T>(this, idx, m_impl->slots[idx].generation.load());
    }

    auto blob = m_impl->vfs->ReadAll(vpath);
    if (!blob) {
        MYE_LOG_ERROR("Asset", "LoadSync: read failed '{}': {}", key, blob.GetError().message);
        const uint32_t idx = AcquireSlot(AssetGuid::Generate(), importer->ProducedType());
        m_impl->slots[idx].state = AssetState::Failed;
        m_impl->pathToSlot.emplace(key, idx);
        return AssetHandle<T>(this, idx, m_impl->slots[idx].generation.load());
    }

    ImportContext ctx;
    ctx.sourcePath = vpath;
    ctx.sourceBytes = std::move(blob.Value());
    ctx.device = m_impl->device;
    ctx.settings = nullptr;   // M1 슬림: .meta settings 주입은 M1-B(임포트 설정 UI와 함께).

    auto imported = importer->Import(ctx);

    const uint32_t idx = AcquireSlot(AssetGuid::Generate(), importer->ProducedType());
    m_impl->pathToSlot.emplace(key, idx);
    AssetSlot& slot = m_impl->slots[idx];

    if (!imported) {
        MYE_LOG_ERROR("Asset", "LoadSync: import failed '{}': {}", key,
                      imported.GetError().message);
        slot.state = AssetState::Failed;
        return AssetHandle<T>(this, idx, slot.generation.load());
    }

    slot.object = imported.Value();
    slot.state = AssetState::Loaded;
    m_impl->slotImporter[idx] = importer;
    MYE_LOG_INFO("Asset", "LoadSync: loaded '{}' (slot {})", key, idx);
    return AssetHandle<T>(this, idx, slot.generation.load());
}

// 명시적 인스턴스화 — M1 소비 타입.
template AssetHandle<Texture> AssetManager::LoadSync<Texture>(std::string_view);

} // namespace mye::asset
