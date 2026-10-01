# mye_persist — 영속화 계층 (계정·캐릭터·아이템 원장) (docs/mmorpg/03)
# 윈도우 개발/서버 전제. 현재 저장은 JSON 상태 파일·세대 백업이며 DB 도입은 별도 확장 요구다.
add_library(mye_persist STATIC
    include/mye/persist/AccountStore.h
    include/mye/persist/CharacterStore.h
    include/mye/persist/ItemLedger.h
    include/mye/persist/PersistenceService.h

    src/AccountStore.cpp
    src/CharacterStore.cpp
    src/ItemLedger.cpp
    src/PersistenceService.cpp
)

target_include_directories(mye_persist PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(mye_persist
    PUBLIC mye_core
    PRIVATE bcrypt
)
