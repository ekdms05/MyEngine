// mye/asset/AssetGuid.h — 128비트 에셋 식별자 (docs/04 §식별자·핸들)
//
// GUID는 최초 임포트 시 생성되어 사이드카 .meta에 영속된다. 파일을 옮겨도 .meta가
// 따라가므로 경로가 아닌 이 안정 식별자로 에셋을 참조한다. 문자열 표기는 표준
// 8-4-4-4-12 하이픈 UUID 형식(소문자 hex)을 사용한다.
#pragma once

#include "mye/core/Base.h"

#include <compare>
#include <cstdint>
#include <string>
#include <string_view>

namespace mye::asset {

struct AssetGuid {
    uint64_t hi = 0;
    uint64_t lo = 0;

    // 랜덤 128비트 생성(버전 4 UUID 레이아웃). 구현: src/AssetGuid.cpp
    static AssetGuid Generate();

    // "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" 파싱. 실패 시 Error.
    static Expected<AssetGuid, Error> FromString(std::string_view text);

    std::string ToString() const;   // 소문자 하이픈 UUID

    constexpr bool IsValid() const { return hi != 0 || lo != 0; }
    constexpr auto operator<=>(const AssetGuid&) const = default;
    constexpr bool operator==(const AssetGuid&) const = default;
};

// 에셋 타입 식별자 — 정본은 04의 refl::TypeId(FNV-1a 64bit 이름 해시)이나,
// M0~M2에는 리플렉션이 없으므로 여기서는 타입 이름 문자열의 FNV-1a 64 해시를 직접 쓴다.
// 각 에셋 타입은 static constexpr AssetTypeId kAssetTypeId 를 심는다(MYE_ASSET_TYPE).
using AssetTypeId = uint64_t;

#define MYE_ASSET_TYPE(TypeName) \
    static constexpr ::mye::asset::AssetTypeId kAssetTypeId = ::mye::HashFnv1a64(#TypeName)

// 직렬화되는 에셋 참조 필드(하드/소프트 의존성 선언에 사용). M1+에서 본격 활용.
struct AssetRef {
    AssetGuid   guid;
    AssetTypeId type = 0;
};

} // namespace mye::asset
