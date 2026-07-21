// mye/asset/SpriteImporter.h — Aseprite/스프라이트시트 임포터 (docs/04 §임포터 라인업, M3-A)
//
// AsepriteImporter: Aseprite 공식 CLI(--sheet --data JSON) 산출물(시트 PNG + JSON)을 파싱해
//   SpriteSheet + AnimationClipData×N 을 만든다. 태그→클립·프레임 duration→타이밍·슬라이스→
//   pivot/9-slice/히트박스·`@hitbox` 이름 규약 레이어→히트박스.
//
// M3-A 범위: 인터페이스 동결 + JSON 파싱 골격. **테스트 자산은 aseprite.exe 설치에 의존하지
//   않는다** — 임포터는 이미 익스포트된 (시트 PNG + Aseprite JSON) 쌍을 입력으로 받는 순수
//   파서 경로를 노출한다(ParseAsepriteJson). CLI 호출은 별도 얇은 래퍼(에디터 전용, 후속).
#pragma once

#include "mye/asset/Importer.h"
#include "mye/asset/SpriteSheet.h"
#include "mye/core/Base.h"
#include "mye/core/Json.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mye::asset {

// Aseprite 임포트 설정(.meta settings). 07이 SettingsType 리플렉션으로 인스펙터 자동 생성(후속).
struct AsepriteImportSettings {
    std::string atlasGroup;                 // 소속 아틀라스 그룹(빈 문자열 = 개별 텍스처)
    std::string pivotSliceName = "pivot";   // pivot 으로 쓸 슬라이스 이름
    std::string hitboxLayerPrefix = "@hitbox";  // 히트박스 레이어 이름 규약 접두
    bool        premultiplyAlpha = true;    // 02 통일 규약
    // 페이퍼돌 파츠 규약 검증: 바디 템플릿(프레임 수·태그·캔버스·duration 대조). 비어있으면 비활성.
    AssetRef    bodyTemplate;
};

// ParseAsepriteJson 의 산출물 — 임포터가 SpriteSheet/클립으로 굳히기 전의 중간 표현.
//   (aseprite.exe 없이 순수 데이터로 단위 테스트 가능하게 CLI 경로와 분리한다.)
struct AsepriteData {
    Vec2i                          sheetSize;      // 시트 PNG 크기(픽셀)
    std::vector<SpriteFrame>       frames;         // frames[] (rect·duration 은 아래 병렬)
    std::vector<float>             frameDurations; // frames 와 동수(초)
    std::vector<SpriteSlice>       slices;         // slices[]
    std::vector<AnimationClipData> clips;          // meta.frameTags[] → 클립(프레임 구간)
    std::vector<std::string>       warnings;       // 규약 위반·미지 필드 경고(07 배지 연동)
};

// Aseprite `--data` JSON(하나의 json::Value)을 파싱. 시트 PNG 는 별도 경로로 디코드(TextureImporter).
//   실패 시 Error. 경고는 out.warnings 로 수집(임포트 에러/경고 리포트).
Expected<AsepriteData, Error> ParseAsepriteJson(const json::Value& asepriteJson);

// Aseprite CLI 산출물(시트 PNG + JSON) → SpriteSheet(+클립) 임포터.
class AsepriteImporter final : public IAssetImporter {
public:
    const char* Name() const override { return "AsepriteImporter"; }
    std::span<const std::string_view> SourceExtensions() const override;   // {".ase", ".aseprite"}
    uint32_t Version() const override { return 1; }
    AssetTypeId ProducedType() const override { return SpriteSheet::kAssetTypeId; }

    Expected<void*, Error> Import(ImportContext& ctx) override;
    void Destroy(void* assetObject) override;
};

// 이미 익스포트된 스프라이트시트(그리드 셀 기반 PNG + 설정)를 직접 임포트하는 대안 경로.
//   Aseprite 없이 균등 그리드 시트를 다루는 게임·툴용(테스트에도 사용).
struct SpriteSheetImportSettings {
    Vec2i       frameSize{0, 0};    // 셀 크기(픽셀). 0,0 이면 단일 프레임
    Vec2i       margin{0, 0};       // 시트 가장자리 여백
    Vec2i       spacing{0, 0};      // 셀 간 간격
    std::string atlasGroup;
    bool        premultiplyAlpha = true;
};

class SpriteSheetImporter final : public IAssetImporter {
public:
    const char* Name() const override { return "SpriteSheetImporter"; }
    std::span<const std::string_view> SourceExtensions() const override;   // {".sheet.png"} 규약
    uint32_t Version() const override { return 1; }
    AssetTypeId ProducedType() const override { return SpriteSheet::kAssetTypeId; }

    Expected<void*, Error> Import(ImportContext& ctx) override;
    void Destroy(void* assetObject) override;

    // 그리드 셀 계산 순수 헬퍼(단위 테스트) — 시트 크기·셀·여백·간격 → 프레임 rect 목록.
    static std::vector<SpriteFrame> SliceGrid(Vec2i sheetSize,
                                              const SpriteSheetImportSettings& s);
};

} // namespace mye::asset
