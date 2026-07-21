// mye/scene/Scene.h — mye_scene 공개 표면 집합 헤더 (docs/03)
//
// 이 헤더 하나로 M2-A가 동결한 ECS·Transform·스케줄러·CommandBuffer·SceneModule 계약과
// 스텁(RenderExtract/Tilemap/Collision) 표면을 모두 가져온다. 구현 에이전트·소비자용.
#pragma once

// ---- ECS 코어 ----
#include "mye/ecs/Entity.h"
#include "mye/ecs/ComponentType.h"
#include "mye/ecs/ComponentPool.h"
#include "mye/ecs/World.h"
#include "mye/ecs/View.h"
#include "mye/ecs/CommandBuffer.h"

// ---- Transform·스케줄러·모듈 ----
#include "mye/scene/Transform.h"
#include "mye/scene/SystemScheduler.h"
#include "mye/scene/SceneModule.h"

// ---- 렌더러블·정렬(추출 스텁) ----
#include "mye/scene/Renderable.h"
#include "mye/scene/RenderExtract.h"

// ---- 스텁(후속 워크플로우) ----
#include "mye/tilemap/Tilemap.h"
#include "mye/phys/Collision.h"

namespace mye::scene {

// SceneManager — 씬 로드/언로드/additive 합성(docs/03 §10). [스텁 시그니처 — M2-B]
// class SceneManager {
// public:
//     SceneHandle loadAdditiveAsync(asset::AssetRef sceneAsset, LoadPriority = {});
//     void        unload(SceneHandle);
//     void        setPersistent(ecs::Entity root);           // 씬 언로드에서 제외
//     ecs::Entity instantiate(asset::AssetRef prefab, const LocalTransform&, SceneHandle = {});
// };

} // namespace mye::scene
