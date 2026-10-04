# 08. MyEngine MCP 개발 도구

0.5.0: `engine_reference(topic="world2d")`는 온라인 공격·포털/epoch·저장 맵 계약, `topic="export"`는 실제 MyEditor export CLI를 조회한다. 기존 `engine_run(sample="MyEditor", args=["--project", FILE, "--export-game", NEW_DIR, "--runtime", RELEASE_DIR])`로 내보내기를 실행한다. 새 원격 에디터 변경 도구는 추가하지 않았다. 이 개발 MCP의 설치/빌드와 현재 세션 연결, 소스 없는 소비자용 배포는 별개다.

현재 MCP 서버는 [tools/mcp](../tools/mcp)의 TypeScript·공식 MCP SDK·stdio 구현이다. 엔진을 직접 링크하지 않고 CMake·CTest·실행 파일·캡처/로그 파일로 상호작용한다. 등록된 도구는 [index.ts](../tools/mcp/src/index.ts)를 기준으로 설명한다.

## 도구 목록

| 도구 | 현재 기능 |
|---|---|
| engine_build | 미구성 시 CMake configure, 지정 타깃/구성 빌드, 오류·경고 요약 |
| engine_test | 구성된 빌드의 CTest 실행·결과 요약 |
| engine_run | 지정 앱을 프레임 제한, MyServer를 틱 제한으로 실행. ticks 입력으로 MyGame/MyServer 고정 틱 한도 선택 |
| engine_capture_frame | 실행 타깃 BMP 캡처를 PNG 이미지 콘텐츠로 반환 |
| engine_logs | 빌드·테스트·실행·캡처 로그 조회 |
| project_status | configure 상태·마지막 작업·앱 소스·캡처·버전 조회 |
| project_inspect | 프로젝트·씬 계층/컴포넌트·에셋 GUID 읽기, 검색·페이지 조회 |
| engine_reference | 현재 Lua·컴포넌트·에디터·에셋·씬·렌더·2D/XYZ 온라인 문서의 검색·줄 범위 조회 |
| asset_import | 에디터 CLI 임포트 경계로 원본을 assets/에 복사·등록, 기존 파일 덮어쓰기 거부 |

현재 버전은 **0.4.0**, 등록 도구는 **9개**다. 픽셀 제작은 외부 도구에서 진행하고 PNG/.anim을 엔진에 가져온다. 에디터 메모리의 씬 조작·Undo 원격 명령·Lua REPL은 제공하지 않는다.

## 설치·실행·프로젝트 설정

```powershell
cd tools/mcp
npm.cmd ci
npm.cmd run build
npm.cmd run smoke
```

직접 서버 실행은 `node dist/index.js`다. stdout은 프로토콜 전용, 서버 로그는 stderr를 사용한다. 사용 중인 MCP 클라이언트에 Node 실행 파일, 저장소의 `tools/mcp/dist/index.js`, `MYE_ROOT`, `MYE_BUILD_DIR=build/dev`를 설정한다. 개인 설치 경로·연결 설정은 로컬로 관리한다. 설정과 실제 연결은 해당 세션의 도구 노출로 구분한다.

| 환경변수 | 구현 기본값 | 현재 프로젝트 설정 |
|---|---|---|
| MYE_ROOT | tools/mcp/../.. | 사용자의 저장소 절대 경로 |
| MYE_PROJECT_ROOT | MYE_ROOT | 명시적으로 허용한 기존 제작 프로젝트 폴더의 절대 경로 |
| MYE_BUILD_DIR | build/dev | build/dev |

설정 없는 MCP도 프로젝트 preset과 동일한 build/dev를 사용한다. MYE_BUILD_DIR로 다른 루트 상대 경로를 지정할 수 있다. 실행 경로 해석은 [root.ts](../tools/mcp/src/root.ts)를 따른다.

미구성 트리는 `cmake -S . -B <buildDir> -A x64`로 CMake의 기본 Visual Studio 선택을 사용한다. 특정 버전이 필요하면 README의 `-G` 명령으로 먼저 구성한다. 기존 `CMakeCache.txt`는 다시 configure하지 않고 그 제너레이터로 `cmake --build ... --parallel`을 실행한다. 별도의 Visual Studio 탐지·설정 인터페이스는 두지 않는다.

## 실행·캡처 경계

`engine_reference(topic="ui")`는 `docs/06-runtime-systems.md`에서 실제 Play/MyGame 안내 표시와 위젯 라이브러리/작성 HUD의 연결 상태를 조회한다. 참조 조회는 게임 UI 편집 API나 현재 세션 MCP 연결의 증거가 아니다.

`--frames N`은 자동 종료, `--dump <path.bmp>`는 프레임 기록에 사용한다. 실행 타깃은 설정된 빌드 트리에서 찾는다. `MyEditor`에는 `args=["--project", "프로젝트 경로"]`를 전달해야 한다. 프로젝트 없이 실행하면 프로젝트 선택 창이 열린다. 모든 게임/서버 CLI를 임의로 호출하는 일반 셸 도구가 아니다.

main의 MyGame 최종 `--dump`는 네이티브 창의 실제 backbuffer(UI 포함), headless는 내부 타깃을 저장한다. 스텝 `--capture-at`은 내부 2D 월드 진단 좌표를 유지한다. 설치 0.3.0의 캡처/표시 동작과 구분한다.

개발 소스의 `MyGame --ticks N`은 실제 고정 시뮬레이션 N회 후 최종 프레임을 렌더/캡처하고 종료한다. MCP에서 `engine_run(sample="MyGame", ticks=4, args=["--project", "프로젝트.myeproj", "--headless"])`를 호출하면 기본 frames 대신 `--ticks 4`를 전달한다. MyEditor 등 다른 앱의 ticks는 거부한다. 한도는 양수이며 CLI에 frames/ticks를 모두 지정하면 먼저 도달한 한도로 종료한다. 프레임 수는 물리 틱 수나 서버 ack 수가 아니다. `engine_capture_frame`은 여전히 프레임 기준 도구다. 새 MyGame 틱 옵션은 기존 0.3.0 바이너리에 없다.

```text
MCP 요청 → 입력/경로 검증 → 작업 직렬화 → CLI 실행·로그 저장
→ 결과 요약 또는 BMP 디코드/PNG 반환 → MCP 응답
```

실패는 `isError`·원인·다음 조치로 반환한다. 자식 프로세스는 timeout과 종료 경로를 갖고 빌드·테스트·실행·캡처·임포트는 작업 gate로 직렬화한다. 제작 파일 경로는 루트 상대 계약과 실제 symlink/junction 경계를 확인한다. 원문 로그/캡처는 ignored `tools/mcp/.state/`에 보관한다.

## 프로젝트 제작 조회·임포트

`project_inspect`의 `project`는 MYE_PROJECT_ROOT 상대 `.myeproj`, `scene`은 프로젝트 상대 `.scene` 경로다. 제작 루트가 없으면 기존 MYE_ROOT를 사용한다. `section="scene"`은 버전·ID·부모의 존재·순환을 확인하고 오브젝트 속성을 출력한다. 타입 등록·필드 범위·Lua 실행의 최종 검증은 엔진 로더와 Play에서 수행한다. `section="assets"`는 파일 경로와 기존 `.meta` GUID를 조회한다. `filter`, `offset`, `limit`로 필요한 부분만 읽는다. JSON은 32 MiB, 에셋 탐색은 깊이 32/파일 25,000 제한이다.

`engine_reference(topic="lua")`는 [Lua API](19-lua-api.md)를 읽는다. `search`는 일치 줄, `startLine`·`lines`는 범위 조회에 사용한다. 현재 저장소 문서가 근거이며 웹 문서의 다른 엔진 API를 반환하지 않는다.

`engine_reference(topic="components")`는 [컴포넌트 사용법](20-components.md)의 단위·기본값·조작·실행 제약을 읽는다.

`engine_reference(topic="roadmap")`은 [온라인 2D MMORPG 작업 목록](22-2d-mmorpg-roadmap.md)의 목표·현재 경로·작은 작업/완료 조건을 읽는다. 미완료 작업을 현재 제공 API로 설명하지 않는다. 기존 `online` 주제의 XYZ 계약은 유지한다.

`engine_reference(topic="input")`은 [프로젝트 입력 설정](25-input-actions.md)의 기본 조작·저장 `inputMap`·공통 고정 틱 소비·캡처 취소와 미완료 Lua/직접 장치 검수 범위를 읽는다. MyEditor의 `--input-settings-dialog`는 실제 설정 창을 열어 캡처하는 CLI이며 원격 편집 API가 아니다. MyGame의 기존 `--input`은 의미 입력 재생으로 물리 키 재매핑/직접 장치 검증을 대신하지 않는다.

`engine_reference(topic="online")`은 [XYZ/온라인](21-3d-play-and-online.md)의 장면 구성·카메라 입력·인증 CLI·예측/영속·지원 한계를 읽는다. `engine_run(sample="MyServer", frames=N)`은 서버의 `--ticks N`으로 변환된다. 개발 MCP와 릴리즈 소비자용 도구의 연결은 구분한다.

`engine_reference(topic="camera2d")`는 main 소스의 [저장 2D 게임 카메라](24-2d-camera.md)를 읽는다. 이름/부모 추종·경계·데드존·줌/흔들림·실제 프레임 근거와 온라인 Lua 격리를 설명한다. Entity의 `set_camera_zoom`·`shake_camera`·좌표 변환은 `topic="lua"`, 저장 필드는 `topic="components"`로 조회한다. `MyGame --input`의 선택 cameraZoomSteps는 각 고정 틱의 -16~16 휠 단계이며 신규 프로토콜이나 서버 권위 입력이 아니다.

개발 소스의 2D 로더·인증/권위·예측·저장 계약은 `engine_reference(topic="scene", search="StepMotion2D")`와 [씬 문서](03-scene-world.md#인증된-2d-이동의-라이브러리-연결)에서 조회한다. `engine_reference(topic="online2d")`는 [공식 2D 플레이](23-2d-online-play.md)의 활성 캐릭터 판별·인증 CLI·--input 고정 틱 재생/ack·원격 비주얼·저장 재접속과 남은 범위를 읽는다. 기존 9개 도구와 XYZ online 주제는 유지한다. main 소스 전용이며 0.3.0 바이너리에는 이 기능이 없다.

`asset_import`는 `project`, 저장소 상대 `source`, **assets/ 기준** `destination`을 받는다. 예: `destination="characters/player.png"`. 대상 폴더는 에셋 브라우저에서 먼저 생성한다. 내부 호출은 `MyEditor --project <manifest> --import-asset <source> --asset-destination <destination> --headless --frames 1`이다. 파일 복사·지원 형식·GUID/.meta·실패 처리는 에디터의 공통 임포트 경계가 담당한다. 열린 GUI 상태 변경이나 에셋 삭제는 지원하지 않는다. 현재 프로젝트를 GUI에서 다시 스캔하거나 다시 열어 반영한다.

## 검증과 확장 조건

`asset_import.sourceScope`는 `engine`(기본: 기존 원본 경로 유지) 또는 `project`다. 후자는 source를 MYE_PROJECT_ROOT 상대 경로로 해석한다. 예: 제작 루트를 `C:/games/MyProject`로 설정하면 `project="project.myeproj", sourceScope="project", source="art/hero.png", destination="characters/hero.png"`다. 빌드·문서·실행 타깃은 계속 MYE_ROOT를 사용한다. 루트 밖 경로·절대 경로 입력·symlink/junction 탈출·덮어쓰기는 허용하지 않는다.

이는 소스 저장소에서 실행하는 **엔진 개발용 MCP**의 제작 파일 경계다. 릴리즈 사용자를 위한 독립 MCP 패키지는 아직 없으며, 소스 조회/빌드가 금지된 게임 제작 세션에 이 서버를 연결하지 않는다. 설정 파일 존재와 현재 세션 연결, 실제 CLI 임포트 성공은 별도 상태다.

`npm.cmd run build`는 TypeScript 컴파일, `npm.cmd run smoke`는 프로토콜 초기화·9개 도구 노출·격리 fixture의 조회/계층/경로 검사·구성되지 않은 빌드의 실패 안내를 확인한다. 실제 임포트 성공은 빌드된 MyEditor와 새 테스트 프로젝트로 별도 확인한다. 이 검증은 엔진 전체 빌드·실제 에디터 원격 조작 완료를 뜻하지 않는다.

API·CLI·파일 계약이 바뀌면 도구 스키마, 이 문서와 smoke 재현을 함께 갱신한다. 새 도구는 기존 등록·오류·경로·작업 gate를 재사용한다. 에디터 원격 제어가 필요해지면 인증·메인 스레드·CommandStack·Undo·수명 계약을 갖춰 구현한다.
