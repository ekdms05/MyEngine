# MyEngine MCP 서버

AI 에이전트가 MyEngine을 빌드·테스트·실행·캡처하고 프로젝트·Lua API·에셋을 조회하는 MCP(Model Context Protocol) 서버.
기능과 실행 계약: [docs/08-mcp.md](../../docs/08-mcp.md). 현재는 CLI와 파일을 통한 개발 도구이며, 에디터 원격 제어는 미구현이다.

- TypeScript + 공식 `@modelcontextprotocol/sdk`, stdio 트랜스포트
- 엔진 코드를 링크하지 않는다 — CLI(cmake/ctest/exe)와 파일(BMP/로그) 경계로만 상호작용
- 임의 셸 실행 툴 없음, 셸 미사용 spawn, 모든 자식 프로세스 타임아웃 + 트리 kill

## 빌드

```sh
cd tools/mcp
npm install
npm run build     # tsc → dist/
```

## 클라이언트 등록

사용하는 MCP 클라이언트에 아래 설정을 저장한다. 경로를 자신의 설치 위치로 바꾸고 연결 여부는 클라이언트의 도구 목록에서 확인한다. 개인 경로가 들어간 설정은 공개 저장소에 올리지 않는다.

```json
{
  "mcpServers": {
    "myengine": {
      "command": "node",
      "args": ["C:/workspace/MyEngine/tools/mcp/dist/index.js"],
      "env": { "MYE_ROOT": "C:/workspace/MyEngine", "MYE_BUILD_DIR": "build/dev" }
    }
  }
}
```

수동 실행: `node dist/index.js` (stdout 은 MCP 프로토콜 전용 — 서버 로그는 stderr).

### 환경변수

| 변수 | 기본값 | 의미 |
|---|---|---|
| `MYE_ROOT` | `tools/mcp/../..` (리포 루트) | 엔진 리포 루트. 모든 자식 프로세스의 cwd |
| `MYE_PROJECT_ROOT` | `MYE_ROOT` | 기존 제작 폴더의 명시적 허용 루트. project_inspect/asset_import의 project 경로 기준 |
| `MYE_BUILD_DIR` | `build/dev` | CMake 빌드 디렉터리(루트 상대). 프로젝트 preset·README 실행 경로와 동일하며 필요하면 다른 경로로 지정 |

## 툴 목록

| 툴 | 요약 |
|---|---|
| `engine_build` | CMake 빌드(미구성 시 CMake 기본 제너레이터 / x64로 configure). 기존 cache의 제너레이터 유지. 에러≤30·경고≤10 을 `파일(줄): 코드: 메시지`로 요약 |
| `engine_test` | `ctest --output-on-failure` 실행·요약(passed/failed/total + 실패 테스트 출력 꼬리). 자동 빌드는 하지 않음 |
| `engine_run` | `--frames N` 실행(MyServer는 --ticks N). ticks 지정 시 MyGame/MyServer 고정 틱 한도. 프로젝트는 args에 --project 전달 |
| `engine_capture_frame` | 프레임 BMP를 PNG MCP 이미지 콘텐츠로 반환. 최대 변 960px |
| `engine_logs` | 최근 빌드/테스트/실행/캡처 원본 로그 tail + 레벨(warn/error)·정규식 필터 |
| `project_status` | configure 상태·마지막 작업 요약·앱 소스 목록·최근 캡처·서버 버전. 항상 성공 |
| `project_inspect` | 프로젝트/씬의 계층·컴포넌트·에셋 GUID 조회, 검색·페이지 지원 |
| `engine_reference` | 현재 Lua·컴포넌트·에디터·에셋·씬·렌더·camera2d·input·animation2d·online2d·roadmap 문서 검색·줄 범위 조회 |
| `asset_import` | 에디터 CLI를 통한 에셋 복사·GUID/.meta 등록. assets 기준 대상 경로, 덮어쓰기 거부 |

공통 규약: 반환 텍스트 ≤8KB(원문은 `.state/logs/`에 저장 후 경로 안내), 빌드·테스트·실행·캡처는
전역 뮤텍스로 직렬화(사용 중이면 즉시 "다른 작업 진행 중" 에러). 임포트도 같은 gate를 사용한다. 실패는 `isError` + 원인 + 다음 행동 제안.

## 실행 CLI 계약

실행·캡처 도구는 다음 플래그를 사용한다 ([CLI 계약](../../docs/08-mcp.md) 참조):

| 플래그 | 의미 |
|---|---|
| `--frames <N>` | N 프레임 렌더 후 exit 0 자동 종료 |
| `--ticks <N>` | MyGame/MyServer 고정 틱 한도. engine_run의 ticks 입력이 frames 대신 전달됨 |
| `--dump <path.bmp>` | MyGame의 지정 한도 최종 프레임을 BMP로 저장. 한도 없으면 3번째 프레임 기록 |

MyGame의 ticks는 현재 개발 소스 옵션으로 기존 0.3.0 바이너리에는 없다. MyEditor 등의 ticks 입력은 거부하며 engine_capture_frame은 프레임 기준을 유지한다. CLI에서 frames/ticks를 모두 지정하면 먼저 도달한 한도로 종료한다. 물리 확인에는 렌더 프레임 수를 고정 틱 수로 간주하지 않는다.

## 상태 디렉터리 (`.state/`, gitignore)

외부 제작 파일은 `asset_import.sourceScope="project"`로 MYE_PROJECT_ROOT 상대 경로를 사용한다. 기본 `engine`은 기존 MYE_ROOT 기준이다. 절대 입력·상위 탈출·연결 파일 탈출·기존 대상/.meta 덮어쓰기를 거부한다. 이 서버는 엔진 소스 개발용이며 독립 릴리즈 MCP 패키지는 아직 제공하지 않는다.

```
.state/
  logs/       # <build|test|run|capture>-<timestamp>.log — 세션 원문
  captures/   # <sample>-<timestamp>.png — 프레임 캡처 결과
  status.json # 마지막 작업 요약 (project_status 의 데이터원)
```

## 스모크 테스트

```sh
npm run smoke   # 초기화·9개 도구·격리 프로젝트 조회/오류 경계 확인 (엔진 빌드 없이)
```

## 확장

`src/tools/` 파일 1개 = 툴 1개 컨벤션. 새 개발도구는 파일 추가 + `src/index.ts` 등록 한 줄이다
([현재 도구 경계](../../docs/08-mcp.md)를 유지한다).

animation2d는 main 소스의 기본/8방향 `.anim` 버전 1/2, 편집·저장/Undo·반전/대체, 실제 앱 검사와 남은 진행률/행동 전환을 설명한다. 기존 reference 도구를 사용하며 새 원격 편집 API나 0.3.0 지원 약속을 추가하지 않는다.
