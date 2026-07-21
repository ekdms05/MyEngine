// mye/audio/AudioTypes.h — 오디오 공용 타입(버스·핸들·재생 파라미터) (docs/06 §4, M3-B)
//
// 무의존(코어 수학만). 백엔드·엔진·믹서가 공유한다. 06 규약:
//   - 버스 트리: Master ← { BGM, SFX, UI, Voice }. 버스별 볼륨/뮤트. Master 는 전 버스에 곱해진다.
//   - 리스너 = 플레이어 월드 위치(카메라 아님). 감쇠 = 월드 XY 평면 거리, 패닝 = 화면 X 축만.
#pragma once

#include "mye/core/Math.h"

#include <cstdint>

namespace mye::audio {

// 버스 식별자. Master 는 항상 존재하는 루트 버스(모든 출력에 곱해짐).
// 나머지는 06 §4 MVP 버스 집합 + Voice(대사). AudioBusLayout 에셋화는 후속(지금은 고정 열거).
enum class BusId : uint8_t {
    Master = 0,
    BGM,
    SFX,
    UI,
    Voice,
    Count
};

constexpr uint32_t kBusCount = static_cast<uint32_t>(BusId::Count);

// 재생 인스턴스 핸들. 원샷/루프 재생 1회 = 1 핸들. 정지·질의에 사용.
// 0 = 무효(재생 실패 또는 미배정). generation 으로 슬롯 재사용 dangling 검출.
struct VoiceHandle {
    uint32_t index = 0;       // 1-기반 슬롯 인덱스(0=무효)
    uint32_t generation = 0;

    constexpr bool IsValid() const { return index != 0; }
    constexpr bool operator==(const VoiceHandle&) const = default;
};

// 한 보이스의 재생 파라미터. 큐 변조(랜덤 볼륨/피치) 적용 후의 최종 값.
struct PlayParams {
    float volume = 1.0f;      // 선형 게인 [0,∞) — 보통 [0,1]
    float pitch  = 1.0f;      // 재생 속도 배수(1=원속). 리샘플로 구현.
    float pan    = 0.0f;      // -1(좌) .. 0(중앙) .. +1(우). 화면 X 축.
    bool  loop   = false;
    BusId bus    = BusId::SFX;
};

// 2.5D 공간화 파라미터. worldPos 가 주어진 보이스에만 적용.
//   attenuation: 리스너-소스 XY 거리 d 에 대해 [minDistance,maxDistance] 사이 선형 감쇠.
//     d <= minDistance → 게인 1, d >= maxDistance → 게인 0(=minGain), 사이 선형.
//   패닝: 리스너 기준 소스의 화면 X 오프셋을 panScale 로 정규화.
struct SpatialParams {
    Vec2  worldPos{};              // 소스 월드 위치(XY 평면)
    float minDistance = 1.0f;
    float maxDistance = 20.0f;
    float minGain     = 0.0f;      // maxDistance 이상에서의 하한 게인
    float panScale    = 10.0f;     // 이 X 거리에서 팬이 ±1 로 포화
};

// 거리 감쇠 게인 계산(월드 XY 평면). 결과 [minGain,1].
float ComputeAttenuation(Vec2 listener, const SpatialParams& sp);
// 화면 X 축 패닝 계산. 결과 [-1,1].
float ComputePan(Vec2 listener, const SpatialParams& sp);

} // namespace mye::audio
