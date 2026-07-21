// mye/audio/AudioCue.h — 이벤트 기반 재생 큐 에셋 (docs/06 §4, M3-B)
//
// 06 규약: "코드는 파일이 아니라 AudioCue 를 재생한다." Cue = {클립 목록(랜덤/순차 선택), 볼륨·피치
//   변조 범위, 대상 버스, 폴리포니 제한, 우선순위}. 예: "칼 휘두르기" 큐 하나에 3 클립 랜덤 재생.
//   에셋 직렬화·리플렉션 등록은 후속(여기선 런타임 표현만 — M3-C 데모가 발소리/BGM 을 이걸로 배선).
#pragma once

#include "mye/asset/AudioClip.h"
#include "mye/audio/AudioTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mye::audio {

// 큐 안에서 여러 클립 중 하나를 고르는 방식.
enum class CueSelect : uint8_t { Random, Sequential };

struct AudioCue {
    // 재생 후보 클립들(비소유 포인터 — 소유자가 수명 보장). 랜덤/순차로 1개 선택.
    std::vector<const asset::AudioClip*> clips;
    CueSelect selection = CueSelect::Random;

    BusId bus = BusId::SFX;

    // 변조 범위(선택된 클립에 곱해질 최종 값을 [min,max] 균등 샘플). min==max 면 고정.
    float volumeMin = 1.0f, volumeMax = 1.0f;
    float pitchMin  = 1.0f, pitchMax  = 1.0f;

    bool loop = false;

    // 동시 재생 제한(폴리포니). 0 = 무제한. 초과 시 우선순위/최고령 기준 거절/스틸.
    uint32_t maxVoices = 0;
    int32_t  priority = 0;   // 스틸 판정용(높을수록 우선). 지금은 카운트 제한만 사용.

    // 순차 선택 커서(post 마다 증가). 런타임 상태 — 에셋 직렬화 대상 아님.
    mutable uint32_t sequentialCursor = 0;

    bool IsValid() const { return !clips.empty(); }
};

} // namespace mye::audio
