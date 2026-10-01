# 08. MyEngine MCP 개발 도구

현재 MCP 서버는 [tools/mcp](../tools/mcp)의 TypeScript·공식 MCP SDK·stdio 구현이다. 엔진을 직접 링크하지 않고 CMake·CTest·실행 파일·캡처/로그 파일로 상호작용한다. 등록된 도구는 [index.ts](../tools/mcp/src/index.ts)를 기준으로 설명한다.

## 도구 목록

| 도구 | 현재 기능 |
|---|---|
| engine_build | 미구성 시 CMake configure, 지정 타깃/구성 빌드, 오류·경고 요약 |
| engine_test | 구성된 빌드의 CTest 실행·결과 요약 |
| engine_run | 지정 샘플을 프레임 제한으로 실행하고 종료/출력 확인 |
| engine_capture_frame | 샘플 BMP 캡처를 PNG 이미지 콘텐츠로 반환 |
| engine_logs | 빌드·테스트·실행·캡처 로그 조회 |
| project_status | configure 상태·마지막 작업·샘플·캡처·버전 조회 |
| dot_write_sprite | 픽셀 명세를 이미지로 기록 |
| dot_from_photo | 입력 이미지의 픽셀 변환 |

에디터 씬 조작·Undo 원격 명령·Lua REPL·동적 editor_* 툴은 현재 이 8개 도구에 포함되지 않는다. 외부 픽셀 제작 스킬 설치도 MCP 도구 연결을 뜻하지 않는다.

## 설치·실행·프로젝트 설정

```powershell
cd tools/mcp
npm.cmd ci
npm.cmd run build
npm.cmd run smoke
```

직접 서버 실행은 `node dist/index.js`다. stdout은 프로토콜 전용, 서버 로그는 stderr를 사용한다. 프로젝트 [`.mcp.json`](../.mcp.json)은 MyEngine MCP와 `MYE_BUILD_DIR=build/dev`를 설정한다. 클라이언트가 실제로 설정을 읽고 연결했는지는 해당 세션의 도구 노출로 확인한다.

| 환경변수 | 구현 기본값 | 현재 프로젝트 설정 |
|---|---|---|
| MYE_ROOT | tools/mcp/../.. | E:/MyEngine |
| MYE_BUILD_DIR | build | build/dev |

설정 없는 MCP의 기본 build 경로와 저장소의 권장 build/dev를 혼동하지 않는다. 실행 경로 해석은 [root.ts](../tools/mcp/src/root.ts)를 따른다.

## 실행·캡처 경계

샘플의 `--frames N`은 자동 종료, `--dump <path.bmp>`는 프레임 기록에 사용한다. `engine_run`·capture의 실행 타깃 탐색은 설정된 빌드 하위의 샘플을 기준으로 한다. 모든 게임/서버 CLI를 임의로 호출하는 일반 셸 도구가 아니다.

```text
MCP 요청 → 입력/경로 검증 → 작업 직렬화 → CLI 실행·로그 저장
→ 결과 요약 또는 BMP 디코드/PNG 반환 → MCP 응답
```

실패는 `isError`·원인·다음 조치로 반환한다. 자식 프로세스는 timeout과 종료 경로를 갖고 빌드·테스트·실행·캡처는 작업 gate로 직렬화한다. 경로는 루트 상대 계약·탈출 검사를 따르며 원문 로그/캡처는 `tools/mcp/.state/`에 보관한다.

## 검증과 확장 조건

`npm.cmd run build`는 TypeScript 컴파일, `npm.cmd run smoke`는 프로토콜 초기화·8개 도구 노출·구성되지 않은 빌드의 실패 안내를 확인한다. 이 검증은 엔진 전체 빌드·실제 에디터 원격 조작 완료를 뜻하지 않는다.

새 도구는 기존 `src/tools`·등록·오류·경로·작업 gate를 재사용한다. 에디터 원격 제어는 실제 편집 워크플로가 안정된 뒤 인증·메인 스레드·CommandStack·Undo·수명 계약을 갖춰 구현한다. 사용처 없는 별도 원격 서버나 추측성 제공자 계층을 추가하지 않는다.
