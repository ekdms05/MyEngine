// mye/asset/MeshImporter.h — glTF 정적 메시 임포터 (docs/04 §임포터 라인업 glTF)
//
// cgltf(third_party/cgltf) 단일 헤더로 .gltf/.glb를 파싱한다. M2-C 범위: 정적 메시만
// (POSITION/NORMAL/TEXCOORD0, 인덱스). 스켈레탈·머티리얼·애니메이션은 M6+ 확장.
// 좌표 규약(왼손·+Y업·+Z전방)으로 변환한다 — Mesh.h 상단 주석 참조.
//
// 경로(TextureImporter와 동형):
//   동기 : IAssetImporter::Import — 소스 바이트 → cgltf 파싱 → MeshData → rhi 버퍼 업로드 → Mesh*
//   비동기: IAsyncAssetLoader     — Parse(워커: cgltf 파싱, 순수 CPU) / Finalize(메인: 버퍼 업로드)
#pragma once

#include "mye/asset/AssetGuid.h"
#include "mye/asset/AsyncLoad.h"
#include "mye/asset/Importer.h"
#include "mye/asset/Mesh.h"
#include "mye/core/Base.h"

#include <span>
#include <string_view>

namespace mye::rhi { class IDevice; }

namespace mye::asset {

// glTF 임포트 설정(.meta settings로 직렬화, M1+). 정적 메시 MVP는 거의 옵션이 없다.
struct MeshImportSettings {
    float scale = 1.0f;              // 유닛 스케일(glTF 미터 → 엔진 유닛). 기본 1:1.
    bool  flipUv = false;            // 일부 파이프라인이 좌하단 UV를 쓰는 경우(기본 false = glTF 규약)
    bool  recomputeNormalsIfMissing = true;  // NORMAL 누락 시 평면 법선 생성
};

// glTF 임포터 — cgltf 소비. rhi 버퍼(정점·인덱스) 생성까지 수행(동기 경로).
class MeshImporter final : public IAssetImporter {
public:
    const char* Name() const override { return "MeshImporter"; }
    std::span<const std::string_view> SourceExtensions() const override;  // {".gltf", ".glb"}
    uint32_t Version() const override { return 1; }
    AssetTypeId ProducedType() const override { return Mesh::kAssetTypeId; }

    Expected<void*, Error> Import(ImportContext& ctx) override;
    void Destroy(void* assetObject) override;
    // 정점·인덱스 GPU 버퍼를 device로 반납한 뒤 CPU Mesh 객체를 해제.
    void DestroyWithDevice(void* assetObject, rhi::IDevice* device) override;

    // 순수 파싱(GPU 없이 단위 테스트 가능). cgltf로 .gltf/.glb 바이트를 파싱해 CPU MeshData 산출.
    //   - 왼손 변환(Z 반전 + 와인딩 뒤집기), 통합 정점 버퍼, 32bit 인덱스, 서브메시별 바운딩.
    //   - 외부 .bin 참조는 미지원(임베드 .glb / data-URI만). 스켈레탈 메시는 정적 부분만 취함.
    // 구현: src/MeshImporter.cpp
    static Expected<MeshData, Error> ParseGltf(std::span<const std::byte> gltfBytes,
                                               const MeshImportSettings& settings);

    // CPU MeshData → rhi 정점/인덱스 버퍼 생성 → Mesh 채움(핸들·서브메시·바운딩).
    static Expected<Mesh, Error> Upload(rhi::IDevice& device, MeshData data);
};

// 비동기 Mesh 로더 — Parse/Finalize 분리(04 §비동기 로딩).
//   Parse   : 워커. glTF 바이트 → MeshData(순수 CPU). ParsedAsset.cpuData = new MeshData*.
//   Finalize: 메인. MeshData → rhi Upload → new Mesh*(소유권 이전). device=nullptr면 CPU 스텁.
class MeshAsyncLoader final : public IAsyncAssetLoader {
public:
    AssetTypeId Type() const override { return Mesh::kAssetTypeId; }

    Expected<ParsedAsset, Error> Parse(LoadContext& ctx,
                                       std::span<const std::byte> runtimeData) override;
    Expected<void*, Error> Finalize(LoadContext& ctx, ParsedAsset& parsed,
                                    rhi::IDevice* device) override;
    void DiscardParsed(ParsedAsset& parsed) override;
    void Unload(void* assetObject, rhi::IDevice* device) override;
};

} // namespace mye::asset
