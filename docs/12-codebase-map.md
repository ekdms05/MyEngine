# 12. 코드 탐색 지도

기준일: **2026-10-01**. 현재 CMake 실행 타깃과 소스 소비자를 기준으로 한 탐색 안내다. 모듈별 의존성·기능 상태는 [13](13-architecture-and-features.md), 작업 규칙은 [AGENTS.md](../AGENTS.md)를 따른다.

## 시작점

| 찾을 내용 | 먼저 읽을 파일 | 다음 소비자 |
|---|---|---|
| 전체 빌드 타깃 | [CMakeLists.txt](../CMakeLists.txt) | engine/·game/·apps/·samples/·tests/의 CMakeLists |
| 부팅·루프·종료 | [App.cpp](../engine/core/src/App.cpp) | 앱의 모듈 등록·틱 |
| 에디터 실행 | [main.cpp](../apps/editor/src/main.cpp) | EditorApp·EditorModule·ProjectContext·PlayMode |
| 게임 실행 | [main.cpp](../apps/game/main.cpp) | 에셋·씬·렌더·오디오·게임 UI·NetClient |
| 서버 실행 | [main.cpp](../apps/server/main.cpp) | NetGameServer·GameServer·PersistenceService·liveops |
| 에셋 패키징 | [paktool](../apps/paktool) | PakFile·VFS |
| 자동 검증 | [tests/CMakeLists.txt](../tests/CMakeLists.txt) | mye_tests·샘플 CTest 시나리오 |
| MCP | [index.ts](../tools/mcp/src/index.ts) | tools/, root.ts, state.ts, scripts/smoke.mjs |

## 상태에서 최종 출력까지

```text
입력 → 고정 틱/앱 상태 → World·컴포넌트 → RenderExtract
→ RenderProxyList → HybridRenderer/SpriteBatch → DX11 → Present
```

- 월드 저장·시스템: [World.h](../engine/scene/include/mye/ecs/World.h), [SceneModule.cpp](../engine/scene/src/scene/SceneModule.cpp).
- 층·깊이: [RenderExtract.h](../engine/scene/include/mye/scene/RenderExtract.h), [DepthEncoder.cpp](../engine/render/src/DepthEncoder.cpp).
- GPU 소비: [HybridRenderer.cpp](../engine/render/src/HybridRenderer.cpp), [Dx11Device.cpp](../engine/rhi/src/dx11/Dx11Device.cpp).
- 픽셀/마우스 변환: [PixelPerfectTarget.cpp](../engine/render/src/PixelPerfectTarget.cpp).
- UI 글리프: [TextRenderer.cpp](../engine/ui/src/text/TextRenderer.cpp), [SpriteBatch.cpp](../engine/render/src/SpriteBatch.cpp).

```text
가상 파일 → 임포터 → CPU 데이터 → AssetManager finalize → 핸들 → 렌더/오디오
```

[AssetManager.cpp](../engine/asset/src/AssetManager.cpp)에서 등록·로드·실패·교체를 추적한다. 재임포트는 AssetDatabase·FileWatcher의 실제 앱 시작 호출까지 확인한다.

```text
NetClient 입력 → NetServer 검증/권위 이동 → 스냅샷 → 예측 재조정/원격 보간
GameServer 세션 → 캐릭터 상태·원장 → PersistenceService → state.json·백업
```

[Protocol.h](../engine/net/include/mye/net/Protocol.h), [NetGameServer.cpp](../engine/gameserver/src/NetGameServer.cpp), [PersistenceService.cpp](../engine/persist/src/PersistenceService.cpp)에서 버전·순서·세션·오류·저장 경계를 확인한다.

## 모듈 위치

| 경로 | 책임 |
|---|---|
| engine/core·rhi·reflect·ddc | 플랫폼·GPU·메타·동적 데이터 기반 |
| engine/asset·render·scene·audio | 콘텐츠와 월드의 로딩·시뮬레이션·출력 |
| engine/script·ui·runtime·imgui·editor·plugin | 스크립트·사용자 화면·콘텐츠·편집·확장 |
| engine/gameplay·net·persist·liveops·gameserver | 재사용 RPG 규칙·접속·저장·운영·서버 연결 |
| game/social·game/mmo | 게임 고유 소셜·경제·직업·사냥 콘텐츠 |
| apps/ | 실행 조합 |
| samples/ | 개별 렌더·에셋·콘텐츠 검증 경로 |
| tools/ | MCP·패키징·서버 검증·측정 |

## 변경 전에 확인할 경계

정의와 모든 호출자를 찾고 입력에서 최종 소비자까지 추적한다. 공통 원인은 공통 경계에서 고친다. 헤더·CMake 링크·앱 등록·테스트가 각각 존재하는지 확인하고, 라이브러리 구현만으로 앱 연결을 완료 표시하지 않는다.

현재 render→scene 소스 결합, 앱의 GUID/선로딩, 에디터 파일 열기와 MyGame의 런타임/물리 배선은 [13](13-architecture-and-features.md)에 근거가 있다. 긴 파일이라는 이유만으로 분리하지 않고 수명·변경 이유·의존성이 다른 책임을 분리한다.
