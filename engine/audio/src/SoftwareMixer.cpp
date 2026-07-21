// SoftwareMixer.cpp — 순수 소프트웨어 믹서 구현 (docs/06 §4, M3-B)
//
// 렌더 경로(RenderInto):
//   1) 이 블록의 프레임별 버스 게인(램프)을 미리 계산(버스별 1회 전진 → 보이스 수와 무관하게 정확).
//   2) 각 활성 보이스를 순회: 소스에서 필요한 프레임을 인터리브 F32 로 당겨(loop 반영) 리샘플
//      (pitch·샘플레이트 비, 선형 보간)하고 스테레오 업/다운믹스, 보이스 볼륨·페이드·공간화
//      (감쇠·패닝)·버스·마스터 게인을 곱해 출력에 가산.
//   클리핑은 하지 않는다(수치 검증 신뢰성 — 상위/장치가 필요 시 처리).
#include "mye/audio/SoftwareMixer.h"

#include <algorithm>
#include <cmath>

namespace mye::audio {

// ---- GainRamp ----
void GainRamp::RampTo(float g, uint64_t durationFrames) {
    target = g;
    if (durationFrames == 0) { current = g; perFrame = 0.0f; return; }
    perFrame = (target - current) / static_cast<float>(durationFrames);
    if (perFrame == 0.0f) current = target;   // 동일 게인 → 즉시
}

float GainRamp::Advance() {
    if (perFrame == 0.0f) return current;
    current += perFrame;
    if ((perFrame > 0.0f && current >= target) ||
        (perFrame < 0.0f && current <= target)) {
        current = target;
        perFrame = 0.0f;
    }
    return current;
}

// ---- SoftwareMixer ----
SoftwareMixer::SoftwareMixer(uint32_t outputSampleRate)
    : m_sampleRate(outputSampleRate) {
    for (auto& b : m_buses) b.gain.SetImmediate(1.0f);
}

void SoftwareMixer::SetBusVolume(BusId bus, float linear) {
    m_buses[static_cast<uint32_t>(bus)].gain.SetImmediate(std::max(0.0f, linear));
}
float SoftwareMixer::GetBusVolume(BusId bus) const {
    return m_buses[static_cast<uint32_t>(bus)].gain.target;
}
void SoftwareMixer::SetBusMute(BusId bus, bool mute) {
    m_buses[static_cast<uint32_t>(bus)].mute = mute;
}
bool SoftwareMixer::GetBusMute(BusId bus) const {
    return m_buses[static_cast<uint32_t>(bus)].mute;
}
void SoftwareMixer::RampBusVolume(BusId bus, float linear, uint64_t durationFrames) {
    m_buses[static_cast<uint32_t>(bus)].gain.RampTo(std::max(0.0f, linear), durationFrames);
}

VoiceHandle SoftwareMixer::Allocate() {
    for (uint32_t i = 0; i < m_voices.size(); ++i) {
        if (!m_voices[i].active) {
            m_generations[i] = ++m_generationCounter;
            m_voices[i] = MixerVoice{};
            VoiceHandle h{i + 1, m_generations[i]};
            m_voices[i].handle = h;
            return h;
        }
    }
    const uint32_t idx = static_cast<uint32_t>(m_voices.size());
    m_voices.emplace_back();
    m_generations.push_back(++m_generationCounter);
    VoiceHandle h{idx + 1, m_generations[idx]};
    m_voices[idx].handle = h;
    return h;
}

MixerVoice* SoftwareMixer::Resolve(VoiceHandle h) {
    if (!h.IsValid() || h.index > m_voices.size()) return nullptr;
    const uint32_t i = h.index - 1;
    if (m_generations[i] != h.generation) return nullptr;
    return &m_voices[i];
}
const MixerVoice* SoftwareMixer::Resolve(VoiceHandle h) const {
    if (!h.IsValid() || h.index > m_voices.size()) return nullptr;
    const uint32_t i = h.index - 1;
    if (m_generations[i] != h.generation) return nullptr;
    return &m_voices[i];
}

VoiceHandle SoftwareMixer::Play(std::unique_ptr<IClipSource> source, const PlayParams& params) {
    if (!source) return {};
    VoiceHandle h = Allocate();
    MixerVoice* v = Resolve(h);
    v->source = std::move(source);
    v->params = params;
    v->spatial = false;
    v->fade.SetImmediate(1.0f);
    v->active = true;
    v->phase = 0.0;
    v->hasHistory = false;
    return h;
}

VoiceHandle SoftwareMixer::PlaySpatial(std::unique_ptr<IClipSource> source,
                                       const PlayParams& params, const SpatialParams& spatial) {
    VoiceHandle h = Play(std::move(source), params);
    if (MixerVoice* v = Resolve(h)) {
        v->spatial = true;
        v->spatialParams = spatial;
    }
    return h;
}

void SoftwareMixer::Stop(VoiceHandle h) {
    if (MixerVoice* v = Resolve(h)) { v->active = false; v->source.reset(); }
}

void SoftwareMixer::FadeOutAndStop(VoiceHandle h, uint64_t durationFrames) {
    if (MixerVoice* v = Resolve(h)) {
        v->fade.RampTo(0.0f, durationFrames);
        v->stopWhenFadedOut = true;
        if (durationFrames == 0) { v->active = false; v->source.reset(); }
    }
}

void SoftwareMixer::SetVoiceFade(VoiceHandle h, float target, uint64_t durationFrames) {
    if (MixerVoice* v = Resolve(h)) {
        v->fade.RampTo(std::max(0.0f, target), durationFrames);
        v->stopWhenFadedOut = false;
    }
}

void SoftwareMixer::SetVoiceVolume(VoiceHandle h, float linear) {
    if (MixerVoice* v = Resolve(h)) v->params.volume = std::max(0.0f, linear);
}
void SoftwareMixer::SetVoicePaused(VoiceHandle h, bool paused) {
    if (MixerVoice* v = Resolve(h)) v->paused = paused;
}
bool SoftwareMixer::IsVoiceActive(VoiceHandle h) const {
    const MixerVoice* v = Resolve(h);
    return v && v->active;
}
uint32_t SoftwareMixer::ActiveVoiceCount() const {
    uint32_t n = 0;
    for (const auto& v : m_voices) if (v.active) ++n;
    return n;
}
uint32_t SoftwareMixer::ActiveVoiceCountOnBus(BusId bus) const {
    uint32_t n = 0;
    for (const auto& v : m_voices) if (v.active && v.params.bus == bus) ++n;
    return n;
}

void SoftwareMixer::RenderInto(float* dst, uint64_t frames) {
    std::fill(dst, dst + frames * kMixerChannels, 0.0f);
    if (frames == 0) return;

    // 1) 프레임별 버스 게인(램프)을 미리 계산. 인덱스 [bus][frame].
    static thread_local std::vector<float> busGains;   // kBusCount * frames
    busGains.assign(static_cast<size_t>(kBusCount) * frames, 0.0f);
    for (uint32_t b = 0; b < kBusCount; ++b) {
        // 램프를 복사본으로 전진(원본은 마지막에 최종 상태로 커밋).
        GainRamp ramp = m_buses[b].gain;
        const bool mute = m_buses[b].mute;
        for (uint64_t f = 0; f < frames; ++f) {
            const float g = ramp.Advance();
            busGains[static_cast<size_t>(b) * frames + f] = mute ? 0.0f : g;
        }
        m_buses[b].gain = ramp;   // 최종 상태 커밋
    }
    const uint32_t masterIdx = static_cast<uint32_t>(BusId::Master);
    const float* masterGains = &busGains[static_cast<size_t>(masterIdx) * frames];

    // 2) 보이스 믹싱.
    //
    // 리샘플 좌표계: 로컬 버퍼 buf = [ (히스토리 프레임) , pull[0], pull[1], ... ] 를 만들고 cursor 가
    //   buf 프레임 인덱스를 가리킨다. hasHistory 면 buf[0]=직전 블록 마지막 프레임이고 이번 블록
    //   실 데이터는 buf[1]..; 아니면 buf[0]=pull[0](신규 보이스, 히스토리 없음). 이렇게 하면 step=1
    //   에서 첫 출력이 정확히 소스 프레임 0 이 되고(히스토리 없음 → cursor 0 = pull[0]), 블록 경계도
    //   히스토리 프레임 1개로 선형 보간이 연속된다. 소스는 순차 pull 이라 소비량만큼만 당겨 커서 동기.
    for (auto& v : m_voices) {
        if (!v.active || v.paused || !v.source) continue;
        IClipSource* src = v.source.get();
        const uint16_t ch = src->Channels();
        if (ch == 0 || src->SampleRate() == 0) continue;

        const double srcRate = static_cast<double>(src->SampleRate());
        const double step = static_cast<double>(v.params.pitch) *
                            (srcRate / static_cast<double>(m_sampleRate));

        const uint32_t histCount = v.hasHistory ? 1u : 0u;   // buf 앞에 붙는 히스토리 프레임 수
        const double startCursor = static_cast<double>(histCount) + v.phase;
        // 마지막 샘플 위치(f=frames-1)의 상위 보간 인덱스 i1 = floor(lastCursor)+1 까지 필요.
        const double lastCursor = startCursor + step * static_cast<double>(frames - 1);
        const uint64_t maxIndex = static_cast<uint64_t>(std::floor(lastCursor)) + 1;
        const uint64_t bufFramesNeeded = maxIndex + 1;   // 인덱스 0..maxIndex
        // 정확히 소비분만 pull(과다 pull 방지 → 블록 간 프레임 손실 없음).
        const uint64_t pullNeeded =
            (bufFramesNeeded > histCount) ? (bufFramesNeeded - histCount) : 0;

        static thread_local std::vector<float> buf;   // 스테레오로 정규화해 저장(2ch)
        buf.assign(bufFramesNeeded * 2, 0.0f);
        if (histCount) { buf[0] = v.histL; buf[1] = v.histR; }

        // 소스에서 pullNeeded 프레임을 소스 채널 인터리브로 당긴다.
        static thread_local std::vector<float> pull;
        pull.assign((pullNeeded ? pullNeeded : 1) * ch, 0.0f);
        const uint64_t got = pullNeeded ? src->ReadFrames(pull.data(), pullNeeded, v.params.loop) : 0;

        // pull → buf(스테레오) 업/다운믹스.
        for (uint64_t i = 0; i < got; ++i) {
            const float l = pull[i * ch + 0];
            const float r = (ch >= 2) ? pull[i * ch + 1] : l;
            buf[(histCount + i) * 2 + 0] = l;
            buf[(histCount + i) * 2 + 1] = r;
        }
        // 소스가 완전히 소진되었고 히스토리도 없으면(첫 블록부터 무음) 정지.
        if (got == 0 && histCount == 0) { v.active = false; v.source.reset(); continue; }

        // 유효한 마지막 buf 프레임 인덱스(그 이후는 무음 → 소진).
        const int64_t lastValid = static_cast<int64_t>(histCount + got) - 1;

        bool exhausted = false;
        const uint32_t busIdx = static_cast<uint32_t>(v.params.bus);
        const float* busG = &busGains[static_cast<size_t>(busIdx) * frames];

        double cursor = startCursor;
        for (uint64_t f = 0; f < frames; ++f) {
            const int64_t i0 = static_cast<int64_t>(std::floor(cursor));
            const double frac = cursor - static_cast<double>(i0);
            const int64_t i1 = i0 + 1;

            float sL = 0.0f, sR = 0.0f;
            if (i0 <= lastValid) {
                const float aL = buf[static_cast<uint64_t>(i0) * 2 + 0];
                const float aR = buf[static_cast<uint64_t>(i0) * 2 + 1];
                const bool haveNext = (i1 <= lastValid);
                const float bL = haveNext ? buf[static_cast<uint64_t>(i1) * 2 + 0] : aL;
                const float bR = haveNext ? buf[static_cast<uint64_t>(i1) * 2 + 1] : aR;
                sL = Lerp(aL, bL, static_cast<float>(frac));
                sR = Lerp(aR, bR, static_cast<float>(frac));
            } else if (!v.params.loop) {
                exhausted = true;
            }

            float g = v.params.volume * v.fade.Advance();

            float panL, panR;
            if (v.spatial) {
                g *= ComputeAttenuation(m_listener, v.spatialParams);
                const float pan = ComputePan(m_listener, v.spatialParams);
                panL = 1.0f - std::max(0.0f, pan);
                panR = 1.0f + std::min(0.0f, pan);
            } else {
                const float pan = Clamp(v.params.pan, -1.0f, 1.0f);
                panL = 1.0f - std::max(0.0f, pan);
                panR = 1.0f + std::min(0.0f, pan);
            }

            const float totalG = g * busG[f] * masterGains[f];
            dst[f * kMixerChannels + 0] += sL * panL * totalG;
            dst[f * kMixerChannels + 1] += sR * panR * totalG;

            cursor += step;
        }

        // 이월: 다음 블록 히스토리 = 이번 블록에서 마지막으로 유효했던 소스 프레임(=buf[lastValid]).
        //   위상 = cursor 소수부. 다음 블록은 buf[0]=이 히스토리, buf[1]=다음 미읽 소스 프레임이 되어
        //   선형 보간이 프레임 경계에서 연속된다(정확히 소비분만 pull 했으므로 손실 없음).
        if (lastValid >= 0) {
            v.histL = buf[static_cast<uint64_t>(lastValid) * 2 + 0];
            v.histR = buf[static_cast<uint64_t>(lastValid) * 2 + 1];
            v.hasHistory = true;
        }
        v.phase = cursor - std::floor(cursor);

        if (v.stopWhenFadedOut && v.fade.AtTarget() && v.fade.target == 0.0f) {
            v.active = false; v.source.reset();
        } else if (exhausted && !v.params.loop) {
            v.active = false; v.source.reset();
        }
    }
}

} // namespace mye::audio
