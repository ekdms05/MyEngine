// Lua VM ownership and protected game scripting.
#pragma once

#include "mye/core/Base.h"
#include "mye/ecs/Entity.h"
#include "mye/script/ScriptComponent.h"  // CallbackBit
#include "mye/script/ScriptRuntime.h"    // ScriptError

#include "mye/script/LuaApi.h"

#include <string_view>

namespace mye::script {

// 소스 문자열을 로드·실행하고 스크립트가 return 한 클래스 테이블을 반환한다.
Expected<LuaReference, ScriptError> LoadClass(ScriptRuntime& runtime,
                                            std::string_view source,
                                            std::string_view chunkName);

// 클래스 테이블에서 인스턴스 self 를 만든다(setmetatable(instance, {__index=classTable})).
//   entity 는 self.entity 에 주입할 엔티티 핸들. props 는 self 에 주입할 초기 프로퍼티(nil 허용).
LuaReference MakeInstance(ScriptRuntime& runtime, const LuaReference& classTable,
                        ecs::Entity entity, const LuaReference& props);

// 클래스 테이블을 훑어 정의된 라이프사이클 콜백 present 비트(CallbackBit OR)를 계산한다.
uint32_t ScanCallbacks(const LuaReference& classTable);

} // namespace mye::script
