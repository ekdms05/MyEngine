// mye/render/SpriteRenderer.cpp — 스프라이트 조립 헬퍼 구현 (docs/02)
#include "mye/render/SpriteRenderer.h"

#include "mye/render/Camera2D.h"   // kPixelsPerUnit
#include "mye/asset/Texture.h"

namespace mye::render {

SpriteDraw MakeSprite(const asset::Texture* tex, Vec2 position, Vec2 pivot, Color tint) {
    return MakeSpriteFromAtlas(tex, SpriteSourceRect{}, position, pivot, tint);
}

SpriteDraw MakeSpriteFromAtlas(const asset::Texture* tex, SpriteSourceRect src, Vec2 position,
                               Vec2 pivot, Color tint) {
    SpriteDraw s{};
    s.position = position;
    s.pivot = pivot;
    s.tint = tint;
    s.sortY = position.y;   // 발밑 Y 기본. 호출자가 필요 시 덮어씀.
    if (tex == nullptr || tex->width == 0 || tex->height == 0) {
        return s;   // texture 미설정(무효) — 호출자가 Submit 전 검사.
    }
    s.texture = tex->gpuTexture;

    const float texW = static_cast<float>(tex->width);
    const float texH = static_cast<float>(tex->height);

    // 서브렉트 0,0이면 텍스처 전체.
    const float rw = (src.w == 0) ? texW : static_cast<float>(src.w);
    const float rh = (src.h == 0) ? texH : static_cast<float>(src.h);

    // 월드 크기(unit) = 소스 픽셀 / PPU.
    s.size = {rw / kPixelsPerUnit, rh / kPixelsPerUnit};

    // 정규화 UV(좌상단 원점, +V 아래) = 서브렉트 / 텍스처 크기.
    s.uv = {static_cast<float>(src.x) / texW,
            static_cast<float>(src.y) / texH,
            rw / texW,
            rh / texH};
    return s;
}

} // namespace mye::render
