// mye/render/MeshTypes.h — 하이브리드 3D 메시 깊이 모드 (M2-C) (docs/02 §삽입 3D)
//
// 정점 포맷·GPU 버퍼·바운딩은 04의 asset::Mesh(gltf 임포트 결과)가 정본이다. 04와 합의한 레이아웃:
//   POSITION(float3, off 0) + NORMAL(float3, off 12) + TEXCOORD0(float2, off 24), stride 32,
//   인덱스 Uint32. (mye/asset/Mesh.h::MeshVertex / MeshVertexAttributes() 참조.)
// 본 렌더러는 asset::Mesh의 vertexBuffer/indexBuffer 핸들을 그대로 바인딩한다(재업로드 없음).
//
// 좌표 규약(02): 왼손·+Y 업·+Z 전방·1 unit = 1m. glTF→왼손 변환은 04 임포터가 수행.
#pragma once

#include <cstdint>

namespace mye::render {

// 3D 메시 깊이 모드 (docs/02 §삽입 3D 깊이 모드) — scene::MeshRenderer::depthMode 인덱스와 정합.
enum class DepthMode : uint8_t {
    AnchorFlat    = 0,   // 오브젝트 전체를 앵커 지면Y flat depth(스프라이트와 동일 취급). 소형.
    AnchorBiased  = 1,   // 앵커 flat depth + 지오메트리 깊이 축소본(ε). 대형(석상·풍차) 기본.
    Geometry      = 2,   // 실제 카메라 깊이(HD2D/배경 구조물).
};

} // namespace mye::render
