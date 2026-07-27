// mye/net/UdpSocket.cpp — Winsock UDP 구현 (UdpSocket.h 참조)
#include "mye/net/UdpSocket.h"

#include "mye/core/Log.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <format>
#include <utility>

#pragma comment(lib, "Ws2_32.lib")

namespace mye::net {

NetSubsystem::NetSubsystem() {
    WSADATA wsa{};
    ok = (::WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
    if (!ok) MYE_LOG_ERROR("Net", "WSAStartup 실패");
}
NetSubsystem::~NetSubsystem() {
    if (ok) ::WSACleanup();
}

Expected<Endpoint, Error> Endpoint::Parse(std::string_view ipv4, uint16_t port) {
    in_addr a{};
    std::string s(ipv4);
    if (::inet_pton(AF_INET, s.c_str(), &a) != 1)
        return Error{"Endpoint::Parse: 잘못된 IPv4 '" + s + "'", 1};
    return Endpoint{::ntohl(a.s_addr), port};
}

std::string Endpoint::ToString() const {
    return std::format("{}.{}.{}.{}:{}", (addr >> 24) & 0xFF, (addr >> 16) & 0xFF,
                       (addr >> 8) & 0xFF, addr & 0xFF, port);
}

namespace {
SOCKET AsSock(uint64_t h) { return static_cast<SOCKET>(h); }
}

UdpSocket::~UdpSocket() { Close(); }

UdpSocket& UdpSocket::operator=(UdpSocket&& o) noexcept {
    if (this != &o) {
        Close();
        m_handle = o.m_handle;
        o.m_handle = kInvalid;
    }
    return *this;
}

bool UdpSocket::Open(uint16_t port) {
    Close();
    SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) { MYE_LOG_ERROR("Net", "socket 실패 (WSA={})", ::WSAGetLastError()); return false; }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = ::htons(port);
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        MYE_LOG_ERROR("Net", "bind({}) 실패 (WSA={})", port, ::WSAGetLastError());
        ::closesocket(s);
        return false;
    }
    u_long nonblock = 1;
    ::ioctlsocket(s, FIONBIO, &nonblock);
    m_handle = static_cast<uint64_t>(s);
    return true;
}

void UdpSocket::Close() {
    if (m_handle != kInvalid) {
        ::closesocket(AsSock(m_handle));
        m_handle = kInvalid;
    }
}

uint16_t UdpSocket::LocalPort() const {
    if (m_handle == kInvalid) return 0;
    sockaddr_in addr{};
    int len = sizeof(addr);
    if (::getsockname(AsSock(m_handle), reinterpret_cast<sockaddr*>(&addr), &len) == SOCKET_ERROR)
        return 0;
    return ::ntohs(addr.sin_port);
}

int UdpSocket::SendTo(const Endpoint& to, const void* data, size_t size) {
    if (m_handle == kInvalid) return -1;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::htonl(to.addr);
    addr.sin_port = ::htons(to.port);
    const int n = ::sendto(AsSock(m_handle), static_cast<const char*>(data), static_cast<int>(size), 0,
                           reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    return n == SOCKET_ERROR ? -1 : n;
}

int UdpSocket::RecvFrom(Endpoint& from, void* out, size_t maxSize) {
    if (m_handle == kInvalid) return -1;
    sockaddr_in addr{};
    int len = sizeof(addr);
    const int n = ::recvfrom(AsSock(m_handle), static_cast<char*>(out), static_cast<int>(maxSize), 0,
                             reinterpret_cast<sockaddr*>(&addr), &len);
    if (n == SOCKET_ERROR) {
        const int err = ::WSAGetLastError();
        return (err == WSAEWOULDBLOCK) ? 0 : -1;   // 데이터 없음 = 0
    }
    from.addr = ::ntohl(addr.sin_addr.s_addr);
    from.port = ::ntohs(addr.sin_port);
    return n;
}

} // namespace mye::net
