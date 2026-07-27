# MyServer — 헤드리스 게임 서버 (docs/mmorpg/02, M9). 윈도우 콘솔 서버.
add_executable(MyServer
    main.cpp
)

target_link_libraries(MyServer PRIVATE
    mye_core mye_net
)
