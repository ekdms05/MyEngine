// mye/ser/detail/SerializeImpl.h — Serialize<T> 템플릿 본문 (계약 헤더는 선언만)
//
// 컴파일타임 진입점: refl::GetType<T>() 로 메타를 얻어 SerializeDynamic 으로 위임.
#pragma once

#include "mye/refl/TypeInfo.h"
#include "mye/refl/TypeRegistry.h"
#include "mye/refl/detail/GetType.h"   // GetType<T>() 정의
#include "mye/ser/Serialize.h"

namespace mye::ser {

template <typename T>
Expected<void, Error> Serialize(IArchive& ar, T& obj) {
    const refl::TypeInfo* type = refl::GetType<T>();
    if (!type) return Error{"Serialize<T>: type not reflected", 1};
    return SerializeDynamic(ar, *type, static_cast<void*>(&obj));
}

} // namespace mye::ser
