# mye_social — [게임 레이어] MMORPG 소셜: 로그인 대기열·친구/차단·프레즌스·채팅 (docs/mmorpg, M12)
# 엔진이 아니라 엔진 위에 얹는 게임 레이어. 순수 로직 위주(서버 사용).
add_library(mye_social STATIC
    include/mye/social/LoginQueue.h
    include/mye/social/FriendGraph.h
    include/mye/social/Presence.h
    include/mye/social/ChatRouter.h
    include/mye/social/PartySystem.h
    include/mye/social/GuildSystem.h
    include/mye/social/GuildBank.h
    include/mye/social/MailSystem.h
    include/mye/social/TradeSystem.h
    include/mye/social/AuctionHouse.h

    src/LoginQueue.cpp
    src/FriendGraph.cpp
    src/Presence.cpp
    src/ChatRouter.cpp
    src/PartySystem.cpp
    src/GuildSystem.cpp
    src/GuildBank.cpp
    src/MailSystem.cpp
    src/TradeSystem.cpp
    src/AuctionHouse.cpp
)

target_include_directories(mye_social PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(mye_social
    PUBLIC mye_core mye_persist
)
