// mye/asset/Mesh.h — Mesh 에셋 타입 (docs/04 §임포터 라인업 glTF, docs/02 하이브리드 3D)
//
// 04는 GPU를 모르지 않되(같은 L2), Mesh 에셋은 rhi Buffer 핸들(정점·인덱스)을 보유한다.
// MeshImporter(cgltf)가 .gltf/.glb를 파싱해 CPU 정점/인덱스를 만들고, Finalize 단계에서
// rhi::IDevice::CreateBuffer로 GPU에 올린 뒤 핸들을 이 구조체에 심는다.
//
// 좌표 규약(엔진 확정): 왼손·+Y업·+Z전방·row-major·v*M. glTF는 오른손·+Y업·-Z전방이므로
// MeshImporter가 임포트 시 Z를 반전(위치 z·법선 z 부호 반전 + 삼각형 와인딩 뒤집기)해
// 왼손 좌표계로 변환한다. 텍스처 좌표(UV)는 glTF와 동일하게 좌상단 원점을 유지한다.
//
// 렌더(02 hybrid) 정합: 아래 MeshVertex 레이아웃이 02가 3D 포워드 패스에서 바인딩할
// 정점 포맷의 정본이다. apiForIntegration 참조.
#pragma once

#include "mye/asset/AssetGuid.h"
#include "mye/core/Math.h"
#include "mye/rhi/RhiTypes.h"

#include <cstdint>
#include <span>
#include <vector>

namespace mye::asset {

// ---------------------------------------------------------------------------
// 정점 포맷 — 02 3D 포워드 패스가 바인딩하는 정본 레이아웃.
// interleaved, 슬롯 0. HLSL 시맨틱: POSITION / NORMAL / TEXCOORD0.
//   POSITION : float3 (RGB32Float, offset 0)   — 왼손 좌표(모델 로컬)
//   NORMAL   : float3 (RGB32Float, offset 12)  — 왼손 좌표, 정규화
//   TEXCOORD0: float2 (RG32Float,  offset 24)  — glTF UV(좌상단 원점)
// stride = 32 바이트.
// ---------------------------------------------------------------------------
struct MeshVertex {
    Vec3 position{};   // POSITION
    Vec3 normal{};     // NORMAL
    Vec2 uv{};         // TEXCOORD0
};
static_assert(sizeof(MeshVertex) == 32, "MeshVertex는 32바이트 interleaved 레이아웃이어야 한다");

// 02가 파이프라인 VertexLayoutDesc를 구성할 때 참조하는 정점 속성 서술.
// 반환 span은 정적 저장 수명(프로세스 전역) — 호출부가 그대로 파이프라인에 넘길 수 있다.
std::span<const rhi::VertexAttribute> MeshVertexAttributes();
inline constexpr uint32_t kMeshVertexStride = static_cast<uint32_t>(sizeof(MeshVertex));

// ---------------------------------------------------------------------------
// 바운딩 — 축 정렬 박스(AnchorBiased 깊이 매핑·프러스텀 컬링·에디터 기즈모용).
// ---------------------------------------------------------------------------
struct Aabb {
    Vec3 min{};
    Vec3 max{};
    Vec3 Center() const { return (min + max) * 0.5f; }
    Vec3 Extents() const { return (max - min) * 0.5f; }
    bool IsValid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
};

// ---------------------------------------------------------------------------
// 서브메시 — 하나의 인덱스 버퍼 내 [firstIndex, firstIndex+indexCount) 범위.
// glTF의 primitive 하나 = 서브메시 하나. 머티리얼 슬롯 인덱스를 보유(02가 매핑).
// ---------------------------------------------------------------------------
struct Submesh {
    uint32_t firstIndex = 0;    // 인덱스 버퍼 시작 오프셋
    uint32_t indexCount = 0;    // 인덱스 개수
    int32_t  baseVertex = 0;    // 정점 버퍼 베이스(현재 임포터는 항상 0 — 통합 정점 버퍼)
    int32_t  materialSlot = -1; // glTF material 인덱스(-1 = 기본). 02가 머티리얼 에셋과 매핑
    Aabb     bounds{};          // 이 서브메시의 로컬 바운딩
};

// ---------------------------------------------------------------------------
// CPU측 메시 데이터 — MeshImporter::Parse 산출물(GPU 업로드 전 중간 표현).
// Finalize가 이를 소비해 rhi 버퍼를 만든 뒤 Mesh를 커밋한다. 헤드리스 테스트는
// 이 CPU 데이터만으로 정점 수·바운딩·인덱스 정확성을 검증한다.
// ---------------------------------------------------------------------------
struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t>   indices;      // 항상 32bit(임포터가 통일)
    std::vector<Submesh>    submeshes;
    Aabb                    bounds{};     // 전체 메시 로컬 바운딩
};

// ---------------------------------------------------------------------------
// 런타임 Mesh 에셋 객체(AssetHandle<Mesh>가 가리킴, RenderExtract의 AssetGuid mesh가 해석).
// GPU 버퍼 핸들 + 서브메시·바운딩. 정점 포맷은 항상 MeshVertex, 인덱스는 항상 Uint32.
// ---------------------------------------------------------------------------
struct Mesh {
    MYE_ASSET_TYPE(Mesh);

    rhi::BufferHandle vertexBuffer{};   // rhi 소유(Device::CreateBuffer, usage=Vertex)
    rhi::BufferHandle indexBuffer{};    // rhi 소유(usage=Index)
    rhi::IndexFormat  indexFormat = rhi::IndexFormat::Uint32;

    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;

    std::vector<Submesh> submeshes;     // 최소 1개
    Aabb     bounds{};                  // 전체 메시 로컬 바운딩

    // 헤드리스(device=nullptr) 로드 시 CPU 데이터를 보존해 검증·재업로드에 쓴다.
    // GPU 업로드가 성공하면 비워도 무방하나, 현재는 항상 유지(에디터 재임포트 편의).
    MeshData cpu;
};

} // namespace mye::asset
