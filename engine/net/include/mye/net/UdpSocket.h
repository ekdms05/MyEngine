// mye/net/UdpSocket.h — 논블로킹 UDP 소켓 (Winsock) (docs/mmorpg/02, M9)
//
// 윈도우 개발/서버 환경 전제(사용자 결정). 헤더에 <winsock2.h>를 노출하지 않는다(SOCKET은 불투명
// 핸들로 보관). IPv4 UDP 송수신·논블로킹. 서버·클라 공용 전송 기반.
#pragma once

#include "mye/core/Base.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace mye::net {

// Winsock 전역 초기화(WSAStartup/Cleanup) — 프로세스당 1회 RAII. 소켓 사용 전 생성.
struct NetSubsystem {
    NetSubsystem();
    ~NetSubsystem();
    NetSubsystem(const NetSubsystem&) = delete;
    NetSubsystem& operator=(const NetSubsystem&) = delete;
    bool ok = false;
};

// IPv4 종단점(호스트 표기 헬퍼 포함). addr/port는 호스트 바이트오더로 보관.
struct Endpoint {
    uint32_t addr = 0;   // 0.0.0.0
    uint16_t port = 0;

    static Endpoint Loopback(uint16_t port) { return Endpoint{0x7F000001u, port}; }   // 127.0.0.1
    static Expected<Endpoint, Error> Parse(std::string_view ipv4, uint16_t port);
    std::string ToString() const;
    bool operator==(const Endpoint&) const = default;
};

class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&& o) noexcept { *this = std::move(o); }
    UdpSocket& operator=(UdpSocket&& o) noexcept;

    // 소켓 생성 + 바인드(port 0 = 임시 포트) + 논블로킹. 성공 true.
    bool Open(uint16_t port = 0);
    void Close();
    bool IsOpen() const { return m_handle != kInvalid; }

    // 바인드된 로컬 포트(임시 포트 확인용). 실패 시 0.
    uint16_t LocalPort() const;

    // 송신 — 보낸 바이트 수(<0 오류).
    int SendTo(const Endpoint& to, const void* data, size_t size);
    // 수신 — 받은 바이트 수. 데이터 없음(논블로킹)이면 0, 오류 -1. from에 송신자 기록.
    int RecvFrom(Endpoint& from, void* out, size_t maxSize);

private:
    static constexpr uint64_t kInvalid = ~0ull;
    uint64_t m_handle = kInvalid;   // SOCKET(불투명)
};

} // namespace mye::net
