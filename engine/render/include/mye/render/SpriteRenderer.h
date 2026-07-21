// mye/render/SpriteRenderer.h — 스프라이트 조립 헬퍼 (docs/02 통합 스프라이트 경로)
//
// 목적: "텍스처(+선택 아틀라스 서브렉트) → SpriteDraw" 조립을 한 곳으로 통일한다. 이 계산
//   (월드 크기 = 소스픽셀/PPU, 정규화 UV = 서브렉트/텍스처크기)을 데모·03(SpriteRenderer
//   컴포넌트)·06(UI)·아틀라스 소비자가 각자 재구현해 규약이 어긋나는 것을 막는다.
//   SpriteBatch는 텍스처 크기를 모르므로(헤더 계약) 이 헬퍼가 텍스처 크기로 UV를 정규화한다.
//
// 좌표/픽셀 규약(02): PPU=kPixelsPerUnit(48), UV 좌상단 원점·+V 아래, 피벗은 0..1 정규화.
#pragma once

#include "mye/core/Math.h"
#include "mye/render/SpriteBatch.h"

namespace mye::asset { struct Texture; }

namespace mye::render {

// 아틀라스 서브렉트(소스 텍셀 단위, 좌상단 원점). 기본 = 텍스처 전체(w=h=0 → 전체로 해석).
struct SpriteSourceRect {
    uint32_t x = 0, y = 0;      // 좌상단 텍셀 오프셋
    uint32_t w = 0, h = 0;      // 텍셀 크기(0,0이면 텍스처 전체)
};

// 텍스처 전체를 그리는 SpriteDraw를 조립한다.
//   position: 피벗 지점의 월드 위치(unit)
//   pivot   : 정규화 피벗(0..1). 기본 {0.5,1} = 하단 중앙(발밑).
//   size    = {tex.width, tex.height} / PPU (unit), uv = {0,0,1,1}.
// tex가 null이면 texture 미설정 SpriteDraw(무효)를 돌려준다 — 호출자가 Submit 전 검사.
SpriteDraw MakeSprite(const asset::Texture* tex, Vec2 position,
                      Vec2 pivot = {0.5f, 1.0f}, Color tint = Color::White());

// 아틀라스 서브렉트를 그리는 SpriteDraw를 조립한다. size = {src.w,src.h}/PPU,
// uv = 서브렉트를 텍스처 크기로 정규화(0..1). src.w/h가 0이면 텍스처 전체로 취급.
SpriteDraw MakeSpriteFromAtlas(const asset::Texture* tex, SpriteSourceRect src, Vec2 position,
                               Vec2 pivot = {0.5f, 1.0f}, Color tint = Color::White());

} // namespace mye::render
