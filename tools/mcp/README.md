# MyEngine MCP 서버 (Stage A — 개발도구)

AI 에이전트가 MyEngine 을 빌드·테스트·실행하고 **렌더링 결과를 이미지로 직접 보는** MCP(Model Context Protocol) 서버.
설계 정본: [docs/08-mcp.md](../../docs/08-mcp.md). Stage B(에디터 원격 제어)는 로드맵 M4 에서 구현된다.

- TypeScript + 공식 `@modelcontextprotocol/sdk`, stdio 트랜스포트
- 엔진 코드를 링크하지 않는다 — CLI(cmake/ctest/exe)와 파일(BMP/로그) 경계로만 상호작용
- 임의 셸 실행 툴 없음, 셸 미사용 spawn, 모든 자식 프로세스 타임아웃 + 트리 kill

## 빌드

```sh
cd tools/mcp
npm install
npm run build     # tsc → dist/
```

## 등록 (Claude Code)

리포 루트의 [`.mcp.json`](../../.mcp.json)에 등록되어 있어 프로젝트 진입 시 자동 연결된다:

```json
{
  "mcpServers": {
    "myengine": {
      "command": "node",
      "args": ["E:/MyEngine/tools/mcp/dist/index.js"],
      "env": { "MYE_ROOT": "E:/MyEngine", "MYE_BUILD_DIR": "build/dev" }
    }
  }
}
```

수동 실행: `node dist/index.js` (stdout 은 MCP 프로토콜 전용 — 서버 로그는 stderr).

### 환경변수

| 변수 | 기본값 | 의미 |
|---|---|---|
| `MYE_ROOT` | `tools/mcp/../..` (리포 루트) | 엔진 리포 루트. 모든 자식 프로세스의 cwd |
| `MYE_BUILD_DIR` | `build` | CMake 빌드 디렉터리(루트 상대). AI 전용 빌드를 사람(IDE) 빌드와 분리하려면 `build/dev` 권장 |

## 툴 목록

| 툴 | 요약 |
|---|---|
| `engine_build` | CMake 빌드(미구성 시 자동 configure — `Visual Studio 18 2026` / x64). 에러≤30·경고≤10 을 `파일(줄): 코드: 메시지`로 요약 |
| `engine_test` | `ctest --output-on-failure` 실행·요약(passed/failed/total + 실패 테스트 출력 꼬리). 자동 빌드는 하지 않음 |
| `engine_run` | 샘플 exe 를 `--frames N` 으로 자동 종료 실행. exit 코드(NTSTATUS 크래시 해석 병기)·출력 꼬리 반환 |
| `engine_capture_frame` | 샘플을 `--frames N --dump <bmp>` 로 실행 → BMP 디코드 → PNG → **MCP 이미지 콘텐츠** 반환 (AI의 눈). 최대 변 960px 다운스케일 |
| `engine_logs` | 최근 빌드/테스트/실행/캡처 원본 로그 tail + 레벨(warn/error)·정규식 필터 |
| `project_status` | configure 상태·마지막 작업 요약·샘플 목록·최근 캡처·서버 버전. 항상 성공 |

공통 규약: 반환 텍스트 ≤8KB(원문은 `.state/logs/`에 저장 후 경로 안내), 빌드·테스트·실행·캡처는
전역 뮤텍스로 직렬화(사용 중이면 즉시 "다른 작업 진행 중" 에러), 실패는 `isError` + 원인 + 다음 행동 제안.

## 샘플 CLI 계약 (엔진 쪽 요구)

모든 샘플 exe 는 두 플래그를 지원해야 한다 (docs/08-mcp.md 참조, M0 `hello_triangle`부터):

| 플래그 | 의미 |
|---|---|
| `--frames <N>` | N 프레임 렌더 후 exit 0 자동 종료 |
| `--dump <path.bmp>` | 마지막 프레임 백버퍼를 BMP(24/32bpp)로 저장 후 종료 |

## 상태 디렉터리 (`.state/`, gitignore)

```
.state/
  logs/       # <build|test|run|capture>-<timestamp>.log — 세션 원문
  captures/   # <sample>-<timestamp>.png — 프레임 캡처 결과
  status.json # 마지막 작업 요약 (project_status 의 데이터원)
```

## 스모크 테스트

```sh
npm run smoke   # dist 서버를 띄워 initialize → tools/list → 3개 툴 호출 검증 (엔진 빌드 없이)
```

## 확장

`src/tools/` 파일 1개 = 툴 1개 컨벤션. 새 개발도구는 파일 추가 + `src/index.ts` 등록 한 줄이다
(docs/08-mcp.md 확장 포인트 §3).
