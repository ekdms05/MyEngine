// mye/asset/AssetGuid.cpp — 128비트 에셋 식별자 구현 (docs/04 §식별자·핸들)
//
// 생성: 버전4 UUID 레이아웃(랜덤 122비트 + 버전/변형 비트 고정)을 std::random_device
// 시드 mt19937_64로 채운다. 암호학적 강도가 필요한 배포 시나리오(pak 서명 등)는 M4
// 오픈 이슈이며, M1 범위의 에셋 식별에는 충돌 확률이 무시 가능한 이 방식으로 충분하다.
// 문자열 표기: 표준 8-4-4-4-12 하이픈 UUID(소문자 hex).
#include "mye/asset/AssetGuid.h"

#include <array>
#include <chrono>
#include <mutex>
#include <random>

namespace mye::asset {

namespace {

// 프로세스 전역 난수 엔진 — 스레드 안전(뮤텍스 보호). 최초 접근 시 random_device로 시드.
uint64_t NextRandom64() {
    static std::mutex mtx;
    static std::mt19937_64 engine{[] {
        std::random_device rd;
        // random_device 32비트 두 번을 64비트 시드로 합성.
        uint64_t seed = (static_cast<uint64_t>(rd()) << 32) ^ static_cast<uint64_t>(rd());
        seed ^= static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        return seed;
    }()};
    std::lock_guard<std::mutex> lock(mtx);
    return engine();
}

constexpr char kHexLower[] = "0123456789abcdef";

// 하나의 hex 니블(0..15)을 파싱. 실패 시 0xFF.
constexpr uint8_t HexNibble(char c) {
    if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
    return 0xFF;
}

} // namespace

AssetGuid AssetGuid::Generate() {
    AssetGuid g;
    g.hi = NextRandom64();
    g.lo = NextRandom64();

    // RFC 4122 버전4 레이아웃: hi의 bits 12..15(48~51 절대)를 0x4로, lo 최상위 2비트를 10b로.
    // hi 바이트 레이아웃(빅엔디안 표기 기준 time_hi_and_version): (hi >> 12) & 0xF == version.
    g.hi = (g.hi & 0xFFFF'FFFF'FFFF'0FFFull) | 0x0000'0000'0000'4000ull;
    // lo의 최상위 니블 상위 2비트 = variant(10).
    g.lo = (g.lo & 0x3FFF'FFFF'FFFF'FFFFull) | 0x8000'0000'0000'0000ull;

    // version/variant를 넣어도 hi 또는 lo가 0이 될 수 없으므로 IsValid는 항상 true.
    return g;
}

std::string AssetGuid::ToString() const {
    // 16바이트를 빅엔디안 순서로 hex화: hi가 앞 8바이트, lo가 뒤 8바이트.
    std::array<uint8_t, 16> bytes{};
    for (int i = 0; i < 8; ++i) {
        bytes[static_cast<size_t>(i)] = static_cast<uint8_t>((hi >> (56 - i * 8)) & 0xFF);
        bytes[static_cast<size_t>(8 + i)] = static_cast<uint8_t>((lo >> (56 - i * 8)) & 0xFF);
    }

    std::string out;
    out.reserve(36);
    auto appendByte = [&](uint8_t b) {
        out.push_back(kHexLower[(b >> 4) & 0xF]);
        out.push_back(kHexLower[b & 0xF]);
    };
    // 8-4-4-4-12
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out.push_back('-');
        appendByte(bytes[static_cast<size_t>(i)]);
    }
    return out;
}

Expected<AssetGuid, Error> AssetGuid::FromString(std::string_view text) {
    // 정확히 36자, 하이픈 위치 8-13-18-23.
    if (text.size() != 36) {
        return Error{"AssetGuid::FromString: expected 36 chars", 1};
    }
    auto isDash = [](size_t i) { return i == 8 || i == 13 || i == 18 || i == 23; };

    std::array<uint8_t, 16> bytes{};
    size_t byteIdx = 0;
    for (size_t i = 0; i < text.size();) {
        if (isDash(i)) {
            if (text[i] != '-') return Error{"AssetGuid::FromString: expected '-'", 2};
            ++i;
            continue;
        }
        // 두 개의 hex 니블.
        if (byteIdx >= 16) return Error{"AssetGuid::FromString: too many bytes", 3};
        uint8_t hiN = HexNibble(text[i]);
        uint8_t loN = HexNibble(text[i + 1]);
        if (hiN == 0xFF || loN == 0xFF) {
            return Error{"AssetGuid::FromString: invalid hex digit", 4};
        }
        bytes[byteIdx++] = static_cast<uint8_t>((hiN << 4) | loN);
        i += 2;
    }
    if (byteIdx != 16) return Error{"AssetGuid::FromString: too few bytes", 5};

    AssetGuid g;
    g.hi = 0;
    g.lo = 0;
    for (int i = 0; i < 8; ++i) {
        g.hi = (g.hi << 8) | bytes[static_cast<size_t>(i)];
        g.lo = (g.lo << 8) | bytes[static_cast<size_t>(8 + i)];
    }
    return g;
}

} // namespace mye::asset
