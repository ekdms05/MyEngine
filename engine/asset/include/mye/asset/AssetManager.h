// mye/asset/AssetManager.h — 슬롯 테이블·임포터 레지스트리·동기 로드 (docs/04 §AssetManager)
//
// M0 범위: RegisterImporter + 동기 Load<T>(vpath/guid). 확장자→임포터 매핑으로
// 소스를 읽어 임포트하고 슬롯에 커밋한다. 비동기 Parse/Finalize·지연 GC·핫 리로드는
// M1. AssetHandle<T>가 참조하는 슬롯의 소유자.
#pragma once

#include "mye/asset/AssetGuid.h"
#include "mye/asset/AssetHandle.h"
#include "mye/asset/AsyncLoad.h"
#include "mye/asset/FileSystem.h"
#include "mye/asset/Importer.h"
#include "mye/core/Base.h"

#include <memory>
#include <string_view>

namespace mye::rhi { class IDevice; }
namespace mye { class JobSystem; class EventBus; }

namespace mye::asset {

using mye::JobSystem;
using mye::EventBus;

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

    // ---- 비동기 로딩 (M2-A: 계약 + 스텁) ----------------------------------
    // 비동기 로더 등록. 나중 등록이 내장을 오버라이드(로그 경고). (docs/04 §비동기)
    void RegisterAsyncLoader(std::unique_ptr<IAsyncAssetLoader> loader);

    // 잡 시스템·이벤트 버스 배선(비동기 로딩 필수). OnPostInitialize에서 SceneModule/
    // AssetModule이 EngineContext.Jobs()/Events()로 주입. 미배선 시 LoadAsync는
    // 동기 폴백(내부적으로 LoadSync 경유)으로 동작한다.
    void SetAsyncServices(JobSystem* jobs, EventBus* bus);

    // 핸들을 즉시 반환(State=Queued)하고 백그라운드로 로드한다:
    //   (IO 큐) 블롭 읽기 → (워커) Parse → (메인, RunOnMainThread) Finalize →
    //   슬롯 커밋(Loaded) → AssetLoadedEvent 발행. 하드 의존성까지 완료돼야 Loaded.
    template <typename T>
    AssetHandle<T> LoadAsync(std::string_view vpath, LoadPriority pri = LoadPriority::Normal);

    // 메인 루프에서 매 프레임 호출 — Finalize 큐 처리 + 완료 이벤트 발행 + 지연 GC(M1+).
    // (내부적으로 JobSystem::PumpMainThreadTasks도 트리거되도록 배선 가능.)
    void Update();

    // ---- AssetHandle<T>가 호출하는 슬롯 refcount·조회(내부 계약) ----
    AssetSlot* GetSlot(uint32_t index, uint32_t generation);   // gen 불일치 시 nullptr
    void RetainSlot(uint32_t index, uint32_t generation);
    void ReleaseSlot(uint32_t index, uint32_t generation);     // refcount 0 → 지연 GC 큐(M1)

    // ---- 핫 리로드(M3-A): AssetDatabase 가 소스 변경 시 호출 ----
    // 이미 로드된 vpath 를 재임포트하고, 성공 시 슬롯의 object 포인터만 in-place 스왑한다.
    //   (핸들은 index+gen 참조라 소비자는 자동으로 새 데이터를 본다 — generation 은 유지.)
    //   기존 객체는 슬롯을 만든 임포터의 DestroyWithDevice 로 파괴한다(GPU 반납 포함).
    //   반환: 스왑 성공하면 그 슬롯의 {guid,type} 를 out 인자로 채우고 true. 캐시 미스·임포트
    //   실패면 false(기존 데이터 유지 — 에러 시 엔티티만 멈추고 게임 지속 규약과 정렬).
    struct ReimportResult { AssetGuid guid; AssetTypeId type = 0; bool swapped = false; };
    ReimportResult ReimportPath(std::string_view vpath);

private:
    // 확장자 → 임포터 조회. 없으면 nullptr.
    IAssetImporter* FindImporterForPath(std::string_view path) const;
    // 슬롯 확보(신규 또는 재사용). 이미 로드된 vpath면 기존 슬롯 반환.
    uint32_t AcquireSlot(const AssetGuid& guid, AssetTypeId type);

    // 비동기 로딩 내부 헬퍼(메인 스레드 전용). reqRaw는 AsyncRequest* (익명 타입 은닉).
    void CommitAsync(void* reqRaw, void* objectOrNull, bool success);
    void RunBlocking(void* reqRaw);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mye::asset
