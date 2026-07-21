// AudioHotReloadTests.cpp — mye_asset M3-A: AudioImporter(WAV 디코드) + 핫 리로드 파이프라인
//
// 외부 도구(aseprite/오디오 인코더)에 의존하지 않는다. WAV 는 테스트 내에서 RIFF/PCM 바이트를
// 직접 합성(코드 생성 사인파)해 DecodeWav 왕복 정확성을 검증한다. OGG 는 유효 바이트를 손으로
// 만들 수 없어(인코더 없음) 디코드 실패/스트리밍 프로브 거부 경로만 검증한다(notes 참조).
//
// 핫 리로드: 임시 파일을 조작하고 Win32DirectoryWatcher → AssetDatabase::ImportDirty → 슬롯
// in-place 스왑 → AssetReloadedEvent 발행을 왕복 확인한다. 워처 통보는 비동기라 폴링 대기한다.
#include "TestFramework.h"

#include "mye/asset/AssetDatabase.h"
#include "mye/asset/AssetHandle.h"
#include "mye/asset/AssetManager.h"
#include "mye/asset/AudioClip.h"
#include "mye/asset/AudioImporter.h"
#include "mye/asset/FileSystem.h"
#include "mye/asset/FileWatcher.h"
#include "mye/core/Events.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace mye;
using namespace mye::asset;

namespace {

void PushU16LE(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
}
void PushU32LE(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
}
void PushTag(std::vector<uint8_t>& v, const char* t) {
    v.push_back(static_cast<uint8_t>(t[0]));
    v.push_back(static_cast<uint8_t>(t[1]));
    v.push_back(static_cast<uint8_t>(t[2]));
    v.push_back(static_cast<uint8_t>(t[3]));
}

// 16bit PCM WAV(모노/스테레오)를 합성한다. samples: 인터리브드 int16.
std::vector<uint8_t> MakeWav16(uint32_t sampleRate, uint16_t channels,
                               const std::vector<int16_t>& samples) {
    const uint16_t bitsPerSample = 16;
    const uint16_t blockAlign = channels * (bitsPerSample / 8);
    const uint32_t byteRate = sampleRate * blockAlign;
    const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * sizeof(int16_t));

    std::vector<uint8_t> w;
    PushTag(w, "RIFF");
    PushU32LE(w, 36 + dataBytes);   // RIFF chunk size
    PushTag(w, "WAVE");
    // fmt
    PushTag(w, "fmt ");
    PushU32LE(w, 16);               // PCM fmt size
    PushU16LE(w, 0x0001);           // PCM
    PushU16LE(w, channels);
    PushU32LE(w, sampleRate);
    PushU32LE(w, byteRate);
    PushU16LE(w, blockAlign);
    PushU16LE(w, bitsPerSample);
    // data
    PushTag(w, "data");
    PushU32LE(w, dataBytes);
    for (int16_t s : samples) PushU16LE(w, static_cast<uint16_t>(s));
    return w;
}

// 32bit float WAV(모노)를 합성한다.
std::vector<uint8_t> MakeWavF32(uint32_t sampleRate, const std::vector<float>& samples) {
    const uint16_t channels = 1, bitsPerSample = 32;
    const uint16_t blockAlign = channels * (bitsPerSample / 8);
    const uint32_t byteRate = sampleRate * blockAlign;
    const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * sizeof(float));

    std::vector<uint8_t> w;
    PushTag(w, "RIFF"); PushU32LE(w, 36 + dataBytes); PushTag(w, "WAVE");
    PushTag(w, "fmt "); PushU32LE(w, 16);
    PushU16LE(w, 0x0003);   // IEEE float
    PushU16LE(w, channels); PushU32LE(w, sampleRate); PushU32LE(w, byteRate);
    PushU16LE(w, blockAlign); PushU16LE(w, bitsPerSample);
    PushTag(w, "data"); PushU32LE(w, dataBytes);
    for (float f : samples) {
        uint32_t bits; std::memcpy(&bits, &f, sizeof(float));
        PushU32LE(w, bits);
    }
    return w;
}

std::span<const std::byte> AsBytes(const std::vector<uint8_t>& v) {
    return {reinterpret_cast<const std::byte*>(v.data()), v.size()};
}

// 사인파 int16 샘플(모노) 생성.
std::vector<int16_t> SineS16(uint32_t sampleRate, double freq, uint32_t frames, double amp = 0.5) {
    std::vector<int16_t> out(frames);
    for (uint32_t i = 0; i < frames; ++i) {
        double t = static_cast<double>(i) / sampleRate;
        double v = amp * std::sin(2.0 * 3.14159265358979323846 * freq * t);
        out[i] = static_cast<int16_t>(std::lround(v * 32767.0));
    }
    return out;
}

int g_audioTempCounter = 0;
std::filesystem::path MakeTempDir() {
    auto base = std::filesystem::temp_directory_path() /
                ("mye_audio_test_" + std::to_string(g_audioTempCounter++));
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    std::filesystem::create_directories(base, ec);
    return base;
}

void WriteFile(const std::filesystem::path& p, const std::vector<uint8_t>& bytes) {
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

// ===========================================================================
// WAV 디코드 — 16bit PCM 사인파 왕복
// ===========================================================================
MYE_TEST(AudioDecodeWav16MonoRoundTrip) {
    const uint32_t sr = 44100;
    auto samples = SineS16(sr, 440.0, 256);
    auto wav = MakeWav16(sr, 1, samples);

    AudioImportSettings settings;   // 기본: S16 타겟, 모노 유지
    auto clip = AudioImporter::DecodeWav(AsBytes(wav), settings);
    MYE_EXPECT(clip);
    if (clip) {
        const AudioClip& c = clip.Value();
        MYE_EXPECT(c.sampleRate == sr);
        MYE_EXPECT(c.channels == 1);
        MYE_EXPECT(c.format == AudioSampleFormat::S16);
        MYE_EXPECT(c.frameCount == 256);
        MYE_EXPECT(c.samples.size() == 256 * sizeof(int16_t));
        // 샘플 왕복(±1 허용: f32 승격→S16 재양자화 반올림 오차).
        const auto* out = reinterpret_cast<const int16_t*>(c.samples.data());
        bool ok = true;
        for (uint32_t i = 0; i < 256; ++i) {
            int diff = std::abs(static_cast<int>(out[i]) - static_cast<int>(samples[i]));
            if (diff > 1) { ok = false; break; }
        }
        MYE_EXPECT(ok);
        MYE_EXPECT_NEAR(c.DurationSeconds(), 256.0 / sr, 1e-9);
    }
}

// ===========================================================================
// WAV 디코드 — 스테레오, forceMono 다운믹스
// ===========================================================================
MYE_TEST(AudioDecodeWav16StereoDownmix) {
    const uint32_t sr = 22050;
    // L=+10000, R=-10000 인 스테레오 2프레임 → 모노 평균은 0.
    std::vector<int16_t> inter = {10000, -10000, 10000, -10000};
    auto wav = MakeWav16(sr, 2, inter);

    // 스테레오 유지.
    {
        AudioImportSettings s; s.forceMono = false;
        auto clip = AudioImporter::DecodeWav(AsBytes(wav), s);
        MYE_EXPECT(clip);
        if (clip) {
            MYE_EXPECT(clip.Value().channels == 2);
            MYE_EXPECT(clip.Value().frameCount == 2);
        }
    }
    // 모노 다운믹스: 평균 0 근처.
    {
        AudioImportSettings s; s.forceMono = true;
        auto clip = AudioImporter::DecodeWav(AsBytes(wav), s);
        MYE_EXPECT(clip);
        if (clip) {
            MYE_EXPECT(clip.Value().channels == 1);
            MYE_EXPECT(clip.Value().frameCount == 2);
            const auto* out = reinterpret_cast<const int16_t*>(clip.Value().samples.data());
            MYE_EXPECT(std::abs(static_cast<int>(out[0])) <= 1);
            MYE_EXPECT(std::abs(static_cast<int>(out[1])) <= 1);
        }
    }
}

// ===========================================================================
// WAV 디코드 — float 소스 → F32 타겟 유지
// ===========================================================================
MYE_TEST(AudioDecodeWavFloatToF32) {
    const uint32_t sr = 48000;
    std::vector<float> src = {0.0f, 0.5f, -0.5f, 1.0f, -1.0f};
    auto wav = MakeWavF32(sr, src);

    AudioImportSettings s; s.targetFormat = AudioSampleFormat::F32;
    auto clip = AudioImporter::DecodeWav(AsBytes(wav), s);
    MYE_EXPECT(clip);
    if (clip) {
        const AudioClip& c = clip.Value();
        MYE_EXPECT(c.format == AudioSampleFormat::F32);
        MYE_EXPECT(c.frameCount == 5);
        MYE_EXPECT(c.samples.size() == 5 * sizeof(float));
        const auto* out = reinterpret_cast<const float*>(c.samples.data());
        for (size_t i = 0; i < src.size(); ++i) MYE_EXPECT_NEAR(out[i], src[i], 1e-6f);
    }
}

// ===========================================================================
// WAV 디코드 — 잘못된 입력 거부
// ===========================================================================
MYE_TEST(AudioDecodeWavRejectsGarbage) {
    AudioImportSettings s;
    std::vector<std::byte> tiny(10, std::byte{0});
    MYE_EXPECT(!AudioImporter::DecodeWav(tiny, s));

    // RIFF 매직 없음.
    std::vector<uint8_t> notRiff(64, 0xAB);
    MYE_EXPECT(!AudioImporter::DecodeWav(AsBytes(notRiff), s));

    // RIFF 는 맞지만 WAVE 아님.
    std::vector<uint8_t> riffNoWave;
    PushTag(riffNoWave, "RIFF"); PushU32LE(riffNoWave, 36); PushTag(riffNoWave, "XXXX");
    riffNoWave.resize(64, 0);
    MYE_EXPECT(!AudioImporter::DecodeWav(AsBytes(riffNoWave), s));
}

// ===========================================================================
// OGG 디코드 — 유효 인코더가 없어 실패 경로만 검증(합성 불가, notes 참조)
// ===========================================================================
MYE_TEST(AudioDecodeOggRejectsInvalid) {
    AudioImportSettings s;
    MYE_EXPECT(!AudioImporter::DecodeOgg({}, s));   // 빈 입력

    std::vector<uint8_t> garbage(128, 0xCD);
    MYE_EXPECT(!AudioImporter::DecodeOgg(AsBytes(garbage), s));

    // 스트리밍 프로브도 손상 입력을 거부(헤더 오픈 실패).
    AudioImportSettings ss; ss.streaming = true;
    MYE_EXPECT(!AudioImporter::DecodeOgg(AsBytes(garbage), ss));
}

// ===========================================================================
// AudioImporter::Import — 확장자 라우팅 + AssetManager LoadSync 경로
// ===========================================================================
MYE_TEST(AudioImporterLoadSyncWav) {
    auto dir = MakeTempDir();
    auto samples = SineS16(44100, 440.0, 128);
    WriteFile(dir / "step.wav", MakeWav16(44100, 1, samples));

    VirtualFileSystem vfs;
    vfs.Mount("assets", std::make_unique<LooseFileSystem>(dir.string()), 0);
    AssetManager mgr(vfs, /*device=*/nullptr);
    mgr.RegisterImporter(std::make_unique<AudioImporter>());

    AssetHandle<AudioClip> h = mgr.LoadSync<AudioClip>("assets://step.wav");
    MYE_EXPECT(h.State() == AssetState::Loaded);
    AudioClip* clip = h.Get();
    MYE_EXPECT(clip != nullptr);
    if (clip) {
        MYE_EXPECT(clip->sampleRate == 44100);
        MYE_EXPECT(clip->channels == 1);
        MYE_EXPECT(clip->frameCount == 128);
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

// ===========================================================================
// 역의존 전파 — IReloadConsumer 위상 배치 호출 + 추가 dirty 전파(중복 제거)
// ===========================================================================
namespace {
struct CountingConsumer : IReloadConsumer {
    int calls = 0;
    AssetGuid lastChanged;
    std::vector<AssetGuid> emit;   // OnDependencyReloaded 가 반환할 추가 dirty
    void OnDependencyReloaded(AssetGuid changed, std::vector<AssetGuid>& out) override {
        ++calls;
        lastChanged = changed;
        for (const auto& g : emit) out.push_back(g);
    }
};
}  // namespace

MYE_TEST(HotReloadDependencyPropagation) {
    VirtualFileSystem vfs;
    EventBus bus;
    AssetManager mgr(vfs, nullptr);
    AssetDatabase db(mgr, &bus);

    AssetGuid tex = AssetGuid::Generate();     // 리로드 루트(텍스처)
    AssetGuid atlas = AssetGuid::Generate();   // tex 를 소비(아틀라스)
    AssetGuid sheet = AssetGuid::Generate();   // atlas 를 소비(스프라이트시트)

    CountingConsumer atlasConsumer;   // tex 리로드 → 아틀라스 재패킹 → atlas dirty
    atlasConsumer.emit = {atlas};
    CountingConsumer sheetConsumer;   // atlas dirty → 시트 UV 갱신 → sheet dirty
    sheetConsumer.emit = {sheet};

    db.RegisterReloadConsumer(tex, &atlasConsumer);
    db.RegisterReloadConsumer(atlas, &sheetConsumer);

    // AssetReloadedEvent 카운트(루트 + 전파된 파생).
    int reloadEvents = 0;
    bus.Subscribe<AssetReloadedEvent>([&](const AssetReloadedEvent&) { ++reloadEvents; return false; });

    // 직접 전파 트리거(파일 없이 순수 그래프 검증).
    db.PropagateReloads({tex});

    MYE_EXPECT(atlasConsumer.calls == 1);
    MYE_EXPECT(atlasConsumer.lastChanged == tex);
    MYE_EXPECT(sheetConsumer.calls == 1);
    MYE_EXPECT(sheetConsumer.lastChanged == atlas);
    // 전파로 새로 dirty 된 atlas, sheet 두 건의 이벤트가 발행됨.
    MYE_EXPECT(reloadEvents == 2);

    // 소비자 해제 후 재전파 → 호출되지 않음.
    db.UnregisterReloadConsumer(&atlasConsumer);
    db.UnregisterReloadConsumer(&sheetConsumer);
    atlasConsumer.calls = 0; sheetConsumer.calls = 0;
    db.PropagateReloads({tex});
    MYE_EXPECT(atlasConsumer.calls == 0);
    MYE_EXPECT(sheetConsumer.calls == 0);
}

// ===========================================================================
// FindReferencers — 역방향 의존 간선 질의
// ===========================================================================
MYE_TEST(AssetDatabaseReferencers) {
    VirtualFileSystem vfs;
    AssetManager mgr(vfs, nullptr);
    AssetDatabase db(mgr, nullptr);

    AssetGuid a = AssetGuid::Generate(), b = AssetGuid::Generate(), c = AssetGuid::Generate();
    db.AddDependency(a, c);   // a → c
    db.AddDependency(b, c);   // b → c
    auto refs = db.FindReferencers(c);
    MYE_EXPECT(refs.size() == 2);
    bool hasA = false, hasB = false;
    for (auto& g : refs) { if (g == a) hasA = true; if (g == b) hasB = true; }
    MYE_EXPECT(hasA && hasB);
    MYE_EXPECT(db.FindReferencers(a).empty());
}

// ===========================================================================
// 핫 리로드 — 파일 변경 → 워처 → ImportDirty → 슬롯 스왑 → AssetReloadedEvent
// ===========================================================================
MYE_TEST(HotReloadWavEmitsReloadedEventAndSwaps) {
    auto dir = MakeTempDir();
    // 초기: 128 프레임.
    WriteFile(dir / "step.wav", MakeWav16(44100, 1, SineS16(44100, 440.0, 128)));

    VirtualFileSystem vfs;
    vfs.Mount("assets", std::make_unique<LooseFileSystem>(dir.string()), 0);
    EventBus bus;
    AssetManager mgr(vfs, /*device=*/nullptr);
    mgr.RegisterImporter(std::make_unique<AudioImporter>());

    // 먼저 로드(슬롯 확보 — 리로드 대상이 되려면 로드된 상태여야 함).
    AssetHandle<AudioClip> h = mgr.LoadSync<AudioClip>("assets://step.wav");
    MYE_EXPECT(h.State() == AssetState::Loaded);
    MYE_EXPECT(h.Get() && h.Get()->frameCount == 128);

    AssetDatabase db(mgr, &bus);
    db.ScanDirectory(dir.string());

    // AssetReloadedEvent 구독(Publish 는 즉시 디스패치라 Flush 불필요).
    bool reloaded = false;
    AssetTypeId reloadedType = 0;
    bus.Subscribe<AssetReloadedEvent>([&](const AssetReloadedEvent& e) {
        reloaded = true; reloadedType = e.type; return false;
    });

    auto start = db.StartWatching(std::make_unique<Win32DirectoryWatcher>(), dir.string());
    MYE_EXPECT(start);

    // 워처가 무장할 시간을 잠깐 준다.
    std::this_thread::sleep_for(std::chrono::milliseconds(120));

    // 파일 변경: 256 프레임으로 재기록.
    WriteFile(dir / "step.wav", MakeWav16(44100, 1, SineS16(44100, 330.0, 256)));

    // 워처 통보 + 디바운스 경과를 기다리며 ImportDirty 를 폴링(최대 ~3초).
    bool swapped = false;
    for (int i = 0; i < 300 && !swapped; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        db.ImportDirty();
        if (h.Get() && h.Get()->frameCount == 256) swapped = true;
    }

    MYE_EXPECT(swapped);                    // 슬롯 in-place 스왑으로 핸들이 새 데이터를 봄
    MYE_EXPECT(reloaded);                   // AssetReloadedEvent 발행됨
    MYE_EXPECT(reloadedType == AudioClip::kAssetTypeId);
    if (h.Get()) MYE_EXPECT(h.Get()->frameCount == 256);

    // db 는 스코프 종료 시 파괴되며 워처를 안전하게 정지한다(StopAll → 스레드 조인).
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
