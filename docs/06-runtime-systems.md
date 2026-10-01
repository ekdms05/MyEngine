# 06. 게임 UI·텍스트·오디오·런타임

`mye_ui`는 게임 위젯·텍스트, `mye_audio`는 재생, `mye_runtime`은 대화·컷신·NPC·로컬 세이브·로컬라이즈·씬 전환과 오브젝트 조작·동작 실행을 담당한다. 현재 MyGame은 프로젝트의 오브젝트·Lua·모션·맵 전환을 실행하는 로컬 플레이어다. 다른 런타임 모듈의 라이브러리 지원과 플레이어 통합을 구분한다.

## 게임 UI와 텍스트

| 구성 | 역할 | 소스 |
|---|---|---|
| Widget·Widgets | 패널·라벨·버튼·입력·스크롤·목록 | [Widgets.h](../engine/ui/include/mye/ui/Widgets.h) |
| UiSystem | 위젯 트리·레이아웃·히트 테스트·이벤트 | [UiSystem.h](../engine/ui/include/mye/ui/UiSystem.h) |
| UiDocument·UiSkin | UI 데이터·스타일 | [UiDocument.h](../engine/ui/include/mye/ui/UiDocument.h), [UiSkin.h](../engine/ui/include/mye/ui/UiSkin.h) |
| FontFace·GlyphAtlas | FreeType 래스터·동적 글리프 아틀라스 | [FontFace.h](../engine/ui/include/mye/text/FontFace.h), [GlyphAtlas.h](../engine/ui/include/mye/text/GlyphAtlas.h) |
| TextLayout·TextRenderer | 줄바꿈·리치 텍스트·출력 | [TextLayout.h](../engine/ui/include/mye/text/TextLayout.h), [TextRenderer.h](../engine/ui/include/mye/text/TextRenderer.h) |

게임 UI는 자체 위젯/스프라이트 경로이고 에디터는 ImGui다. 화면은 좌상단 원점·+Y 아래, 글리프 R8은 coverage이며 [렌더 계약](02-rendering.md)의 `yDown`·`alphaMask`로 출력한다. 클릭 좌표도 픽셀 타깃의 destRect·배율을 따른다.

TextInput·ScrollView·ListView 로직 구현과 실제 채팅·인벤토리 화면 통합은 별개다. 현재 MyGame에 UI 문서 편집기·타이틀·채팅·인벤토리는 연결되지 않았다. 한글 출력·글꼴 배포·IME 입력·후보창 UX도 각각 검증한다.

## 오디오

[AudioModule](../engine/audio/include/mye/audio/AudioModule.h)은 초기화·장치·종료 수명을 연결한다. [AudioEngine](../engine/audio/include/mye/audio/AudioEngine.h)·SoftwareMixer는 음원·버스·보이스·큐, MusicPlayer는 BGM 전환을 처리한다. 출력 백엔드는 miniaudio다.

```text
WAV/OGG 임포트 → AudioClip → 재생 소스/보이스 → 버스·믹서 → 장치 콜백
```

현재 MyGame은 AudioModule을 등록하지 않는다. 오디오를 연결하는 앱은 음원 핸들을 해제하기 전에 재생 소스를 정리하고, 장치 콜백과 제어 스레드가 공유 상태의 동기화 계약을 지켜야 한다. headless 무음 테스트·장치 초기화·사람의 청취 평가는 서로 다른 검증이다.

고급 리버브·오클루전·전투 레이어 음악·외부 음원 생성 서비스는 현재 기본 재생 기능으로 설명하지 않는다.

## 대화·컷신·NPC

| 기능 | 현재 경로 | 소스 |
|---|---|---|
| 대화 | 데이터·조건·선택지·UI 상태 | [DialogueSystem.h](../engine/runtime/include/mye/runtime/DialogueSystem.h) |
| 컷신 | Lua 코루틴과 대기·이동·대화 연출 | [CutsceneRuntime.h](../engine/runtime/include/mye/runtime/CutsceneRuntime.h) |
| NPC | 배회·접근·상호작용 상태 | [NpcSystem.h](../engine/runtime/include/mye/runtime/NpcSystem.h) |
| 오디오 리스너 | 씬 위치와 청취 위치 연결 | [AudioListener.h](../engine/runtime/include/mye/runtime/AudioListener.h) |

대화·컷신·세이브·씬 전환의 CPU 조합은 `tests/src/RuntimeIntegrationTests.cpp`에서 검증한다. 컷신 이동이 항상 A*·게임의 권위 이동을 사용한다고 가정하지 않는다. MyGame에서 NPC·대화·퀘스트를 사용하는 제품 경로는 별도 통합 항목이다.

## 에디터 오브젝트 실행

[ObjectComponents](../engine/runtime/include/mye/runtime/ObjectComponents.h)는 조작·상호작용·포털·이벤트→동작·Lua 소스의 저장 데이터를 정의한다. [ObjectSystem](../engine/runtime/src/ObjectSystem.cpp)은 고정 틱에서 기존 PhysicsWorld2D·ScriptSystem을 사용한다. 에디터 PlayMode가 World/이벤트 버스보다 먼저 시스템과 VM을 해제하며, 씬 교체는 기존 SceneTransitionManager에 연결한다. 키보드 입력은 앱/에디터가 Vec2·한 번의 상호작용 요청으로 전달한다. 공유 런타임에서 앱을 역참조하지 않는다. 사용법·제한은 [17](17-object-workflow.md)을 따른다.

## 세이브·로컬라이즈·씬 전환

[SaveSystem](../engine/runtime/include/mye/runtime/SaveSystem.h)은 참여자 기반 로컬 게임 저장, [Localization](../engine/runtime/include/mye/runtime/Localization.h)은 문자열 테이블, [SceneTransition](../engine/runtime/include/mye/runtime/SceneTransition.h)은 전환 상태와 로더 경계를 제공한다. 씬 로더의 실제 연결은 소비 앱의 책임이다.

로컬 세이브와 서버 계정·아이템 원장은 서로 다른 데이터 경계다. 서버 저장은 `engine/persist`의 `state.json`·백업·오류 전달을 따르며 인증·경제·데이터 보존을 UI 편의로 간소화하지 않는다.

## 검증과 개선

위젯·글리프·대화·컷신·NPC·세이브·문자열·전환은 자체 테스트에서 검증한다. GPU R8 출력은 실제 DX11 픽셀 검사, 사용자 흐름은 앱 캡처·입력 시나리오로 확인한다. 배포 폰트·UiDocument 연결·MyGame 콘텐츠 통합은 [14](14-development-priorities.md)의 완료 조건을 따른다.
