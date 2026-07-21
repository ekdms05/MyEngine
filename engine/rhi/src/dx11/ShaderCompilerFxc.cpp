// ShaderCompilerFxc.cpp — FXC(D3DCompile) 런타임 셰이더 컴파일 (docs/02 셰이더 전략)
//
// M0 임시 경로: 이후 04 에셋 파이프라인의 셰이더 임포터(오프라인 컴파일 + D3DReflect 리플렉션
// + 핫 리로드)로 이관되고, 본 함수는 개발용 온디맨드 컴파일 경로로만 남는다.
#include "mye/rhi/ShaderCompiler.h"

#include "mye/core/Log.h"

#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cstring>
#include <format>
#include <string>
#include <vector>

namespace mye::rhi {

Expected<ShaderBytecode, Error> CompileShaderFxc(const ShaderCompileDesc& desc) {
    using Microsoft::WRL::ComPtr;

    if (desc.source.empty())
        return Error{"CompileShaderFxc: empty shader source", 1};

    // D3DCompile은 null 종단 문자열을 요구 — string_view를 복사해 보관
    const std::string entry(desc.entryPoint);
    const std::string name(desc.debugName);
    const char* target = (desc.stage == ShaderStage::Vertex) ? "vs_5_0" : "ps_5_0";

    std::vector<std::string> defineStorage;
    defineStorage.reserve(desc.defines.size() * 2);
    for (const auto& [defineName, defineValue] : desc.defines) {
        defineStorage.emplace_back(defineName);
        defineStorage.emplace_back(defineValue);
    }
    std::vector<D3D_SHADER_MACRO> macros;
    macros.reserve(desc.defines.size() + 1);
    for (size_t i = 0; i < desc.defines.size(); ++i)
        macros.push_back(D3D_SHADER_MACRO{defineStorage[i * 2].c_str(),
                                          defineStorage[i * 2 + 1].c_str()});
    macros.push_back(D3D_SHADER_MACRO{nullptr, nullptr});

    // 엔진 수학 규약(docs/02): row-major 저장 + row-vector(v*M) 곱 —
    // HLSL 기본(column-major 패킹)을 PACK_MATRIX_ROW_MAJOR로 뒤집어 CPU 메모리 레이아웃과 일치시킨다.
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;
    flags |= desc.debug ? (D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION)
                        : D3DCOMPILE_OPTIMIZATION_LEVEL3;

    ComPtr<ID3DBlob> bytecode;
    ComPtr<ID3DBlob> errors;
    const HRESULT hr = ::D3DCompile(desc.source.data(), desc.source.size(), name.c_str(),
                                    macros.data(), nullptr /*#include 미지원 — 소스 문자열이 정본*/,
                                    entry.c_str(), target, flags, 0,
                                    bytecode.ReleaseAndGetAddressOf(),
                                    errors.ReleaseAndGetAddressOf());

    const auto errorText = [&errors]() -> std::string {
        if (!errors || errors->GetBufferSize() == 0) return {};
        std::string text(static_cast<const char*>(errors->GetBufferPointer()),
                         errors->GetBufferSize());
        while (!text.empty() && (text.back() == '\0' || text.back() == '\n'))
            text.pop_back();
        return text;
    };

    if (FAILED(hr)) {
        // 에러 블롭 전문을 Error.message에 포함 (호출측이 로그·다이얼로그로 표시)
        return Error{std::format("FXC compile failed: '{}' ({}, entry='{}', hr=0x{:08X})\n{}",
                                 name, target, entry, static_cast<uint32_t>(hr), errorText()),
                     static_cast<int32_t>(hr)};
    }
    if (errors && errors->GetBufferSize() > 1)
        MYE_LOG_WARN("RHI", "FXC warnings for '{}':\n{}", name, errorText());

    ShaderBytecode out;
    out.data.resize(bytecode->GetBufferSize());
    std::memcpy(out.data.data(), bytecode->GetBufferPointer(), bytecode->GetBufferSize());
    return out;
}

} // namespace mye::rhi
