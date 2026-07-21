// mye/scene/RenderExtract.h — 렌더 추출 스텁 (docs/03 §4) [M2-B 구현]
//
// M2-A: 시그니처·정렬 데이터 모델만 선언(구현 금지). RenderExtract 페이즈에서 본 모듈이
// 02의 SpriteProxy 계약대로 sortLayer·sortKeyY를 채워 RenderItem 목록을 공급한다.
// 정렬 실행·깊이 인코딩은 전적으로 02 소유 — 본 모듈은 CPU측 sortKey 패킹을 하지 않는다.
//
// 정렬 필드(docs/03 §4):
//   - sortLayer(uint16): 02 레이어 밴드 id. FloorLevel{k} → World k 밴드 변환은 추출 시점 수행.
//   - orderInLayer(int16): 같은 sortKeyY에서의 타이브레이커(02 SpriteProxy에 추가 협의 중).
//   - ySort(sortKeyY): 논리 지면 Y(점프 등 시각 오프셋 제외) — 레이어 내 정렬은 02가 담당.
#pragma once

#include "mye/core/Math.h"
#include "mye/ecs/ComponentType.h"

#include <cstdint>

namespace mye::ecs { class World; }

namespace mye::scene {

// 렌더러블 공통 정렬 참조(02 SpriteProxy 계약). SpriteRenderer 등 렌더러블 컴포넌트가 보유.
struct SortingRef {
    uint16_t sortLayer = 0;      // 02 레이어 밴드 id (FloorLevel 변환 결과가 여기 들어감)
    int16_t  orderInLayer = 0;   // 타이브레이커(02와 협의 중 — 확정 전 추출 시 무시)
};

// 다리 위/아래 층(충돌·내비 참조). 렌더는 추출 시 02 World 밴드 sortLayer로 변환.
struct FloorLevel {
    MYE_COMPONENT(FloorLevel);
    int8_t level = 0;
};

// 02로 넘기는 추출 결과 아이템(스텁 — 실제 필드는 02 SpriteProxy 계약에 맞춰 M2-B 확정).
// struct RenderItem { uint16_t sortLayer; float sortKeyY; int16_t orderInLayer; /* + 그리기 데이터 */ };
//
// RenderExtract 진입점(스텁 시그니처만 — 구현은 M2-B):
//   void ExtractRenderItems(ecs::World& world, /* out */ RenderProxyList& out);
//     - SpriteRenderer/BillboardRenderer/MeshRenderer/TilemapRenderer 순회
//     - FloorLevel → World k 밴드 sortLayer 변환
//     - WorldTransform에서 sortKeyY(논리 지면 Y) 산출 → 02 SpriteProxy로 공급
//   IRenderExtractor 확장 포인트(docs/03 §확장 3): 플러그인 렌더러블 추출 콜백 등록.

} // namespace mye::scene
