// mye/script/IBindingModule.h — Lua 바인딩 모듈 인터페이스 (docs/05 §Binding 계층, M3-C)
//
// 바인딩 에이전트가 구현하는 확장 포인트. 여러 바인딩 모듈(core 수학·엔티티·오디오·anim 등)이
//   각자 sol::state 에 usertype/함수를 등록한다. ScriptRuntime 이 초기화(및 VM 재생성) 시
//   등록된 모든 모듈의 Register 를 순서대로 호출한다.
//
// 계약(docs/05 §Lua 스테이트 소유 모델):
//   - 등록 함수는 VM 이 (재)생성될 때마다 다시 호출된다 → 바인딩 모듈은 sol 객체(테이블·함수)를
//     멤버로 저장해두면 안 된다. 매 Register 호출에서 새로 등록만 한다.
//   - 전역은 `mye` 테이블 하나. 함수·필드는 snake_case, 타입은 PascalCase(docs/05 네이밍).
//   - Register 는 메인 스레드에서만 호출된다(Lua 단일 스레드 계약).
//
// 무거운 sol.hpp 를 이 헤더가 강제하지 않도록 sol/forward.hpp(전방 선언)만 인클루드한다.
//   구현 .cpp 는 <sol/sol.hpp> 를 포함해 sol::state 실체에 접근한다.
#pragma once

#include <sol/forward.hpp>

#include <string_view>

namespace mye::script {

class IBindingModule {
public:
    virtual ~IBindingModule() = default;

    // 바인딩 모듈 식별 이름(진단·중복 검출·플러그인 소유 표시). 예: "core", "audio", "anim".
    virtual std::string_view Name() const = 0;

    // sol::state 에 이 모듈의 바인딩(usertype·함수·상수)을 등록한다.
    //   ScriptRuntime 이 `mye` 전역 테이블을 이미 만들어 두므로 모듈은 lua["mye"] 하위에
    //   자기 표면을 추가한다(get_or_create 로 안전 접근). 예외를 던질 수 있다(sol2 경계 허용).
    virtual void Register(sol::state& lua) = 0;
};

} // namespace mye::script
