// mye/asset/AssetGuid.h — 128비트 에셋 식별자 (docs/04 §식별자·핸들)
//
// GUID는 최초 임포트 시 생성되어 사이드카 .meta에 영속된다. 파일을 옮겨도 .meta가
// 따라가므로 경로가 아닌 이 안정 식별자로 에셋을 참조한다. 문자열 표기는 표준
// 8-4-4-4-12 하이픈 UUID 형식(소문자 hex)을 사용한다.
#pragma once

#include "mye/core/Base.h"
#include "mye/refl/TypeId.h"

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

// 에셋 타입 식별자 — M3-A에서 정본 refl::TypeId(FNV-1a 64bit 이름 해시)로 승격.
// refl::TypeId 와 mye::HashFnv1a64 는 동일 알고리즘이라 M2의 placeholder 해시 값과
// 비트 호환이다(기존 kAssetTypeId 값 불변 — .meta·슬롯 타입 매칭이 깨지지 않는다).
// 각 에셋 타입은 static constexpr AssetTypeId kAssetTypeId 를 심는다(MYE_ASSET_TYPE).
using AssetTypeId = refl::TypeId;

// 타입 이름 문자열로 AssetTypeId 계산. refl::TypeIdFromName 과 동치(정본 경유).
#define MYE_ASSET_TYPE(TypeName) \
    static constexpr ::mye::asset::AssetTypeId kAssetTypeId = ::mye::refl::TypeIdFromName(#TypeName)

// 직렬화되는 에셋 참조 필드(하드/소프트 의존성 선언에 사용). M1+에서 본격 활용.
struct AssetRef {
    AssetGuid   guid;
    AssetTypeId type = 0;
};

} // namespace mye::asset
