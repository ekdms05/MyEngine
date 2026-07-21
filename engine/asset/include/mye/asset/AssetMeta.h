// mye/asset/AssetMeta.h — 사이드카 .meta (GUID·임포터·설정·서브에셋) (docs/04 §.meta)
//
// 소스 에셋마다 짝을 이루는 JSON. 코어의 json 파서/직렬화를 그대로 소비한다
// (04의 리플렉션 직렬화는 M3까지 없음). 최초 임포트 시 GUID를 생성해 기록하고,
// 서브 에셋은 name→GUID로 고정 매핑(리임포트해도 GUID 불변).
#pragma once

#include "mye/asset/AssetGuid.h"
#include "mye/core/Base.h"
#include "mye/core/Json.h"

#include <map>
#include <string>
#include <string_view>

namespace mye::asset {

struct AssetMeta {
    AssetGuid   guid;
    std::string importer;              // 임포터 이름("TextureImporter")
    uint32_t    importerVersion = 0;
    json::Value settings;              // 임포터별 임포트 설정(자유 JSON)
    std::map<std::string, AssetGuid, std::less<>> subAssets;   // name → GUID

    // .meta JSON 텍스트 파싱/직렬화. 구현: src/AssetMeta.cpp
    static Expected<AssetMeta, Error> Parse(std::string_view utf8Json);
    std::string Stringify() const;

    // guid가 없으면 새로 생성해 반환(최초 임포트 시). 이미 있으면 그대로.
    static AssetMeta CreateFor(std::string_view importer, uint32_t importerVersion);
};

// 소스 경로에 대한 .meta 사이드카 경로 규약: "<source>.meta".
std::string MetaPathFor(std::string_view sourcePath);

} // namespace mye::asset
