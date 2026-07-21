// mye/scene/RenderExtract.h — 렌더 추출(03 → 02 데이터 계약) (docs/03 §4) [M2-B 구현]
//
// RenderExtract 페이즈에서 본 모듈이 World를 순회해 가시 렌더러블 → RenderItem 목록을 만든다.
// 02 SpriteProxy 계약대로 sortLayer·orderInLayer·sortKeyY(논리 지면 Y)를 채우되,
// **정렬·밴드 배분·깊이 인코딩·CPU sortKey 패킹은 하지 않는다(전적으로 02/M2-C 소유).**
// 본 모듈은 순수 데이터 공급자다.
//
// 정렬 필드(docs/03 §4, docs/02 §렌더 프록시):
//   - sortLayer(uint16): 02 레이어 밴드 id. FloorLevel{k} → World k 밴드 변환은 추출 시점 수행.
//   - orderInLayer(int16): 같은 sortKeyY에서의 타이브레이커(02 SpriteProxy 필드).
//   - sortKeyY(float): 논리 지면 Y(점프 등 시각 오프셋 제외) = Transform.y + FloorLevel 높이 반영.
//   - floorLevel(int8): 층 원본값(02가 밴드 매핑을 재확인·디버그할 수 있게 함께 공급).
#pragma once

#include "mye/core/Math.h"
#include "mye/asset/AssetGuid.h"
#include "mye/ecs/ComponentType.h"

#include <cstdint>
#include <vector>

namespace mye::ecs { class World; }
namespace mye::tilemap { class TilemapWorld; }

namespace mye::scene {

// 렌더러블 공통 정렬 참조(02 SpriteProxy 계약). SpriteRenderer 등 렌더러블 컴포넌트가 보유.
struct SortingRef {
    uint16_t sortLayer = 0;      // 02 레이어 밴드 id (FloorLevel 변환 결과가 여기 들어감)
    int16_t  orderInLayer = 0;   // 타이브레이커(02 SpriteProxy 필드)
};

// 다리 위/아래 층(충돌·내비 참조). 렌더는 추출 시 02 World 밴드 sortLayer로 변환.
struct FloorLevel {
    MYE_COMPONENT(FloorLevel);
    int8_t level = 0;
};

// ---------------------------------------------------------------------------
// FloorLevel → 02 World 밴드 sortLayer 변환 (docs/02 §다리 위/아래, docs/03 §4)
// ---------------------------------------------------------------------------
// 02의 레이어 밴드 표에서 "World 0..N" 밴드의 시작 id를 인용한다(밴드 표 정의·순서는 02 소유).
// 본 모듈은 이름 매핑만 가지므로 여기 상수는 M2-B 기준 협의값 — M2-C에서 02 프로젝트 설정과 정합.
inline constexpr uint16_t kSortLayerWorldBase = 100;   // World 0 밴드의 sortLayer id
// FloorLevel{k} → World k 밴드 sortLayer. k<0은 World 0로 클램프(지하 밴드는 M2-C 확장).
inline uint16_t FloorLevelToSortLayer(int8_t floorLevel) {
    const int k = floorLevel < 0 ? 0 : static_cast<int>(floorLevel);
    return static_cast<uint16_t>(kSortLayerWorldBase + k);
}

// ---------------------------------------------------------------------------
// RenderItem — 02로 넘기는 추출 결과(SpriteProxy로 인코딩되기 전 프록시 데이터)
// ---------------------------------------------------------------------------
enum class RenderItemKind : uint8_t { Sprite, Billboard, Mesh, TilemapChunk };

struct RenderItem {
    RenderItemKind kind = RenderItemKind::Sprite;

    // 그리기 소스(02가 아틀라스·메시 핸들로 해석). 02 SpriteProxy/MeshProxy로 이관.
    asset::AssetGuid texture{};    // Sprite/Billboard: 스프라이트/아틀라스 GUID
    asset::AssetGuid mesh{};       // Mesh: 메시 GUID
    asset::AssetGuid material{};   // Mesh: 머티리얼 GUID
    Rect             srcUV{0, 0, 1, 1};
    Vec2             pivotPx{0, 0};
    Color            tint = Color::White();
    Color            flashColor = Color::White();
    float            flashAmount = 0.0f;
    bool             flipX = false, flipY = false;
    uint8_t          billboardMode = 0;   // BillboardMode
    uint8_t          depthMode = 0;       // Mesh: 02 DepthMode

    // 월드 변환(현재 고정스텝). prevTransform 보간은 M2-C(보간 계약 배선 시).
    Mat4 worldTransform = Mat4::Identity();

    // ---- 정렬 데이터(02 SpriteProxy 계약) ----
    uint16_t sortLayer = 0;      // FloorLevel 변환 결과 포함
    int16_t  orderInLayer = 0;
    float    sortKeyY = 0.0f;    // 논리 지면 Y(점프 오프셋 제외, FloorLevel 높이 반영)
    int8_t   floorLevel = 0;     // 원본 층(02 디버그·재확인용)

    // ---- 타일맵 청크(kind == TilemapChunk) ----
    const tilemap::TilemapWorld* tilemap = nullptr;
    uint8_t  tileLayer = 0;
    Vec2i    chunkCoord{0, 0};   // 청크 좌표(02가 배치 메시 빌드 시 사용)
};

// 추출 결과 목록(프레임 버퍼). RenderExtract가 채우고 02가 소비 후 프레임 경계에서 Clear.
// FrameAllocator(01) 기반 아레나 변형은 후속 — M2-B는 std::vector 기준 구현(계약 동결).
struct RenderProxyList {
    std::vector<RenderItem> items;

    void Clear() { items.clear(); }
    size_t Size() const { return items.size(); }
    RenderItem& Push() { items.emplace_back(); return items.back(); }
};

// ---------------------------------------------------------------------------
// RenderExtract 진입점 — World 순회 → RenderProxyList 수집
// ---------------------------------------------------------------------------
// SpriteRenderer/BillboardRenderer/MeshRenderer/TilemapRenderer 순회.
// FloorLevel → World k 밴드 sortLayer 변환, WorldTransform에서 sortKeyY(논리 지면 Y) 산출.
// 정렬은 하지 않는다(02 몫). out은 호출 전 Clear됨.
void ExtractRenderItems(ecs::World& world, RenderProxyList& out);

// SystemScheduler(RenderExtract 페이즈)에 추출 시스템을 등록하는 헬퍼.
// (SceneModule 배선용 — RenderProxyList는 호출자가 소유·수명 관리.)
class SystemScheduler;
void RegisterRenderExtractSystem(SystemScheduler& scheduler, RenderProxyList& target);

} // namespace mye::scene
