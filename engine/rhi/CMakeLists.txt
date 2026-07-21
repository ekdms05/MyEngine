# mye_rhi — L1 RHI 인터페이스 + DX11 백엔드 + FXC 런타임 셰이더 컴파일(M0 임시)
add_library(mye_rhi STATIC
    include/mye/rhi/RhiTypes.h
    include/mye/rhi/Rhi.h
    include/mye/rhi/ShaderCompiler.h

    src/dx11/Dx11Device.h
    src/dx11/Dx11Device.cpp
    src/dx11/ShaderCompilerFxc.cpp
)

target_include_directories(mye_rhi PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(mye_rhi
    PUBLIC  mye_core
    PRIVATE d3d11 dxgi d3dcompiler dxguid
)
