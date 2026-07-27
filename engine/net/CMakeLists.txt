# mye_net — 네트워킹 계층 (비트직렬화·프로토콜·스냅샷·델타·전송) (docs/mmorpg/02, M9)
# 와이어 포맷·직렬화는 순수 로직(소켓 무관, 유닛 테스트). 전송(UDP)·서버는 후속.
add_library(mye_net STATIC
    include/mye/net/BitStream.h
    include/mye/net/Quantization.h
    include/mye/net/DeltaCodec.h
    include/mye/net/UdpSocket.h
    include/mye/net/Protocol.h
    include/mye/net/NetServer.h
    include/mye/net/NetClient.h

    src/BitStream.cpp
    src/Quantization.cpp
    src/DeltaCodec.cpp
    src/UdpSocket.cpp
    src/NetServer.cpp
    src/NetClient.cpp
)

target_include_directories(mye_net PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(mye_net
    PUBLIC  mye_core
    PRIVATE Ws2_32
)
