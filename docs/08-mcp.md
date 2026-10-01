# 08. MyEngine MCP 개발 도구

현재 MCP 서버는 [tools/mcp](../tools/mcp)의 TypeScript·공식 MCP SDK·stdio 구현이다. 엔진을 직접 링크하지 않고 CMake·CTest·실행 파일·캡처/로그 파일로 상호작용한다. 등록된 도구는 [index.ts](../tools/mcp/src/index.ts)를 기준으로 설명한다.

## 도구 목록

| 도구 | 현재 기능 |
|---|---|
| engine_build | 미구성 시 CMake configure, 지정 타깃/구성 빌드, 오류·경고 요약 |
| engine_test | 구성된 빌드의 CTest 실행·결과 요약 |
| engine_run | 지정 실행 타깃을 프레임 제한으로 실행하고 종료/출력 확인 |
| engine_capture_frame | 실행 타깃 BMP 캡처를 PNG 이미지 콘텐츠로 반환 |
| engine_logs | 빌드·테스트·실행·캡처 로그 조회 |
| project_status | configure 상태·마지막 작업·앱 소스·캡처·버전 조회 |
| project_inspect | 프로젝트·씬 계층/컴포넌트·에셋 GUID 읽기, 검색·페이지 조회 |
| engine_reference | 현재 Lua API·컴포넌트·에디터·에셋·씬·렌더 문서의 검색·줄 범위 조회 |
| asset_import | 에디터 CLI 임포트 경계로 원본을 assets/에 복사·등록, 기존 파일 덮어쓰기 거부 |

현재 버전은 **0.2.2**, 등록 도구는 **9개**다. 픽셀 제작은 외부 도구에서 진행하고 PNG/.anim을 엔진에 가져온다. 에디터 메모리의 씬 조작·Undo 원격 명령·Lua REPL은 제공하지 않는다.

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

`--frames N`은 자동 종료, `--dump <path.bmp>`는 프레임 기록에 사용한다. 실행 타깃은 설정된 빌드 트리에서 찾는다. `MyEditor`에는 `args=["--project", "프로젝트 경로"]`를 전달해야 한다. 프로젝트 없이 실행하면 프로젝트 선택 창이 열린다. 모든 게임/서버 CLI를 임의로 호출하는 일반 셸 도구가 아니다.

```text
MCP 요청 → 입력/경로 검증 → 작업 직렬화 → CLI 실행·로그 저장
→ 결과 요약 또는 BMP 디코드/PNG 반환 → MCP 응답
```

실패는 `isError`·원인·다음 조치로 반환한다. 자식 프로세스는 timeout과 종료 경로를 갖고 빌드·테스트·실행·캡처·임포트는 작업 gate로 직렬화한다. 제작 파일 경로는 루트 상대 계약과 실제 symlink/junction 경계를 확인한다. 원문 로그/캡처는 ignored `tools/mcp/.state/`에 보관한다.

## 프로젝트 제작 조회·임포트

`project_inspect`의 `project`는 MYE_PROJECT_ROOT 상대 `.myeproj`, `scene`은 프로젝트 상대 `.scene` 경로다. 제작 루트가 없으면 기존 MYE_ROOT를 사용한다. `section="scene"`은 버전·ID·부모의 존재·순환을 확인하고 오브젝트 속성을 출력한다. 타입 등록·필드 범위·Lua 실행의 최종 검증은 엔진 로더와 Play에서 수행한다. `section="assets"`는 파일 경로와 기존 `.meta` GUID를 조회한다. `filter`, `offset`, `limit`로 필요한 부분만 읽는다. JSON은 32 MiB, 에셋 탐색은 깊이 32/파일 25,000 제한이다.

`engine_reference(topic="lua")`는 [Lua API](19-lua-api.md)를 읽는다. `search`는 일치 줄, `startLine`·`lines`는 범위 조회에 사용한다. 현재 저장소 문서가 근거이며 웹 문서의 다른 엔진 API를 반환하지 않는다.

`engine_reference(topic="components")`는 [컴포넌트 사용법](20-components.md)의 단위·기본값·조작·실행 제약을 읽는다.

`asset_import`는 `project`, 저장소 상대 `source`, **assets/ 기준** `destination`을 받는다. 예: `destination="characters/player.png"`. 대상 폴더는 에셋 브라우저에서 먼저 생성한다. 내부 호출은 `MyEditor --project <manifest> --import-asset <source> --asset-destination <destination> --headless --frames 1`이다. 파일 복사·지원 형식·GUID/.meta·실패 처리는 에디터의 공통 임포트 경계가 담당한다. 열린 GUI 상태 변경이나 에셋 삭제는 지원하지 않는다. 현재 프로젝트를 GUI에서 다시 스캔하거나 다시 열어 반영한다.

## 검증과 확장 조건

`asset_import.sourceScope`는 `engine`(기본: 기존 원본 경로 유지) 또는 `project`다. 후자는 source를 MYE_PROJECT_ROOT 상대 경로로 해석한다. 예: 제작 루트를 `C:/games/MyProject`로 설정하면 `project="project.myeproj", sourceScope="project", source="art/hero.png", destination="characters/hero.png"`다. 빌드·문서·실행 타깃은 계속 MYE_ROOT를 사용한다. 루트 밖 경로·절대 경로 입력·symlink/junction 탈출·덮어쓰기는 허용하지 않는다.

이는 소스 저장소에서 실행하는 **엔진 개발용 MCP**의 제작 파일 경계다. 릴리즈 사용자를 위한 독립 MCP 패키지는 아직 없으며, 소스 조회/빌드가 금지된 게임 제작 세션에 이 서버를 연결하지 않는다. 설정 파일 존재와 현재 세션 연결, 실제 CLI 임포트 성공은 별도 상태다.

`npm.cmd run build`는 TypeScript 컴파일, `npm.cmd run smoke`는 프로토콜 초기화·9개 도구 노출·격리 fixture의 조회/계층/경로 검사·구성되지 않은 빌드의 실패 안내를 확인한다. 실제 임포트 성공은 빌드된 MyEditor와 새 테스트 프로젝트로 별도 확인한다. 이 검증은 엔진 전체 빌드·실제 에디터 원격 조작 완료를 뜻하지 않는다.

API·CLI·파일 계약이 바뀌면 도구 스키마, 이 문서와 smoke 재현을 함께 갱신한다. 새 도구는 기존 등록·오류·경로·작업 gate를 재사용한다. 에디터 원격 제어가 필요해지면 인증·메인 스레드·CommandStack·Undo·수명 계약을 갖춰 구현한다.
