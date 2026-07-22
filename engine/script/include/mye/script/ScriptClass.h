// mye/script/ScriptClass.h — 클래스 테이블 로드 헬퍼(sol::table 반환 표면) (docs/05, M3-C)
//
// ScriptRuntime.h 는 sol.hpp 전파를 막으려 sol::table 을 반환하는 표면을 두지 않는다.
//   이 헤더가 그 "무거운" 표면을 담당한다 — .lua 소스를 청크로 로드·실행해 스크립트가 return
//   한 클래스 테이블을 얻는다(docs/05 §Lua 스크립트 규약: 파일은 엔티티 클래스 테이블을 반환).
//
// 반환 규약(docs/05):
//   - 성공: return 값이 table 이면 그 테이블(클래스). table 이 아니면 에러(스크립트 규약 위반).
//   - 실패: 청크 컴파일/실행 에러 → ScriptError(file·line·traceback). 호출측이 격리 처리.
//
// 이 헤더를 포함하는 TU 는 <sol/sol.hpp> 세계다(ScriptSystem.cpp·데모 배선부).
#pragma once

#include "mye/core/Base.h"
#include "mye/ecs/Entity.h"
#include "mye/script/ScriptComponent.h"  // CallbackBit
#include "mye/script/ScriptRuntime.h"    // ScriptError

#include <sol/sol.hpp>

#include <string_view>

namespace mye::script {

// 소스 문자열을 로드·실행하고 스크립트가 return 한 클래스 테이블을 반환한다.
//   chunkName 은 에러 메시지 파일명("@" 접두사는 내부 처리). runtime 은 게임 sol::state 소유자.
Expected<sol::table, ScriptError> LoadClass(ScriptRuntime& runtime,
                                            std::string_view source,
                                            std::string_view chunkName);

// 클래스 테이블에서 인스턴스 self 를 만든다(setmetatable(instance, {__index=classTable})).
//   entity 는 self.entity 에 주입할 엔티티 핸들. props 는 self 에 주입할 초기 프로퍼티(nil 허용).
//   반환: 새 인스턴스 테이블(항상 유효 — sol 이 예외를 던지지 않는 경로).
sol::table MakeInstance(ScriptRuntime& runtime, const sol::table& classTable,
                        ecs::Entity entity, const sol::table& props);

// 클래스 테이블을 훑어 정의된 라이프사이클 콜백 present 비트(CallbackBit OR)를 계산한다.
uint32_t ScanCallbacks(const sol::table& classTable);

} // namespace mye::script
