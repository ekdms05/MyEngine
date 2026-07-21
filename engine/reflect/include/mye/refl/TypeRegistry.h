// mye/refl/TypeRegistry.h — 프로세스 전역 타입 레지스트리 (docs/04 §리플렉션 TypeRegistry)
//
// DLL-safe: 이름 해시(TypeId)로 조회하므로 모듈 경계에서 안정. 플러그인 언로드 시
// Unregister(ids)로 일괄 해제한다(등록 대칭 해제 규약). 정적 초기화 순서 안전을 위해
// Get()은 함수-로컬 static 싱글턴이다.
//
// 등록 경로: MYE_REFLECT(T)가 심는 자동 등록 스텁 → Reflect<T>(builder) 호출 →
//   builder가 TypeInfo를 만들어 Register. 조회는 Find(id/name).
#pragma once

#include "mye/refl/TypeId.h"
#include "mye/refl/TypeInfo.h"
#include "mye/core/Base.h"

#include <memory>
#include <span>
#include <string_view>

namespace mye::refl {

class TypeRegistry {
public:
    static TypeRegistry& Get();

    // 조회 — 없으면 nullptr. 반환 포인터는 프로세스 수명(재해제 전까지) 안정.
    const TypeInfo* Find(TypeId id) const;
    const TypeInfo* Find(std::string_view name) const;

    // 등록 — 이미 같은 TypeId가 있으면 이름 충돌 검사 후 Error(중복 등록/해시 충돌 구분).
    //   소유권 이전(레지스트리가 TypeInfo 수명 소유). 성공 시 안정 포인터 반환.
    Expected<const TypeInfo*, Error> Register(std::unique_ptr<TypeInfo> info);

    // 플러그인 언로드 시 일괄 해제. 해당 타입을 참조하는 필드가 남아 있으면 dangling 위험 —
    //   호출자(05)가 언로드 순서를 보장한다(계약).
    void Unregister(std::span<const TypeId> ids);

    // 등록된 전체 타입(07 에셋 브라우저·타입 목록 UI). 순서 불보장.
    std::span<const TypeInfo* const> All() const;

    TypeRegistry(const TypeRegistry&) = delete;
    TypeRegistry& operator=(const TypeRegistry&) = delete;

private:
    TypeRegistry();
    ~TypeRegistry();
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// T의 TypeInfo를 얻는다(없으면 자동 등록 시도 → Reflect<T> 호출). 등록 실패 시 nullptr.
//   03/05/06/07이 컴파일타임 타입에서 메타를 얻는 표준 진입점.
template <typename T>
const TypeInfo* GetType();

} // namespace mye::refl
