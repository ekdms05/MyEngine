// mye/scene/Renderable.h — 렌더러블 컴포넌트 (docs/03 §4) [스텁 — M2-B에서 추출 배선]
//
// 렌더러블은 전부 컴포넌트다. M2-A는 컴포넌트 데이터 선언만 두고(희소집합 풀에 저장 가능),
// RenderExtract 추출 로직은 두지 않는다(M2-B). 정렬 데이터는 SortingRef(RenderExtract.h).
#pragma once

#include "mye/core/Math.h"
#include "mye/asset/AssetGuid.h"   // AssetRef
#include "mye/ecs/ComponentType.h"
#include "mye/scene/RenderExtract.h"  // SortingRef

namespace mye::scene {

using asset::AssetRef;

// 2D 스프라이트. 화면 정렬은 정렬 레이어 규약(02). sprite는 AssetRef(런타임 해석).
struct SpriteRenderer {
    MYE_COMPONENT(SpriteRenderer);
    AssetRef   sprite;
    Color      tint = Color::White();
    bool       flipX = false, flipY = false;
    SortingRef sort;
};

// 3D 월드 안에서 카메라를 향하는 스프라이트(HD-2D 캐릭터).
struct BillboardRenderer {
    MYE_COMPONENT(BillboardRenderer);
    AssetRef   sprite;
    uint8_t    mode = 0;   // BillboardMode 스텁(M2-B 확정)
    SortingRef sort;
};

// 3D 메시(테일즈위버식 3D 오브젝트 삽입, HD-2D 월드).
struct MeshRenderer {
    MYE_COMPONENT(MeshRenderer);
    AssetRef mesh;
    // AssetRef materials[slots] — 슬롯 배열은 M2-B 확정.
};

// 타일맵 청크 렌더링 참조.
struct TilemapRenderer {
    MYE_COMPONENT(TilemapRenderer);
    AssetRef map;
};

} // namespace mye::scene
