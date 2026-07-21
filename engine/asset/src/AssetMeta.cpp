// mye/asset/AssetMeta.cpp — 사이드카 .meta 파싱/직렬화 (docs/04 §.meta)
//
// 코어의 최소 json(L0)을 직접 소비한다(04 리플렉션 직렬화는 M3까지 없음). 스키마:
//   { "guid": "<uuid>", "importer": "TextureImporter", "importerVersion": 1,
//     "settings": { ... 임포터 자유 JSON ... },
//     "subAssets": { "name": "<uuid>", ... } }
// guid/importer가 없으면 파싱 실패(에셋 식별 불가). settings/subAssets는 선택.
#include "mye/asset/AssetMeta.h"

namespace mye::asset {

Expected<AssetMeta, Error> AssetMeta::Parse(std::string_view utf8Json) {
    auto parsed = json::Parse(utf8Json);
    if (!parsed) {
        return Error{"AssetMeta::Parse: " + parsed.GetError().message, parsed.GetError().code};
    }
    const json::Value& root = parsed.Value();
    if (!root.IsObject()) {
        return Error{"AssetMeta::Parse: root is not an object", 1};
    }

    AssetMeta meta;

    const json::Value* guidV = root.Find("guid");
    if (!guidV || !guidV->IsString()) {
        return Error{"AssetMeta::Parse: missing/invalid 'guid'", 2};
    }
    auto guid = AssetGuid::FromString(guidV->AsString());
    if (!guid) {
        return Error{"AssetMeta::Parse: bad guid: " + guid.GetError().message, 3};
    }
    meta.guid = guid.Value();

    const json::Value* impV = root.Find("importer");
    if (!impV || !impV->IsString()) {
        return Error{"AssetMeta::Parse: missing/invalid 'importer'", 4};
    }
    meta.importer = std::string(impV->AsString());

    if (const json::Value* verV = root.Find("importerVersion"); verV && verV->IsNumber()) {
        meta.importerVersion = static_cast<uint32_t>(verV->AsInt());
    }

    if (const json::Value* setV = root.Find("settings"); setV) {
        meta.settings = *setV;  // 임포터 자유 JSON을 그대로 보관.
    }

    if (const json::Value* subV = root.Find("subAssets"); subV && subV->IsObject()) {
        for (const auto& [name, val] : subV->AsObject()) {
            if (!val.IsString()) {
                return Error{"AssetMeta::Parse: subAsset '" + name + "' is not a string", 5};
            }
            auto subGuid = AssetGuid::FromString(val.AsString());
            if (!subGuid) {
                return Error{"AssetMeta::Parse: subAsset '" + name +
                                 "' bad guid: " + subGuid.GetError().message,
                             6};
            }
            meta.subAssets.emplace(name, subGuid.Value());
        }
    }

    return meta;
}

std::string AssetMeta::Stringify() const {
    json::Value::Object root;
    root.emplace("guid", json::Value(guid.ToString()));
    root.emplace("importer", json::Value(importer));
    root.emplace("importerVersion", json::Value(static_cast<int64_t>(importerVersion)));

    // settings는 null이 아니면 그대로 실어 라운드트립 보존.
    if (!settings.IsNull()) {
        root.emplace("settings", settings);
    }

    if (!subAssets.empty()) {
        json::Value::Object subObj;
        for (const auto& [name, g] : subAssets) {
            subObj.emplace(name, json::Value(g.ToString()));
        }
        root.emplace("subAssets", json::Value(std::move(subObj)));
    }

    return json::Stringify(json::Value(std::move(root)), 2);
}

AssetMeta AssetMeta::CreateFor(std::string_view importer, uint32_t importerVersion) {
    AssetMeta m;
    m.guid = AssetGuid::Generate();
    m.importer = std::string(importer);
    m.importerVersion = importerVersion;
    return m;
}

std::string MetaPathFor(std::string_view sourcePath) {
    return std::string(sourcePath) + ".meta";
}

} // namespace mye::asset
