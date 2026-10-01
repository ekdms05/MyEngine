# MyEngine 문서 안내

문서는 현재 제품의 기능·실행 경로·데이터 계약을 기준으로 설명한다. 소스와 설명이 다르면 실제 호출 경로를 확인하고 차이와 남은 작업을 기록한다.

| 먼저 읽을 문서 | 내용 |
|---|---|
| [구조와 기능](13-architecture-and-features.md) | 모듈과 앱의 실제 연결·동작 상태 |
| [개선·개발 우선순위](14-development-priorities.md) | 결함·확장 항목의 근거와 완료 조건 |
| [스킬·에이전트](15-skills-and-agents.md) | 설치·설정·현재 세션 연결과 권장 역할 |
| [작업 기록](16-foundation-worklog.md) | 변경·삭제·설치 이유와 실제 검증 결과 |
| [저장소 작업 규칙](../AGENTS.md) | 구조·코드·검증 기준 |

## 제품과 시스템 설명

| 문서 | 내용 |
|---|---|
| [00. 개요](00-overview.md) | 제품 범위, 앱, 계층과 사용 경로 |
| [01. 코어·플랫폼](01-core-platform.md) | 앱 루프·모듈 수명·입력·이벤트·잡·오류 |
| [02. 렌더링](02-rendering.md) | 좌표·픽셀 타깃·2D/3D 깊이·UI 계약 |
| [03. 씬·월드](03-scene-world.md) | 자체 ECS·시스템·타일맵·물리·렌더 추출 |
| [04. 에셋 파이프라인](04-asset-pipeline.md) | GUID·임포트·VFS·수명·리플렉션·패키지 |
| [05. 스크립트·플러그인](05-scripting-plugins.md) | Lua·바인딩·네이티브 확장·수명 |
| [06. 런타임 시스템](06-runtime-systems.md) | 게임 UI·오디오·대화·컷신·NPC·세이브 |
| [07. 에디터·UI](07-editor-ui.md) | 실행 파일·패널·편집·플레이 모드와 한계 |
| [게임 제작 가이드](guide/index.html) | 이미지·GIF·영상으로 설명하는 오프라인 제작 페이지 |
| [18. 도트·모션·UI](18-editor-authoring.md) | 참조·리깅·모션·저장·중립 테마와 공식 벤치마킹 근거 |
| [17. 오브젝트·동작 제작](17-object-workflow.md) | 건물·타일·조작·충돌·포털·이벤트 연결·Lua·2D/3D 뷰 |
| [08. MCP](08-mcp.md) | 현재 8개 개발 도구·CLI·파일 경계·검증 |
| [10. 기능 상태와 개선 안내](10-status-and-roadmap.md) | 현재 제공 기능과 개선 목록의 입구 |
| [11. 확장성](11-extensibility.md) | 리플렉션·DLL·데이터 컴포넌트·Lua 확장 |
| [12. 코드베이스 맵](12-codebase-map.md) | 실행 진입점·모듈·공통 경계의 소스 탐색 |

실행 방법은 [README](../README.md), 앱별 통합 상태는 `13`, 우선순위와 수용 조건은 `14`가 기준이다. 테스트 개수나 라이브러리 존재만으로 앱 기능의 완료를 표시하지 않는다.

## MMORPG 확장 요구사항

[MMORPG 안내](mmorpg/00-overview.md)는 현재 기반과 서비스 확장의 차이를 설명한다. 아래 문서의 목표 수치·제안 타입은 실제 완료 기능이나 운영 보장이 아니다.

| 문서 | 주제 |
|---|---|
| [클라이언트 렌더링](mmorpg/01-client-rendering.md) | 대량 2.5D 표현·카메라·스트리밍 |
| [네트워크·서버](mmorpg/02-netcode-server.md) | 권위 시뮬레이션·세션·복제·존 이전 |
| [영속성·계정](mmorpg/03-persistence-accounts.md) | 저장·계정·경제·복구 |
| [게임플레이](mmorpg/04-gameplay-systems.md) | 전투·성장·경제·사회 규칙 |
| [픽셀 에셋](mmorpg/05-pixel-assets-ai.md) | 제작·임포트·검수·출처 |
| [오디오](mmorpg/06-ai-audio.md) | 음원·음악 전이·공간 재생 |
| [작업 도구 연동](mmorpg/07-ai-orchestration.md) | 외부 제작 도구와 검수 |
| [콘텐츠 도구](mmorpg/08-content-tooling.md) | 데이터 편집·참조 검증·패키징 |
| [운영·보안](mmorpg/09-liveops-security.md) | 배포·서비스·복구 요구 |

기능·계층·실행 경로를 바꾸면 해당 시스템 문서, `13`의 상태, `14`의 완료 조건과 작업 기록을 함께 갱신한다. 경로가 유지된 문서는 현재 내용으로 읽는다.
