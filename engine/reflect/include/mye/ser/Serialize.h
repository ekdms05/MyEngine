// mye/ser/Serialize.h — 리플렉션 기반 제네릭 직렬화 진입점 (docs/04 §직렬화)
//
// 03(프리팹)·06(세이브)·04 임포터가 공용으로 쓰는 표면. 컴파일타임 타입 T 또는 런타임
// TypeInfo로 아카이브를 구동한다. 내부적으로 refl::TypeInfo를 걷어 IArchive 프리미티브를
// 호출한다(struct 필드 순회·enum 문자열 왕복·vector 리사이즈·AssetRef 의존성 수집).
#pragma once

#include "mye/core/Base.h"
#include "mye/ser/Archive.h"

namespace mye::refl { class TypeInfo; }

namespace mye::ser {

// 컴파일타임 진입점 — refl::GetType<T>()로 메타를 얻어 obj를 직렬화/역직렬화.
//   ar.IsReading()이 방향을 결정한다. T는 리플렉션 등록 타입이어야 한다.
template <typename T>
Expected<void, Error> Serialize(IArchive& ar, T& obj);

// 런타임 타입 진입점 — TypeInfo와 void* 인스턴스(프리팹·플러그인·에디터가 타입을 런타임에 앎).
Expected<void, Error> SerializeDynamic(IArchive& ar, const refl::TypeInfo& type, void* obj);

} // namespace mye::ser

// Serialize<T> 템플릿 본문(계약 헤더는 선언만, 구현은 detail).
#include "mye/ser/detail/SerializeImpl.h"
