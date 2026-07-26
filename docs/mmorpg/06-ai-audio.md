# 06. AI 오디오 생성 & 사운드 디자인 (AI Audio Generation & Sound Design)

> 소유 범위: **텍스트→음악/SFX AI 생성**부터 **미리듣기·채택·임포트·`mye_audio` 배선**까지의 콘텐츠
> 파이프라인, 그리고 그 위에 얹히는 **다이나믹 뮤직(전투 레이어드·스팅어)·2.5D 공간 사운드·리버브존·오클루전**
> 런타임 정책이다. 픽셀 2.5D MMORPG(수백~수천 동접·지연·복제·치트·라이브운영)를 전제로 한다.
> 이 도메인은 **기존 `mye_audio`(믹서·버스·큐·크로스페이드·공간화)를 재구현하지 않고 소비·확장**한다.
> 오디오 재생 정본은 [engine/audio](../06-runtime-systems.md) 및 `AudioEngine`/`SoftwareMixer`이며,
> 여기서는 "그 위에 무엇을 데이터로 얹고, 어떻게 AI로 생성해 채워 넣는가"를 정의한다.

관련 실제 코드(현 인벤토리): `engine/audio/{AudioEngine,SoftwareMixer,AudioCue,AudioTypes,AudioModule}`,
`engine/asset/{AudioImporter,AudioClip}`, `engine/runtime/AudioListener`, `engine/script/bindings/AudioBindings.cpp`,
`tools/mcp/src/tools/dot.ts`(AI 생성 MCP 패턴의 정본 레퍼런스).

---

## 1. 목표·범위

### 1.1 목표

- **콘텐츠 병목 해소.** 1인 개발 MMORPG의 최대 병목은 "수백 종의 오디오 에셋(맵별 BGM, 지형별 발소리,
  스킬별 타격음, 바이옴별 앰비언트, UI음, 보스테마)"을 손으로 못 만든다는 것이다. AI 음악/SFX 생성으로
  **"텍스트 프롬프트 → 미리듣기 → 채택 → WAV/OGG 임포트 → AudioCue/BGM 배선"** 루프를 닫는다.
- **`mye_audio` 위의 데이터화.** 현재 `BusId`는 고정 열거, `AudioCue`는 런타임 표현만 있고 에셋 직렬화가
  없다([AudioCue.h] 주석 명시). 이 도메인은 **AudioCue·MusicSet·SoundBank·AudioBusLayout·ReverbZone을
  에셋(리플렉션+JsonArchive)** 으로 승격해, AI 생성 결과가 데이터로 흘러 들어가게 한다.
- **다이나믹 뮤직.** 전투 진입/이탈, 지역 전환, HP 저역, 보스 페이즈에 따라 **스템 레이어드 + 스팅어**로
  전환되는 인터랙티브 뮤직 시스템을 얹는다(단순 BGM 크로스페이드는 이미 `MusicPlayer`가 함).
- **2.5D 공간 사운드 강화.** 이미 있는 거리 감쇠·X축 패닝(`SpatialParams`) 위에 **리버브존·오클루전·
  바이옴 앰비언스 베드**를 데이터로 얹는다. 픽셀 2.5D이므로 완전 3D HRTF는 비목표.
- **라이브 운영·안전.** 라이선스/저작권/워터마크 메타를 에셋에 각인하고, 배치 생성·사운드팩·네이밍 규약으로
  수백 개 에셋을 결정론적으로 관리한다. 서버 권위/치트 관점(뒤 §5)까지 포함한다.

### 1.2 범위(In / Out)

| 구분 | 포함(In) | 제외(Out, 소유 문서) |
|---|---|---|
| 생성 | 텍스트→음악, 텍스트→SFX, 레퍼런스 오디오→변주, 배치 생성, 사운드팩 | 3D 메시/스프라이트 생성 → [AI 이미지](05-ai-image.md) |
| 파이프라인 | 미리듣기·채택·WAV/OGG 임포트·`.meta`/GUID·핫리로드 | 코어 임포터 골격 → [engine/asset](../04-asset-pipeline.md) |
| 런타임 | 다이나믹 뮤직·스팅어·리버브존·오클루전·앰비언스 베드·발소리 | 믹서/버스/보이스 정본 → [engine/audio](../06-runtime-systems.md) |
| 도구 | MCP `sound_*` 툴, 에디터 사운드 브라우저/미리듣기/채택 | MCP 서버 골격 → [08-mcp](../08-mcp.md) |
| 네트워크 | 클라이언트 사이드 재생 결정론, 서버 사운드 이벤트 복제, CDN 스트리밍 | 넷코드 전송/복제 정본 → [넷코드](07-netcode.md) |
| AI 제공자 | 오디오 생성 제공자 추상화(`IAudioGenProvider`) | 멀티 AI 제공자 공통 추상화 → [AI 이미지](05-ai-image.md) §제공자 |

> 원칙: **오디오 재생은 클라이언트 로컬 이벤트**다. 서버는 "무슨 사운드 이벤트가 일어났는가"(스킬 시전,
> 몹 사망)만 복제하고, 어떤 WAV가 어떻게 믹싱되는지는 클라이언트가 결정한다. 따라서 대역폭·치트 표면이 작다.

---

## 2. 핵심 개념·아키텍처

### 2.1 계층 구조

```
[생성 계층]  MCP sound_* 툴 / 에디터 SoundGenPanel
   │  프롬프트(무드·BPM·길이·루프·스템·레퍼런스) → IAudioGenProvider
   ▼
[제공자 계층]  IAudioGenProvider (텍스트→음악/SFX)
   │  - LocalStub(결정론적 합성: 사인/노이즈/ADSR — 오프라인·CI·무키)
   │  - RemoteHttp(외부 AI API — 키·레이트리밋·워터마크 메타 수집)
   ▼
[임포트 계층]  기존 AudioImporter(WAV/OGG → AudioClip) + AudioMeta(라이선스/워터마크/생성프롬프트)
   │  .meta/GUID 안정화(현 인벤토리 갭 — 여기서 반드시 기록/판독)
   ▼
[데이터 계층]  AudioCue · MusicSet · SoundBank · AudioBusLayout · ReverbZone (에셋, 리플렉션+JsonArchive)
   ▼
[런타임 계층]  AudioEngine(정본) + DynamicMusicDirector + SpatialAudioSystem(리버브존·오클루전·앰비언스)
   │  ← 애니메이션 이벤트 / 스킬 이벤트 / 지역 진입 / 전투 상태 / 서버 사운드 이벤트 복제
   ▼
[출력]  SoftwareMixer(버스 트리 Master←{BGM,SFX,UI,Voice,+Ambient}) → IAudioBackend(miniaudio)
```

### 2.2 재사용 vs 신규(엔진 매핑 요약)

- **재사용(그대로 소비):** `AudioEngine::PostCue/Music/SetListener/SetBusVolume`, `SoftwareMixer`(램프·크로스페이드·
  공간화 정본), `AudioImporter::DecodeWav/DecodeOgg`, `AudioClip`, `AudioListenerBridge`, `mye.audio.*` Lua 바인딩.
- **확장(기존 파일에 추가):** `BusId`에 `Ambient` 추가(또는 데이터화 후 매핑), `AudioClip`/`AudioCue`에 리플렉션
  등록·JsonArchive 직렬화, `AudioImportSettings` 확장(loudness normalize·loop point·stem role), `AudioModule`이
  새 시스템(DynamicMusicDirector·SpatialAudioSystem) 소유·틱.
- **신규(새 파일/모듈):** `engine/audio`에 `MusicSet/DynamicMusicDirector/ReverbZone/SpatialAudioSystem/
  AudioBusLayout/SoundBank`, `engine/asset`에 `AudioMeta`(라이선스/워터마크), `engine/audiogen`(생성 제공자
  추상화), `tools/mcp/src/tools/sound.ts`(MCP `sound_*`), 에디터 `SoundGenPanel`.

### 2.3 "생성→채택" 루프(dot 툴과 동형)

`tools/mcp/src/tools/dot.ts`가 이미 확립한 패턴을 오디오로 이식한다:
- **`dot_write_sprite`**(AI가 직접 그리드를 찍음) ↔ **`sound_synthesize`**(AI가 파라미터로 SFX를 직접 합성 —
  무키·오프라인·결정론적, CI 안전).
- **`dot_from_photo`**(입력→변환) ↔ **`sound_from_reference`**(레퍼런스 오디오→변주/스타일 이식).
- **`sound_generate`**(텍스트→음악/SFX, 외부 제공자) — dot에는 없던 신규(원격 API).
- 공통 규약(dot.ts와 동일): 결과는 리포 상대 경로(`assets/audio/generated/`)에 저장, 안전 경로 검증(`root.ts`),
  base64가 아니라 **파형 썸네일 PNG + 메타 JSON**을 반환(오디오는 이미지처럼 인라인 재생 불가 → AI/사용자는
  파형·길이·라우드니스를 보고 판단, 실제 청취는 에디터/`engine_run`).

---

## 3. 기능 목록

우선순위: **P0**=MMORPG MVP 필수, **P1**=초기 라이브 필요, **P2**=콘텐츠 확장, **P3**=고급/폴리시,
**P4**=선택/실험. 상태: **있음**=현 코드 존재, **부분**=일부만/미배선, **신규**=신설.

| 기능 | 우선순위 | 상태 | 엔진 매핑(추가/재사용) |
|---|---|---|---|
| 오디오 재생 정본(믹서·버스·보이스·크로스페이드·공간화) | P0 | 있음 | **재사용** `AudioEngine`/`SoftwareMixer`([06-runtime-systems]) |
| WAV/OGG 임포트(디코드→`AudioClip`) | P0 | 있음 | **재사용** `AudioImporter::DecodeWav/DecodeOgg` |
| AudioCue 런타임(랜덤/순차·변조·폴리포니·버스) | P0 | 부분 | **확장** `AudioCue`에 리플렉션+JsonArchive 직렬화(현재 런타임 표현만) |
| 안정 GUID·`.meta` 기록/판독 | P0 | 부분 | **확장** `AssetMeta`/`AudioMeta` — 현 인벤토리 갭(매 로드 새 GUID) 해소 |
| `sound_synthesize` MCP(파라미터 SFX 합성, 무키·결정론) | P0 | 신규 | **신규** `tools/mcp/src/tools/sound.ts` + `LocalStub` 제공자 |
| `IAudioGenProvider` 추상화(로컬/원격) | P0 | 신규 | **신규** `engine/audiogen` |
| 텍스트→SFX 생성(외부 AI) | P1 | 신규 | **신규** `RemoteHttp` 제공자 + `sound_generate` |
| 텍스트→음악 생성(무드·BPM·길이·루프·스템) | P1 | 신규 | **신규** `RemoteHttp` 제공자(music 모드) |
| 미리듣기·채택 워크플로우(에디터/MCP) | P1 | 신규 | **신규** `SoundGenPanel` + `sound_audition`/`sound_adopt` |
| 파형 썸네일·라우드니스(LUFS)·길이 리턴 | P1 | 신규 | **신규** MCP 요약(dot 썸네일과 동형) |
| 다이나믹 뮤직(전투 진입/이탈·레이어드 스템) | P1 | 신규 | **신규** `MusicSet`/`DynamicMusicDirector`(`MusicPlayer` 위) |
| 뮤직 스팅어(레벨업·보스등장·승리/패배) | P1 | 신규 | **신규** `DynamicMusicDirector::PlayStinger` |
| 발소리(지형별 SurfaceType→Cue) | P1 | 부분 | **확장** `AnimationEvent`→Cue 배선(현재 데모 하드코딩), `SurfaceType` 조회 |
| 바이옴 앰비언스 베드(숲·동굴·도시·비) | P1 | 신규 | **신규** `AmbienceBed`(지역 진입시 크로스페이드 루프) + `Ambient` 버스 |
| 라이선스/저작권/워터마크 메타 각인 | P1 | 신규 | **신규** `AudioMeta{license,source,prompt,watermark}` |
| 배치 생성·사운드팩(네이밍 규약) | P1 | 신규 | **신규** `sound_batch` + `SoundBank` 에셋(GUID 목록) |
| 리버브존(구역별 리버브/딜레이) | P2 | 신규 | **신규** `ReverbZone` 컴포넌트 + 믹서 send 버스(DSP) |
| 오클루전(벽/장애물 저역통과·감쇠) | P2 | 신규 | **신규** `SpatialAudioSystem`(레이/타일 가시성 → LPF·게인) |
| 보이스 스틸링(폴리포니 초과 우선순위) | P2 | 부분 | **확장** `AudioEngine`(현재 단순 거절 — 인벤토리 갭) |
| 스트리밍 OGG frameCount·seek·진행률 | P2 | 부분 | **확장** `AudioImporter`(현재 frameCount=0 — 인벤토리 갭) |
| AudioBusLayout 데이터화(고정 열거 탈피) | P2 | 부분 | **확장** `BusId`→`AudioBusLayout` 에셋([AudioTypes.h] 주석 명시) |
| 라우드니스 정규화(-14 LUFS 통합)·트루피크 | P2 | 신규 | **신규** 임포트 후처리(`AudioImportSettings.normalizeLufs`) |
| 서버 사운드 이벤트 복제(권위 이벤트→클라 재생) | P2 | 신규 | **신규** 넷코드 연동([07-netcode]) — 사운드 이벤트 스키마 |
| DSP 이펙트(리버브·EQ·저역통과·덕킹) | P3 | 신규 | **신규** `SoftwareMixer` send/insert 슬롯([AudioTypes] 주석) |
| 보이스/외침(대사·전투보이스, Voice 버스) | P3 | 부분 | **재사용** `Voice` 버스 + `AudioCue`(다국어 → [로컬라이즈]) |
| CDN 스트리밍·델타패치·무결성 서명 | P3 | 신규 | **신규** `IFileSystem` 확장([04] pak) + [넷코드] |
| 오클루전 존별 리버브 프리셋 블렌드 | P4 | 신규 | **신규** `ReverbZone` 보간 |
| HRTF/완전 3D 사운드 | P4 | 비목표 | — (픽셀 2.5D 비목표) |

---

## 4. 데이터 모델·스키마

### 4.1 AudioMeta — 라이선스/저작권/워터마크(신규, `engine/asset`)

생성 오디오의 출처·라이선스를 에셋에 각인한다. `.meta` 사이드카에 직렬화(현 인벤토리 갭: `.meta`는
파서만 있고 런타임 미기록 — 이 도메인이 오디오 축에서 먼저 배선한다).

```cpp
// engine/asset/include/mye/asset/AudioMeta.h (신규)
namespace mye::asset {

enum class AudioLicense : uint8_t {
    Unknown = 0, ProprietaryGen, CC0, CCBY, Purchased, RoyaltyFree, Internal
};

struct AudioProvenance {           // 생성 출처(라이브 운영·감사·저작권 대응)
    std::string  provider;         // "local-stub" | "remote:<name>" | "manual"
    std::string  model;            // 제공자 모델/버전 태그
    std::string  prompt;           // 생성 프롬프트(재현·감사)
    uint64_t     seed = 0;         // 결정론 재현 시드(로컬 합성/일부 원격)
    std::string  createdAtUtc;     // ISO8601
    AudioLicense license = AudioLicense::Unknown;
    std::string  licenseUrl;       // 라이선스 원문/증빙 URL
    bool         watermarked = false;   // 제공자 워터마크 포함 여부(그대로 유통)
    std::string  contentHash;      // WAV PCM SHA-256(중복·무결성·서명 기반)
};

// AudioClip 확장 메타(임포트/재생 힌트) — .meta settings 로 왕복.
struct AudioAssetMeta {
    AudioProvenance provenance;
    // 재생 힌트
    float    loopStartSec = -1.0f;   // <0 = 루프 없음
    float    loopEndSec   = -1.0f;
    float    gainDb       = 0.0f;    // 임포트 후 정규화 오프셋(LUFS 정규화 결과)
    std::string busHint;             // "BGM" | "SFX" | "UI" | "Voice" | "Ambient"
    std::string stemRole;            // "drums" | "bass" | "lead" | "pad" | "" (음악 스템)
};

} // namespace mye::asset
```

### 4.2 AudioCue 직렬화(확장) — `.audiocue` 에셋

기존 [AudioCue.h]는 런타임 표현(비소유 `const AudioClip*` 포인터)만 가진다. 디스크 에셋은 **GUID 참조**로
저장하고, 로드 시 `AssetManager`가 포인터로 해석한다.

```jsonc
// assets/audio/cues/sword_swing.audiocue.json
{
  "$type": "mye::audio::AudioCue",
  "clips": [                       // GUID 참조(안정) — 런타임에 const AudioClip* 로 해석
    "guid:8f2a...c1", "guid:8f2a...c2", "guid:8f2a...c3"
  ],
  "selection": "Random",           // Random | Sequential
  "bus": "SFX",
  "volumeMin": 0.85, "volumeMax": 1.0,
  "pitchMin": 0.95,  "pitchMax": 1.08,
  "loop": false,
  "maxVoices": 4,                  // 폴리포니 상한(0=무제한)
  "priority": 10,                  // 보이스 스틸링 우선순위
  "cooldownMs": 30,                // 동일 큐 연타 방지(치트/스팸 완화)
  "audibleRadius": 24.0,           // 이 반경 밖 리스너면 재생 생략(스케일 최적화)
  "provenance": { "provider": "manual", "license": "Internal" }
}
```

### 4.3 MusicSet · DynamicMusicDirector(신규, `engine/audio`)

다이나믹 뮤직: 하나의 곡을 **스템(stem) 레이어**로 나눠 전투 강도에 따라 크로스페이드로 얹고, 순간 이벤트는
**스팅어**로 원샷 재생한다. `MusicPlayer`(2보이스 크로스페이드)는 베이스 레이어 전환에 재사용한다.

```cpp
// engine/audio/include/mye/audio/MusicSet.h (신규)
namespace mye::audio {

struct MusicLayer {                     // 동일 BPM·길이·정렬(동기 재생) 스템
    asset::AssetGuid clip;              // 상주 or 스트리밍
    int   intensityMin = 0;             // 이 레이어가 켜지는 강도 구간 [min,max]
    int   intensityMax = 3;             // 0=탐험 1=경계 2=전투 3=보스 등(게임 정의)
    float baseGain = 1.0f;
};

struct MusicSet {                       // 한 지역/상황의 인터랙티브 트랙 묶음
    std::string        id;              // "forest_zone" | "boss_ifrit"
    std::vector<MusicLayer> layers;     // 강도별 켜지는 스템들(동기 루프)
    float              bpm = 120.0f;    // 스팅어 박자 정렬용
    float              loopBars = 8.0f; // 스팅어를 마디 경계에 맞추기 위한 정보
    std::map<std::string, asset::AssetGuid> stingers; // "levelup","boss_appear","victory","defeat"
    float              transitionSec = 1.5f; // 강도 전환 크로스페이드
};

// 런타임 디렉터 — AudioModule 이 소유·틱. MusicPlayer(베이스) + 믹서 보이스(스템) 조율.
class DynamicMusicDirector {
public:
    void SetMusicSet(const MusicSet& set, float fadeInSec);  // 지역 진입
    void SetIntensity(int level, float xfadeSec = -1.0f);    // 전투 상태머신이 호출
    void PlayStinger(const std::string& id, bool quantizeToBar = true); // 레벨업/보스등장
    void Clear(float fadeOutSec);
    void Update(float dt);   // 스템 게인 램프·마디 카운터·스팅어 예약 소비
};

} // namespace mye::audio
```

전투 강도 전이(intensity)는 [게임플레이 프레임워크](08-gameplay.md)의 전투 상태에서 밀어준다(예: 근처
적대 몹 수·보스 페이즈). 이 도메인은 **디렉터와 데이터 스키마**만 정의하고, 전투 판정은 소비하지 않는다.

### 4.4 SpatialAudioSystem · ReverbZone · AmbienceBed(신규)

```cpp
// engine/audio/include/mye/audio/ReverbZone.h (신규 — ECS 컴포넌트 + 시스템)
struct ReverbZone {                     // 구역 컴포넌트(폴리곤/원). 시스템이 리스너 포함 판정.
    float wetGain = 0.25f;              // 리버브 send 량
    float roomSizeSec = 1.2f;           // 프리셋(동굴>홀>방)
    float dampingHz = 4000.0f;          // 고역 감쇠
    int   priority = 0;                 // 겹칠 때 높은 우선순위 채택(또는 블렌드)
    std::string ambienceBed;            // 이 존의 앰비언스 MusicSet/Cue id(선택)
};

struct OcclusionParams {                // 소스-리스너 사이 차폐(타일/콜라이더 가시성)
    float lowPassHz = 900.0f;           // 완전 차폐 시 컷오프
    float occludedGainMul = 0.5f;       // 차폐 감쇠
};
```

- **오클루전**은 [scene] 타일맵/콜라이더 가시성(레이캐스트 또는 타일 solidity)을 샘플해 소스별 LPF 컷오프·게인
  배수를 산출한다. 픽셀 2.5D이므로 "리스너와 소스 사이에 벽 타일이 있으면 저역만 통과" 정도의 저비용 근사.
- **앰비언스 베드**는 지역/바이옴 진입 시 루프 사운드를 `Ambient` 버스로 크로스페이드(비 오는 숲, 동굴 물방울,
  도시 인파). `DynamicMusicDirectorEnter`와 동일 패턴이되 SFX성 루프라 별도 버스로 볼륨 독립 제어.

### 4.5 SoundBank(신규) — 배치 생성·사운드팩 산출물

```jsonc
// assets/audio/banks/skills_fire.soundbank.json — 배치 생성 결과 묶음
{
  "$type": "mye::audio::SoundBank",
  "id": "skills_fire",
  "cues":  ["guid:...", "guid:..."],   // 이 팩이 포함하는 AudioCue GUID들
  "clips": ["guid:...", "guid:..."],   // 원본 클립 GUID들
  "generatedBy": "sound_batch",
  "manifest": "assets/audio/generated/skills_fire/manifest.json"
}
```

### 4.6 네이밍 규약(배치·중복 방지)

```
assets/audio/generated/<pack>/<category>_<subject>_<variant>[_v<n>].wav
  category ∈ {bgm, sfx, amb, ui, voice, stinger}
  예: sfx_sword_swing_01.wav, bgm_forest_explore.ogg, amb_cave_drip.ogg,
      stinger_boss_appear.wav, ui_button_click.wav
```
- 결정론: `sound_synthesize`/`from_reference`는 `seed`가 고정되면 동일 바이트를 낸다(CI·재현).
- 충돌: 동일 이름 존재 시 `contentHash` 비교 → 동일이면 스킵(idempotent), 다르면 `_v2` 승격(덮어쓰기 금지).

---

## 5. 경우의 수·엣지케이스 (exhaustive)

### 5.1 생성(Generation) 실패·품질

| 상황 | 처리 |
|---|---|
| 원격 AI API 키 없음/무효 | `RemoteHttp` 즉시 `Error`, MCP는 명확한 안내 + `LocalStub`(무키 합성)로 폴백 제안. CI는 항상 `LocalStub`. |
| API 레이트리밋/쿼터 초과 | 429 백오프(지수) + 큐잉. 배치는 부분 성공 허용(성공분 채택, 실패분 재시도 목록 반환). |
| API 타임아웃/네트워크 단절 | `job` 시스템 IO 큐로 비동기, N초 타임아웃 후 취소·정리(고아 임시파일 삭제). |
| 생성물 포맷 불일치(mp3 등 미지원) | 임포트 전 트랜스코드 게이트: 지원(WAV/OGG) 외는 거부 또는 사전 변환 필수 명시. |
| 생성물 손상/0바이트/무음 | `AudioImporter` 디코드 실패 → 채택 차단. 무음 검출(RMS<임계) 경고. |
| 클리핑(트루피크>0dBFS) | 임포트 후처리에서 정규화(-1dBTP) 또는 경고. `SoftwareMixer::RenderInto`는 클리핑 안 함(수치검증) → 채택 전 방지. |
| 라우드니스 편차(트랙마다 볼륨 제각각) | LUFS 통합 측정 → `gainDb` 오프셋으로 -14 LUFS 정규화(음악)/-18(SFX). |
| 길이 초과(BGM 10분) | 상한 clamp + 경고. 스트리밍 강제(상주 PCM 메모리 폭발 방지). |
| 루프 이음새 클릭/팝 | 루프 포인트 제로크로싱 스냅 + 마이크로 크로스페이드(믹서 `GainRamp` 재사용, `MusicPlayer` 팝방지와 동일). |
| 스템 간 위상/BPM 불일치 | `MusicSet` 스키마가 동일 BPM·길이·정렬 요구. 검증 실패 시 채택 차단(디렉터 동기 재생 전제 붕괴 방지). |
| 프롬프트가 저작권 있는 곡명/아티스트 지시 | 프롬프트 필터(경고) + `provenance.prompt` 각인으로 사후 감사. |

### 5.2 임포트·에셋(현 인벤토리 갭 직접 연결)

| 상황 | 처리 |
|---|---|
| 매 로드 새 GUID 생성(현 `AssetManager` 갭) | `AudioMeta`가 `.meta`에 GUID 기록/판독 → 실행 간 안정 참조(AudioCue/MusicSet가 GUID로 참조 가능). |
| 스트리밍 OGG `frameCount=0`(현 갭) | 임포트 시 전체 프레임 산정(또는 vorbis 총 샘플 질의)로 채워 길이·진행률·seek 지원. |
| `AudioImportSettings.settings=nullptr`(현 갭) | `.meta` settings JSON → `AudioAssetMeta` 역직렬화 배선(sRGB 대신 오디오: mono/stream/normalize). |
| 핫리로드로 클립 교체 중 재생 | `AssetDatabase` in-place 슬롯 스왑 + 재생 중 보이스는 다음 재생부터 신클립(현 재생 안전 종료). |
| 대량 임포트(사운드팩 200개) | `job` 병렬 디코드 + 임포트 캐시(현 갭: 매번 재디코드) → 바이너리 캐시 굽기. |
| 중복 콘텐츠(같은 소리 여러 이름) | `contentHash` 인덱스로 중복 검출·경고(디스크·메모리 절약). |
| pak 패키징 시 평문(현 갭) | 라이브 배포는 압축/서명(무결성)·선택 암호화([04] pak 확장) — 유료 사운드팩 보호. |

### 5.3 런타임 재생·스케일(수백~수천 동접)

| 상황 | 처리 |
|---|---|
| 화면 밖/원거리 소스 대량 발생(먼 전투 100건) | `AudioCue.audibleRadius` + 리스너 거리 컬링 → 감쇠 0이면 재생 자체 생략(보이스 낭비 제거). |
| 폴리포니 초과(같은 발소리 수십) | 보이스 스틸링(현재 단순 거절 갭): `priority`+최고령 기준 스틸. Cue별 `maxVoices` 상한. |
| 동일 큐 연타(치트·연사) | `cooldownMs`로 동일 큐 최소 간격. UI 클릭 스팸도 동일. |
| 전투 강도 급변(진입↔이탈 깜빡임) | 디렉터 히스테리시스(진입/이탈 임계 분리) + 최소 유지시간 → 스템 채터링 방지. |
| 스팅어와 마디 경계 불일치 | `quantizeToBar` 시 다음 마디까지 예약(디렉터 마디 카운터). 즉시 필요한 UI음은 비양자화. |
| 리버브존 다중 겹침 | `priority` 최고 채택 또는 가중 블렌드(§4.4). 경계에서 부드럽게 보간(팝 방지). |
| 오클루전 매 프레임 레이캐스트 비용 | 소스별 저주기 갱신(예: 4프레임마다) + 이동 시에만 재평가. 거리 컬링된 소스는 skip. |
| 앰비언스 지역 빠른 통과(경계 왕복) | 크로스페이드 최소 유지 + 디바운스. |
| 오디오 콜백 언더런(생성/디코드가 콜백 블로킹) | 생성/디코드는 절대 오디오 스레드에서 안 함(`AudioEngine` 커맨드 큐 규약 준수) — 메인/IO 스레드만. |
| 장치 없음/실패(무음 모드) | 이미 `AudioEngine`이 무음 폴백. 생성/미리듣기는 파형 썸네일로 대체(청취 불가 환경). |
| 샘플레이트 불일치(48k 클립, 44.1k 출력) | `SoftwareMixer` 리샘플 정본 재사용(보이스 위상 연속성 이미 구현). |

### 5.4 네트워크·복제·치트(MMO 필수)

| 상황 | 처리 |
|---|---|
| 서버가 사운드 이벤트를 복제(스킬 시전 목격) | 서버는 `SoundEventId + worldPos + params 시드`만 복제([07-netcode]) — WAV 전송 아님(대역폭 최소). |
| 지연으로 사운드가 시각보다 늦음 | 클라이언트 예측 재생(로컬 액션은 즉시) + 서버 확인 시 보정. 원격 관찰은 스냅샷 시점 재생. |
| 클라이언트가 위조 사운드 이벤트로 정보 취득 | 사운드는 클라 로컬 → 서버 권위 상태(누가 어디 있는지)는 [넷코드]가 관심관리로 필터. 안 보이는 적 사운드 미전송(월핵성 정보 유출 차단). |
| 사운드로 벽 너머 적 위치 파악(치트 유리) | 오클루전+관심관리: 서버가 시야/청각 반경 밖 이벤트를 애초에 안 보냄. 오클루전은 클라 폴리시(정보 균등). |
| 클라이언트 볼륨 조작(발소리 증폭 치트) | 방지 불가(로컬 믹싱)·비목표. 대신 서버가 "들려야 할 최대 반경"을 권위로 제한. |
| CDN에서 사운드팩 스트리밍(패치 지연) | 필수 코어팩은 클라 동봉, 지역 팩은 프리페치. 미도착 시 플레이스홀더(무음/기본음) 폴백. |
| 사운드팩 변조(재분배) | pak 서명 검증([04])·워터마크 메타. 유료 자산은 서명 필수. |
| 대규모 동접 지역 오디오 폭주 | 클라 로컬 컬링으로 서버 무관(오디오는 대역폭 안 씀). 서버는 이벤트 관심관리만. |

### 5.5 라이브 운영·법무·재현

| 상황 | 처리 |
|---|---|
| 사후 저작권 클레임(어떤 트랙이 문제?) | `AudioProvenance`(provider/model/prompt/seed/license) 전수 각인 → 감사·대체 생성 가능. |
| 제공자 워터마크 포함 유통 | `watermarked=true` 각인, 상용 배포 전 화이트리스트 검토. 무워터마크 요구 트랙 별도 관리. |
| 결정론 재현(같은 프롬프트 재생성) | `LocalStub`은 seed로 완전 재현. 원격은 seed 지원 시 재현, 아니면 결과 바이트 보관. |
| A/B·핫스왑(라이브 중 BGM 교체) | 에셋 GUID 유지+핫리로드 → 클라 재접속 없이 교체. 서버 config로 MusicSet id 푸시(원격 config는 [01] 갭). |
| 지역별 오디오 규제/현지화 | Voice 버스는 로케일별 클립([로컬라이즈]) — MusicSet은 공용, Voice만 스왑. |

---

## 6. 신규 모듈·파일 제안 (구체 경로)

### 6.1 생성 제공자 — `engine/audiogen`(신규 모듈)

```
engine/audiogen/
  include/mye/audiogen/
    IAudioGenProvider.h     // 텍스트→음악/SFX 추상(제공자 무관). request/result 구조체.
    AudioGenTypes.h         // AudioGenRequest{mode,prompt,seconds,bpm,loop,stems,reference,seed,mood}
    LocalStubProvider.h     // 결정론 합성(사인/노이즈/ADSR/필터) — 무키·CI·오프라인
    RemoteHttpProvider.h    // 외부 AI API(키·레이트리밋·백오프·워터마크 메타 수집)
    AudioGenModule.h        // 서비스 등록(EngineContext), 제공자 선택(config)
  src/ ...
```
- `IAudioGenProvider`는 [AI 이미지](05-ai-image.md)의 멀티 제공자 추상화와 **동일 패턴**(가능하면 공통
  `IGenProvider` 상위 공유). `Expected<AudioGenResult,Error>` 반환(예외불사용 규약).
- `LocalStubProvider`가 P0: 외부 의존 0, `sound_synthesize`/CI를 무키로 성립시킨다.

### 6.2 오디오 데이터·런타임 — `engine/audio`(기존 모듈에 추가)

```
engine/audio/include/mye/audio/
  MusicSet.h · DynamicMusicDirector.h   // 다이나믹 뮤직(스템 레이어·스팅어)
  ReverbZone.h · SpatialAudioSystem.h   // 리버브존·오클루전·앰비언스 베드(ECS 컴포넌트+시스템)
  AudioBusLayout.h                      // 버스 데이터화(BusId 고정 열거 탈피)
  SoundBank.h                           // 사운드팩(GUID 묶음)
  (AudioCue.h 확장)                     // 리플렉션 등록 + JsonArchive 직렬화
  (AudioTypes.h 확장)                   // BusId::Ambient 추가 / send 슬롯
(AudioModule.cpp 확장)                  // 새 시스템 소유·PostUpdate 틱·config 섹션
```

### 6.3 에셋 메타 — `engine/asset`(추가)

```
engine/asset/include/mye/asset/AudioMeta.h   // AudioProvenance/License/AudioAssetMeta
(AudioImporter 확장)                          // frameCount 산정·LUFS 정규화·loop point·.meta settings 배선
```

### 6.4 도구·에디터

```
tools/mcp/src/tools/sound.ts   // sound_synthesize · sound_generate · sound_from_reference
                               // · sound_batch · sound_audition · sound_adopt  (dot.ts 패턴)
apps/editor/.../SoundGenPanel  // 프롬프트 입력·파형 미리듣기·채택→에셋 배선·프로버넌스 표시
```
- MCP는 `tools/mcp/src/index.ts`에 `registerSoundTools(server, ctx)` 한 줄 추가(dot과 동형).
- 출력 경로 `assets/audio/generated/` — `root.ts` 안전 검증 재사용.
- 반환은 **파형 썸네일 PNG(dot의 `saveAndReturn` 이미지) + 메타 JSON**(길이·LUFS·채널·라이선스).

### 6.5 스크립트 바인딩(확장, `engine/script`)

```
mye.audio.music_set(id)            -- 지역 MusicSet 전환
mye.audio.intensity(level)         -- 전투 강도(디렉터)
mye.audio.stinger(id)              -- 스팅어(레벨업/보스등장)
mye.audio.play_cue(name, x, y)     -- (기존 재사용)
```
게임 디자이너가 만질 표면만 Lua 노출(00 비대칭 원칙). 제공자·생성 API는 C++/MCP 전용.

---

## 7. 마일스톤 단계 (작은 검증가능 단위)

| 단계 | 산출물 | 검증(테스트/CI) |
|---|---|---|
| **A0. 에셋 안정화** | `AudioMeta`+`.meta` GUID 기록/판독, `AudioCue` 직렬화 | 왕복 테스트(저장→로드 GUID 동일), 매 로드 새 GUID 갭 해소 확인 |
| **A1. LocalStub + `sound_synthesize`** | 결정론 합성 제공자 + MCP 툴(파형 썸네일 반환) | 동일 seed→동일 contentHash, WAV 디코드 성공, CI 무키 |
| **A2. 임포트 후처리** | frameCount 산정, LUFS 정규화, loop point 스냅 | 스트리밍 길이>0, 정규화 후 LUFS 목표±0.5 |
| **A3. AudioCue 데이터 배선** | GUID 참조 큐 로드→`PostCue`, cooldown·audibleRadius·거리컬링 | 헤드리스 믹서 RenderInto로 재생/컬링 수치 검증 |
| **B1. 다이나믹 뮤직** | `MusicSet`/`DynamicMusicDirector`(스템 레이어·강도 전이·스팅어) | 강도 전이 크로스페이드·히스테리시스, 스팅어 양자화 |
| **B2. 발소리·앰비언스** | AnimationEvent→SurfaceType→Cue, `Ambient` 버스 앰비언스 베드 | village_demo 데모 배선(현 하드코딩 대체), 지역 크로스페이드 |
| **B3. RemoteHttp + `sound_generate`/`sound_batch`** | 텍스트→음악/SFX(키·레이트리밋·백오프), 배치·사운드팩 | 폴백(무키→LocalStub), 부분 성공, 프로버넌스 각인 |
| **C1. 미리듣기·채택 에디터** | `SoundGenPanel`(프롬프트·파형·채택→에셋) | 채택 후 인스펙터에 프로버넌스/라이선스 표시 |
| **C2. 공간 사운드 강화** | `ReverbZone`·오클루전(저주기 레이·LPF)·리버브 send | 존 진입 wet 변화, 벽 너머 LPF 게인 저하 |
| **C3. 넷코드 연동** | 서버 사운드 이벤트 스키마·관심관리 필터 재생 | 안 보이는 소스 미재생(치트 정보 유출 차단) — [07-netcode] 공동 |
| **D. 보이스 스틸링·스트리밍·DSP·서명** | 폴리포니 스틸, DSP send(리버브/EQ/덕킹), pak 서명 | 폴리포니 상한 준수, 덕킹(대사 시 BGM 감쇠) |

각 단계는 `mye_audio`의 헤드리스 오프라인 렌더(`SoftwareMixer::RenderInto`, `AudioEngine::RenderOffline`)로
장치 없이 수치 검증한다(현 AudioPlayback 16·AudioHotReload 9 테스트 패턴 재사용).

---

## 8. 의존성·타 도메인 문서 참조

### 8.1 이 도메인이 의존(소비)

- [engine/audio 정본 — 06 런타임 시스템](../06-runtime-systems.md): 믹서·버스·보이스·크로스페이드·공간화·`AudioCue` 원형.
- [engine/asset — 04 에셋 파이프라인](../04-asset-pipeline.md): VFS·임포터 골격·`.meta`/GUID·핫리로드·pak.
- [engine/scene — 03 씬·월드](../03-scene-world.md): 애니메이션 이벤트·타일 SurfaceType·콜라이더(발소리·오클루전).
- [engine/script — 05 스크립팅](../05-scripting-plugins.md): `mye.audio.*` 바인딩(게임 로직이 사운드 트리거).
- [08 MCP](../08-mcp.md): `sound_*` 툴 등록·경로 안전·요약 반환 규약(dot 툴 정본 패턴).
- [01 코어·플랫폼](../01-core-platform.md): Job(생성/디코드 비동기)·Config(제공자 키·버스 볼륨)·EventBus.

### 8.2 타 MMORPG 도메인 문서(상호참조)

- [05. AI 이미지 생성 & 픽셀 아트](05-ai-image.md) — 멀티 AI 제공자 추상화(`IGenProvider`) 공유, 생성→채택 UX 공통.
- [07. 넷코드 & 서버](07-netcode.md) — 서버 사운드 이벤트 복제·관심관리·CDN 스트리밍·pak 서명.
- [08. 게임플레이 프레임워크](08-gameplay.md) — 전투 강도(다이나믹 뮤직)·스킬 이벤트(타격 SFX)·레벨업(스팅어).
- [02. 렌더링](../02-rendering.md) — 파티클/이펙트와 SFX 동기(타격 이펙트↔타격음)는 렌더 이벤트 타이밍 참조.
- [게임 런타임 앱·데이터드리븐 부트스트랩](01-runtime-app.md) — 사운드 시스템의 실제 런타임 배선 진입점(현 갭).
- [계정·영속화](09-persistence.md) — 사용자 오디오 설정(버스 볼륨) 저장은 Config/세이브([06] 세이브)와 공유.

> 문서 번호는 `docs/mmorpg/` 도메인 인덱스 규약을 따른다. 아직 미작성 문서는 링크만 예약(선행 도메인이
> 채운다). 재생/에셋의 상세 규약이 충돌하면 **기존 `docs/06`·`docs/04`가 정본**이고, 본 문서는 그 위의
> 데이터·생성·다이나믹/공간 정책만 소유한다(00 §3 의존 규칙).

---

## 이 도메인 요약 3줄

1. **`mye_audio`(믹서·버스·큐·크로스페이드·공간화)는 이미 견고하므로 재구현하지 않고 소비한다.** 이 도메인의
   본질은 그 위에 **AudioCue·MusicSet·ReverbZone·SoundBank를 에셋으로 데이터화**하고, **AI로 그 데이터를 채우는
   생성 파이프라인(텍스트→음악/SFX→미리듣기→채택→임포트→배선)** 을 얹는 것이다.
2. **생성은 dot 툴(`dot_write_sprite`/`dot_from_photo`)이 확립한 MCP 패턴을 오디오로 이식**한다 —
   `sound_synthesize`(무키·결정론·CI 안전)·`sound_generate`(외부 AI)·`sound_from_reference`·`sound_batch`,
   그리고 `IAudioGenProvider`(로컬 스텁/원격) 추상화. 반환은 파형 썸네일+라이선스/프로버넌스 메타.
3. **MMO 스케일·치트·라이브 운영을 1급으로 다룬다** — 오디오는 클라이언트 로컬 이벤트라 대역폭·치트 표면이
   작지만, **서버 사운드 이벤트는 관심관리로 필터**(월핵성 정보 유출 차단)하고, **거리 컬링·보이스 스틸링·
   쿨다운·오클루전**으로 수천 동접에서도 보이스 낭비를 막으며, **프로버넌스/라이선스/워터마크**를 전수 각인해
   저작권·감사·재현을 보장한다.
