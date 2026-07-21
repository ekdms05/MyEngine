// mye/asset/AsyncLoad.h — 비동기 로딩 계약: Parse/Finalize 분리 (docs/04 §비동기 로딩)
//
// M2-A 범위(계약 동결 + 스텁): AssetManager::LoadAsync<T>는 핸들을 즉시 반환하고 상태를
//   Queued → Loading → Loaded|Failed로 전이시킨다. 임포터/로더는 두 단계로 분리한다:
//     - Parse   : 임의 워커 스레드. 블롭 디코드(순수 CPU). 하드 의존성 선언.
//     - Finalize: 메인 스레드(또는 free-threaded 옵트인). GPU 업로드 등 마무리.
//   흐름: Load → (IO 큐) 블롭 읽기 → (워커) Parse → (메인, RunOnMainThread) Finalize →
//         슬롯 커밋(State=Loaded) → AssetLoadedEvent 발행.
//
// 기존 동기 경로(IAssetImporter::Import, LoadSync)는 유지한다 — 이 헤더는 비동기 확장분만
//   추가한다. M3에서 refl::TypeId·IAssetLoader로 승격 시 이 인터페이스가 정본이 된다.
#pragma once

#include "mye/asset/AssetGuid.h"
#include "mye/asset/AssetHandle.h"
#include "mye/asset/FileSystem.h"
#include "mye/core/Base.h"

#include <cstddef>
#include <span>
#include <vector>

namespace mye::rhi { class IDevice; }

namespace mye::asset {

// 로드 우선순위 — IO/디코드 스케줄 순서에 반영(M2-A 스텁은 순서만 기록).
enum class LoadPriority : uint8_t { Low, Normal, High, Blocking };

// 워커 Parse의 산출물 — CPU측 중간 표현(디코드 결과). Finalize가 이를 소비해 최종 객체를 만든다.
// 소유권: Parse가 new한 불투명 포인터. Finalize 또는 실패 정리에서 임포터가 해제.
struct ParsedAsset {
    void* cpuData = nullptr;   // 임포터 내부 타입(예: DecodedImage*) — 임포터만 해석
};

// 로드 문맥 — Parse가 하드/소프트 의존성을 선언하는 통로. (M2-A 스텁: 수집만.)
struct LoadContext {
    AssetGuid                guid;
    std::vector<AssetRef>    hardDependencies;   // 이것들까지 Loaded여야 본 에셋 Loaded
    std::vector<AssetRef>    softReferences;     // 독립 로드, 폴백 허용

    void AddHardDependency(AssetRef ref) { hardDependencies.push_back(ref); }
    void AddSoftReference(AssetRef ref)  { softReferences.push_back(ref); }
};

// 비동기 로더 — 런타임 블롭(캐시/pak) → 메모리 객체. Parse(워커)/Finalize(메인) 분리.
// 확장 포인트: 플러그인·02(GPU 업로드 로더)가 구현하여 AssetManager에 등록.
class IAsyncAssetLoader {
public:
    virtual ~IAsyncAssetLoader() = default;
    virtual AssetTypeId Type() const = 0;

    // 워커 스레드: 블롭 파싱·디코드(순수 CPU), 하드 의존성 선언. GPU/디바이스 접근 금지.
    virtual Expected<ParsedAsset, Error> Parse(LoadContext& ctx,
                                               std::span<const std::byte> runtimeData) = 0;

    // 메인 스레드(기본): GPU 업로드 등 마무리. device는 nullptr일 수 있다(헤드리스/테스트).
    // 성공 시 out에 최종 에셋 객체(타입 T*, 소유권 이전). 슬롯에 커밋된다.
    virtual Expected<void*, Error> Finalize(LoadContext& ctx, ParsedAsset& parsed,
                                            rhi::IDevice* device) = 0;

    // Parse 성공 후 Finalize 실패/취소 시 중간 표현(ParsedAsset)을 해제.
    virtual void DiscardParsed(ParsedAsset& parsed) = 0;

    // 최종 에셋 객체 파괴(GPU 리소스 포함). device는 nullptr 가능.
    virtual void Unload(void* assetObject, rhi::IDevice* device) = 0;
};

// 로드 완료 알림 — 01 EventBus로 발행(하드 의존성 포함 완전 Loaded 시점).
// 파생 상태(GPU 뷰·아틀라스 UV 등)를 가진 소비자가 구독해 갱신한다.
struct AssetLoadedEvent {
    MYE_EVENT(AssetLoadedEvent);
    AssetGuid   guid;
    AssetTypeId type = 0;
    bool        success = false;   // false면 Failed
};

} // namespace mye::asset
