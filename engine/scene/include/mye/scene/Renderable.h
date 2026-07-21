// mye/scene/Renderable.h — 렌더러블 컴포넌트 (docs/03 §4) [M2-B: 데이터 + 추출 배선]
//
// 렌더러블은 전부 ECS 컴포넌트다. 각 컴포넌트는 sortLayer·orderInLayer(SortingRef)를 보유하고,
// RenderExtract 시스템이 이를 02 SpriteProxy 계약대로 RenderItem으로 추출한다(RenderExtract.h).
// 정렬 데이터는 SortingRef, 층은 FloorLevel(RenderExtract.h). 그리기 실행·정렬은 02(M2-C).
#pragma once

#include "mye/core/Math.h"
#include "mye/asset/AssetGuid.h"   // AssetRef
#include "mye/ecs/ComponentType.h"
#include "mye/scene/RenderExtract.h"  // SortingRef

namespace mye::tilemap { class TilemapWorld; }

namespace mye::scene {

using asset::AssetRef;

// 2D 스프라이트. 화면 정렬은 정렬 레이어 규약(02). sprite는 AssetRef(런타임 해석).
// srcUV = 아틀라스 소스 사각형(정규화 0~1 또는 픽셀 — 02가 해석). pivotPx = 피벗(보통 발밑).
struct SpriteRenderer {
    MYE_COMPONENT(SpriteRenderer);
    AssetRef   sprite;
    Rect       srcUV{0, 0, 1, 1};          // 아틀라스 소스 UV(기본 = 전체)
    Vec2       pivotPx{0.0f, 0.0f};         // 픽셀 피벗(발밑 기준 정렬용)
    Color      tint = Color::White();
    Color      flashColor = Color::White(); // 히트 플래시 색(02가 flashAmount로 블렌드)
    float      flashAmount = 0.0f;          // 0=없음, 1=완전 플래시
    bool       flipX = false, flipY = false;
    bool       visible = true;
    SortingRef sort;
};

// 3D 월드 안에서 카메라를 향하는 스프라이트(HD-2D 캐릭터).
enum class BillboardMode : uint8_t { Full = 0, YAxis = 1, None = 2 };

struct BillboardRenderer {
    MYE_COMPONENT(BillboardRenderer);
    AssetRef      sprite;
    Rect          srcUV{0, 0, 1, 1};
    Vec2          pivotPx{0.0f, 0.0f};
    Color         tint = Color::White();
    BillboardMode mode = BillboardMode::YAxis;
    bool          visible = true;
    SortingRef    sort;
};

// 3D 메시(테일즈위버식 3D 오브젝트 삽입, HD-2D 월드). 메시·머티리얼 핸들은 M2-C가 채운다.
// depthMode: 02 MeshProxy::DepthMode(AnchorFlat/AnchorBiased/Geometry) 인덱스 인용.
struct MeshRenderer {
    MYE_COMPONENT(MeshRenderer);
    AssetRef   mesh;
    AssetRef   material;         // 단일 슬롯(다중 슬롯은 M2-C 확장)
    uint8_t    depthMode = 1;    // 기본 AnchorBiased(02 §삽입 3D 깊이 모드)
    bool       visible = true;
    SortingRef sort;
};

// 타일맵 청크 렌더링 참조. map = 직렬화 참조(에셋). runtime = 런타임 질의 허브(비소유 포인터).
struct TilemapRenderer {
    MYE_COMPONENT(TilemapRenderer);
    AssetRef                 map;
    tilemap::TilemapWorld*   runtime = nullptr;   // 03 런타임 인스턴스(추출이 청크 순회)
    bool                     visible = true;
    SortingRef               sort;                 // 청크 프록시 기본 sortLayer(지면 밴드 등)
};

} // namespace mye::scene
