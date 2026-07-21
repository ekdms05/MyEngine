# asset_smoke — M1 토대 에셋 경로 GPU 스모크 (PNG→AssetManager→GPU 텍스처→리드백 대조)
add_executable(asset_smoke
    src/main.cpp
)

target_link_libraries(asset_smoke PRIVATE mye_core mye_rhi mye_asset)

# CTest 등록(선택 실행). GPU 필요 → CI headless 환경에 따라 스킵 가능.
add_test(NAME asset_smoke COMMAND asset_smoke)
