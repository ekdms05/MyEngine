// mye/refl/PropertyPath.h — 경로 단위 read/write (docs/04 §PropertyPath)
//
// 03 프리팹 오버라이드('프로퍼티 경로 표현과 값 직렬화')와 07 PropertyEditCommand{
// PropertyPath, ValueBlob }·Undo가 인용하는 공급 계약. M3-A는 계약 + 파싱/해석 골격.
//
// 문법: `.`으로 중첩 필드(struct 포함), `[i]`로 배열/컨테이너 인덱스.
//   예: "transform.position.x", "slots[2].part"
#pragma once

#include "mye/core/Base.h"
#include "mye/refl/TypeInfo.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace mye::ser { class IArchive; }

namespace mye::refl {

// 경로 세그먼트 하나 — 필드 이름 또는 컨테이너 인덱스.
struct PathSegment {
    std::variant<std::string, std::size_t> value;   // string=필드, size_t=인덱스
    bool IsField() const { return std::holds_alternative<std::string>(value); }
    bool IsIndex() const { return std::holds_alternative<std::size_t>(value); }
    auto operator<=>(const PathSegment&) const = default;
};

// 파싱된 프로퍼티 경로 — 정규형 왕복 보장.
struct PropertyPath {
    std::vector<PathSegment> segments;

    static Expected<PropertyPath, Error> Parse(std::string_view text);
    std::string ToString() const;                       // 정규형(라운드트립 보장)
    bool Empty() const { return segments.empty(); }
    auto operator<=>(const PropertyPath&) const = default;
};

// 경로가 가리키는 리프 필드 — 인스펙터·Undo가 리프 값·메타에 접근.
struct ResolvedProperty {
    const TypeInfo*  type = nullptr;    // 리프 값의 타입
    const FieldInfo* field = nullptr;   // 리프 필드 메타(어트리뷰트 조회용). 컨테이너 원소면 nullptr
    void*            ptr = nullptr;     // 인스턴스 내 리프 값 포인터
};

// root 타입의 instance에서 path를 따라 리프를 해석. 경로 무효·범위초과 시 Error.
Expected<ResolvedProperty, Error>
ResolvePath(const TypeInfo& root, void* instance, const PropertyPath& path);

// 경로 단위 값 직렬화 — out/in은 파일·메모리 스트림 아카이브 모두 가능.
//   07 Undo 값 blob이 메모리 경로(MemoryStream 기반 아카이브)를 소비.
Expected<void, Error>
ReadValue(const TypeInfo& root, const void* instance, const PropertyPath& path, ser::IArchive& out);

Expected<void, Error>
WriteValue(const TypeInfo& root, void* instance, const PropertyPath& path, ser::IArchive& in);

} // namespace mye::refl
