// PropertyPath.cpp — 경로 파싱·해석 (M3-A 골격 스텁)
//
// Parse/ToString 은 실사용 가능한 최소 구현. ResolvePath/ReadValue/WriteValue 의 실제
// 필드·인덱스 트래버설은 구현 에이전트가 채운다(TypeInfo 순회 + 아카이브 연동).
#include "mye/refl/PropertyPath.h"

#include "mye/ser/Serialize.h"

#include <cctype>
#include <string>

namespace mye::refl {

Expected<PropertyPath, Error> PropertyPath::Parse(std::string_view text) {
    PropertyPath path;
    std::size_t i = 0;
    const std::size_t n = text.size();
    while (i < n) {
        if (text[i] == '.') { ++i; continue; }
        if (text[i] == '[') {
            std::size_t j = i + 1;
            std::size_t idx = 0;
            bool any = false;
            while (j < n && std::isdigit(static_cast<unsigned char>(text[j]))) {
                idx = idx * 10 + static_cast<std::size_t>(text[j] - '0');
                any = true;
                ++j;
            }
            if (!any || j >= n || text[j] != ']') {
                return Error{"PropertyPath::Parse: malformed index", 1};
            }
            path.segments.push_back(PathSegment{idx});
            i = j + 1;
        } else {
            std::size_t j = i;
            while (j < n && text[j] != '.' && text[j] != '[') ++j;
            if (j == i) return Error{"PropertyPath::Parse: empty field", 2};
            path.segments.push_back(PathSegment{std::string(text.substr(i, j - i))});
            i = j;
        }
    }
    return path;
}

std::string PropertyPath::ToString() const {
    std::string out;
    for (const auto& seg : segments) {
        if (seg.IsField()) {
            if (!out.empty()) out += '.';
            out += std::get<std::string>(seg.value);
        } else {
            out += '[';
            out += std::to_string(std::get<std::size_t>(seg.value));
            out += ']';
        }
    }
    return out;
}

Expected<ResolvedProperty, Error>
ResolvePath(const TypeInfo& root, void* instance, const PropertyPath& path) {
    const TypeInfo* curType = &root;
    const FieldInfo* curField = nullptr;   // 리프 필드 메타(컨테이너 원소면 nullptr)
    void* curPtr = instance;

    for (const auto& seg : path.segments) {
        if (!curPtr) return Error{"ResolvePath: null instance mid-path", 1};
        if (seg.IsField()) {
            if (curType->GetKind() != Kind::Struct) {
                return Error{"ResolvePath: field access on non-struct", 2};
            }
            const std::string& name = std::get<std::string>(seg.value);
            const FieldInfo* f = curType->FindField(name);
            if (!f) return Error{"ResolvePath: unknown field '" + name + "'", 3};
            curField = f;
            curType = &f->Type();
            curPtr = f->GetPtr(curPtr);
        } else {
            const std::size_t idx = std::get<std::size_t>(seg.value);
            if (curType->GetKind() != Kind::Vector) {
                return Error{"ResolvePath: index on non-vector", 4};
            }
            const std::size_t n = curType->VectorSize(curPtr);
            if (idx >= n) return Error{"ResolvePath: index out of range", 5};
            void* elem = curType->VectorElementAt(curPtr, idx);
            curField = nullptr;   // 컨테이너 원소 — 필드 메타 없음
            curType = curType->ElementType();
            curPtr = elem;
            if (!curType) return Error{"ResolvePath: vector without element type", 6};
        }
    }
    return ResolvedProperty{curType, curField, curPtr};
}

Expected<void, Error>
ReadValue(const TypeInfo& root, const void* instance, const PropertyPath& path,
          ser::IArchive& out) {
    if (out.IsReading()) return Error{"ReadValue: archive must be in write mode", 7};
    auto r = ResolvePath(root, const_cast<void*>(instance), path);
    if (!r) return r.GetError();
    const ResolvedProperty& rp = r.Value();
    return ser::SerializeDynamic(out, *rp.type, rp.ptr);
}

Expected<void, Error>
WriteValue(const TypeInfo& root, void* instance, const PropertyPath& path,
           ser::IArchive& in) {
    if (!in.IsReading()) return Error{"WriteValue: archive must be in read mode", 8};
    auto r = ResolvePath(root, instance, path);
    if (!r) return r.GetError();
    const ResolvedProperty& rp = r.Value();
    return ser::SerializeDynamic(in, *rp.type, rp.ptr);
}

} // namespace mye::refl
