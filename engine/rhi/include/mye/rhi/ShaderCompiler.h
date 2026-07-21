// mye/rhi/ShaderCompiler.h — FXC(D3DCompile) 런타임 셰이더 컴파일 (docs/02 — M0 임시)
//
// HLSL SM 5.0 소스 정본. M0은 런타임 컴파일, 이후 04 에셋 파이프라인의 셰이더 임포터
// (오프라인 컴파일 + 리플렉션 + 핫 리로드)로 이동하고 본 API는 개발용 경로로 남는다.
#pragma once

#include "mye/rhi/RhiTypes.h"

#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace mye::rhi {

struct ShaderCompileDesc {
    std::string_view source;                 // HLSL 소스 (UTF-8)
    std::string_view entryPoint = "main";
    ShaderStage      stage = ShaderStage::Vertex;   // vs_5_0 / ps_5_0 프로파일 자동 선택
    std::string_view debugName = "shader";   // 에러 메시지·마커용
    std::span<const std::pair<std::string_view, std::string_view>> defines = {};
    bool             debug =                  // /Zi /Od (Debug 빌드 기본)
#if MYE_DEBUG
        true;
#else
        false;
#endif
};

struct ShaderBytecode {
    std::vector<std::byte> data;
};

// 실패 시 Error.message에 FXC 에러 로그 전문 포함.  구현: src/dx11/ShaderCompilerFxc.cpp
Expected<ShaderBytecode, Error> CompileShaderFxc(const ShaderCompileDesc& desc);

} // namespace mye::rhi
