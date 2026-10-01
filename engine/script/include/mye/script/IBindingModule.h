// Lua VM ownership and protected game scripting.
#pragma once

struct lua_State;

#include <string_view>

namespace mye::script {

class IBindingModule {
public:
    virtual ~IBindingModule() = default;

    // 바인딩 모듈 식별 이름(진단·중복 검출·플러그인 소유 표시). 예: "core", "audio", "anim".
    virtual std::string_view Name() const = 0;

    //   ScriptRuntime 이 `mye` 전역 테이블을 이미 만들어 두므로 모듈은 lua["mye"] 하위에
    virtual void Register(lua_State* lua) = 0;

    // 선택: 매 프레임 갱신(ScriptRuntime::UpdateBindings 이 호출). 기본 no-op.
    //   예) DdcBindingModule 은 여기서 등록된 Lua 시스템을 스토어 위에서 실행한다.
    virtual void OnUpdate(float /*dt*/) {}
};

} // namespace mye::script
