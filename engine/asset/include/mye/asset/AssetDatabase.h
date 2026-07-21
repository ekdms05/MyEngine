// mye/asset/AssetDatabase.h — 에디터 전용 에셋 DB·리임포트 파이프라인 (docs/04 §핫 리로드)
//
// 파일 워처 → 디바운스·리임포트 큐 → 재임포트 잡 → 핸들 슬롯 데이터 스왑 →
//   AssetReloadedEvent{guid,type} 발행 → 역의존 전파. M3-A는 이 파이프라인의 계약 +
//   골격을 동결한다(전체 스캔 인덱스·의존성 그래프 질의 골격 포함, 실배선은 구현 에이전트).
//
// AssetManager 와의 경계: DB가 리임포트를 수행하면 AssetManager 의 기존 슬롯(같은 GUID)을
//   찾아 새 객체로 데이터 포인터만 스왑한다(핸들은 슬롯 index+gen 참조라 소비자는 자동 갱신).
#pragma once

#include "mye/asset/AssetGuid.h"
#include "mye/asset/AssetHandle.h"
#include "mye/asset/FileWatcher.h"
#include "mye/core/Base.h"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mye { class EventBus; }

namespace mye::asset {

class AssetManager;
class IAssetImporter;

// 리로드 알림 — 01 EventBus로 발행. 파생 상태(GPU 뷰·아틀라스 UV·Lua 캐시)를 가진 소비자가
//   구독해 갱신한다(docs/04 §리로드 전파). AssetLoadedEvent(최초 로드)와 구분: 이건 재임포트.
struct AssetReloadedEvent {
    MYE_EVENT(AssetReloadedEvent);
    AssetGuid   guid;
    AssetTypeId type = 0;
};

// 파생 상태 소비자 계약 — 역의존 전파의 수신측(예: 아틀라스 재패킹, 머티리얼 UV 갱신).
//   AssetDatabase 가 위상 정렬 순서로 OnDependencyReloaded 를 배치 호출한다(중복 제거).
//   대부분의 소비자는 EventBus 구독으로 충분하나, 순서 의존 전파는 이 인터페이스를 등록한다.
class IReloadConsumer {
public:
    virtual ~IReloadConsumer() = default;
    // changedGuid 가 리로드됨 → 이 소비자의 파생 상태를 갱신. 반환 GUID들은 다시 전파 대상.
    virtual void OnDependencyReloaded(AssetGuid changedGuid,
                                      std::vector<AssetGuid>& outFurtherDirty) = 0;
};

class AssetDatabase {
public:
    MYE_SERVICE(AssetDatabase);

    // manager·bus 는 DB보다 오래 살아야 한다(비소유 참조). bus 는 nullptr 가능(테스트).
    AssetDatabase(AssetManager& manager, mye::EventBus* bus);
    ~AssetDatabase();
    AssetDatabase(const AssetDatabase&) = delete;
    AssetDatabase& operator=(const AssetDatabase&) = delete;

    // ---- 임포터 레지스트리(등록 대칭 해제 — 플러그인 언로드 안전) ----
    void RegisterImporter(std::unique_ptr<IAssetImporter> importer);
    void UnregisterImporter(const IAssetImporter* importer);

    // ---- 스캔·GUID 인덱스(에디터 콜드 스타트) ----
    // rootDir 트리를 스캔해 (경로↔GUID↔캐시 상태) 인메모리 인덱스를 구축. 영속화 없음.
    Expected<void, Error> ScanDirectory(std::string_view rootDir);
    AssetGuid   GuidFromPath(std::string_view assetPath) const;
    std::string PathFromGuid(AssetGuid guid) const;

    // ---- 핫 리로드 배선 ----
    // 워처를 붙이고 rootDir 감시 시작. 변경은 디바운스 큐에 쌓인다.
    Expected<void, Error> StartWatching(std::unique_ptr<IFileWatcher> watcher,
                                        std::string_view rootDir);

    // 에디터 틱에서 매 프레임 호출 — 디바운스된 변경을 소진: 리임포트 → 슬롯 스왑 →
    //   AssetReloadedEvent 발행 → 역의존 전파(위상 정렬·중복 제거).
    void ImportDirty();

    // 전체/타입별 강제 리임포트(임포터 버전 상승·수동 트리거).
    void ReimportAll(AssetTypeId type = 0);

    // ---- 의존성 그래프(07 브라우저·리팩터링 도구) ----
    void AddDependency(AssetGuid from, AssetGuid to);   // from 이 to 를 참조
    std::vector<AssetGuid> FindReferencers(AssetGuid guid) const;   // 역방향 간선

    // 순서 의존 파생 상태 소비자 등록(아틀라스 패커 등 내장 후처리도 이 경로).
    void RegisterReloadConsumer(AssetGuid producer, IReloadConsumer* consumer);
    void UnregisterReloadConsumer(IReloadConsumer* consumer);

    // ---- M3-A 배선 보조 ----
    // 워처가 준 OS 경로를 vpath 로 환원할 때 쓰는 마운트 스킴(기본 "assets"). "://" 제외.
    void SetVpathMount(std::string_view mount);
    // 경로↔GUID 인덱스에 항목을 등록(스캔·수동 배선·테스트용). vpath 는 정규화되어 저장.
    void MapPathGuid(std::string_view vpath, AssetGuid guid);

    // 역의존 전파를 직접 구동(수동 트리거·테스트). roots 에서 위상 BFS·중복 제거로 파생
    //   소비자(IReloadConsumer)를 배치 호출하고 새로 dirty 된 GUID 를 AssetReloadedEvent 로 알린다.
    //   보통은 ImportDirty/ReimportAll 내부에서 자동 호출된다.
    void PropagateReloads(const std::vector<AssetGuid>& roots);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mye::asset
