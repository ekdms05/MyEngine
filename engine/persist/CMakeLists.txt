# mye_persist — 영속화 계층 (계정·캐릭터·아이템 원장) (docs/mmorpg/03, M10)
# 윈도우 개발/서버 전제. 1단계는 파일 기반(JSON) — 스케일용 Postgres/Redis는 후속(설계 문서).
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
)
