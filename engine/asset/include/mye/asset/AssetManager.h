// mye/asset/AssetManager.h — 슬롯 테이블·임포터 레지스트리·동기 로드 (docs/04 §AssetManager)
//
// M0 범위: RegisterImporter + 동기 Load<T>(vpath/guid). 확장자→임포터 매핑으로
// 소스를 읽어 임포트하고 슬롯에 커밋한다. 비동기 Parse/Finalize·지연 GC·핫 리로드는
// M1. AssetHandle<T>가 참조하는 슬롯의 소유자.
#pragma once

#include "mye/asset/AssetGuid.h"
#include "mye/asset/AssetHandle.h"
#include "mye/asset/FileSystem.h"
#include "mye/asset/Importer.h"
#include "mye/core/Base.h"

#include <memory>
#include <string_view>

namespace mye::rhi { class IDevice; }

namespace mye::asset {

class AssetManager {
public:
    MYE_SERVICE(AssetManager);

    // vfs·device는 AssetManager보다 오래 살아야 한다(비소유 참조).
    AssetManager(VirtualFileSystem& vfs, rhi::IDevice* device);
    ~AssetManager();
    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    // 확장자(SourceExtensions)로 임포터를 등록. 나중 등록이 내장을 오버라이드(로그 경고).
    void RegisterImporter(std::unique_ptr<IAssetImporter> importer);

    // 동기 로드 — 부트스트랩·에디터·단위 테스트용. 실패 시 Failed 상태 핸들.
    template <typename T> AssetHandle<T> LoadSync(std::string_view vpath);

    // ---- AssetHandle<T>가 호출하는 슬롯 refcount·조회(내부 계약) ----
    AssetSlot* GetSlot(uint32_t index, uint32_t generation);   // gen 불일치 시 nullptr
    void RetainSlot(uint32_t index, uint32_t generation);
    void ReleaseSlot(uint32_t index, uint32_t generation);     // refcount 0 → 지연 GC 큐(M1)

private:
    // 확장자 → 임포터 조회. 없으면 nullptr.
    IAssetImporter* FindImporterForPath(std::string_view path) const;
    // 슬롯 확보(신규 또는 재사용). 이미 로드된 vpath면 기존 슬롯 반환.
    uint32_t AcquireSlot(const AssetGuid& guid, AssetTypeId type);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mye::asset
