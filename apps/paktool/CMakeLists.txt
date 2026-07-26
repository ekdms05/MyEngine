# paktool — .pak 아카이브 패커/리스터 CLI (docs/04 배포). mye_asset(PakWriter/PakFileSystem) 사용.
add_executable(paktool
    main.cpp
)

target_link_libraries(paktool PRIVATE
    mye_core mye_asset
)
