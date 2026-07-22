// mye/script/ScriptImporter.h — .lua → ScriptAsset 임포터 (docs/05 §04 경계, M3-C)
//
// docs/05: ".lua 를 ScriptAsset 으로 취급(핫 리로드는 04 파일 워처 이벤트에 반응)."
//   임포터는 소스 바이트를 UTF-8 원문으로 읽어 ScriptAsset(원문·해시)을 만든다. 컴파일(청크
//   로드)은 런타임(ScriptRuntime)이 인스턴스화 시점에 수행 — 임포터는 데이터만 만든다.
//   (바이트코드 프리컴파일은 오픈이슈 #5, 후속. M3-C 는 원문 임베드.)
//
// AssetManager::RegisterImporter(std::make_unique<ScriptImporter>()) 로 등록한다.
#pragma once

#include "mye/asset/Importer.h"
#include "mye/script/ScriptTypes.h"

#include <span>
#include <string_view>

namespace mye::script {

// .lua 소스 임포터 — IAssetImporter 계약(04) 구현. GPU 리소스 없음(순수 CPU 에셋).
class ScriptImporter final : public asset::IAssetImporter {
public:
    const char* Name() const override { return "ScriptImporter"; }
    std::span<const std::string_view> SourceExtensions() const override;   // {".lua"}
    uint32_t Version() const override { return 1; }
    asset::AssetTypeId ProducedType() const override { return ScriptAsset::kAssetTypeId; }

    // 소스 바이트(UTF-8 .lua 원문) → new ScriptAsset*. sourcePath·source·sourceHash 채움.
    Expected<void*, Error> Import(asset::ImportContext& ctx) override;
    void Destroy(void* assetObject) override;   // delete ScriptAsset* (GPU 리소스 없음)
};

} // namespace mye::script
