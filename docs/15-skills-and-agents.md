# 15. 스킬·에이전트 구성

조사일: **2026-10-01**. MyEngine은 C++20·DX11 자체 엔진이므로 도구의 이름·인기보다 실제 언어·API·호출 경로와 맞는지를 기준으로 선택했다. [skills.sh](https://www.skills.sh/)와 [MCP Market 게임 개발 목록](https://mcpmarket.com/ko/tools/skills/categories/game-development), 원본 지침을 확인했다.

## 1. 설치·설정·연결·적용 구분

| 상태 | 확인 근거 |
|---|---|
| 설치 | 로컬 SKILL.md/참조 파일·설치 명령·skills-lock.json |
| 설정 | 프로젝트 MCP·플러그인 활성화 파일 |
| 현재 세션 연결 | 실제 노출된 도구 목록. 파일 존재만으로 연결 성공이라 하지 않음 |
| 실제 적용 | 지침 읽기·프로젝트 계약으로 조정·빌드/실행 결과 |

프로젝트 MyEngine MCP 설정은 있고 서버 build/smoke도 통과했다. 현재 대화에 해당 MCP 도구는 노출되지 않았다. Aseprite는 PATH·기본 설치 위치에서 발견되지 않았고 pixel-mcp도 현재 세션에 없다. 픽셀 스킬 설치가 곧 에디터 제어 연결을 뜻하지 않는다.

다운로드한 외부 스킬은 `.agents/skills/`(Codex)·`.claude/skills/`(요청한 pixel 4종)에 로컬로 보관하고 Git에서는 제외했다. 프로젝트 코드에 외부 지침·샘플·실행 파일을 대량 편입하지 않는다. 출처·설치법은 이 문서, CLI가 생성한 기록은 [skills-lock.json](../skills-lock.json)에 보관한다. 수동 등록 creator는 lock 항목을 임의로 만들어 넣지 않았다.

## 2. 이번에 설치한 프로젝트 스킬 11종

| 스킬 | 원본 | 적용·제약 |
|---|---|---|
| find-skills | [vercel-labs/skills](https://github.com/vercel-labs/skills) | 검색·후보 발견에 사용. 자동 설치 권한으로 해석하지 않음 |
| cpp-coding-standards | [affaan-m/ecc](https://github.com/affaan-m/ecc/tree/main/skills/cpp-coding-standards) | C++20·RAII·const·소유권. MyEngine Expected·이름·포맷 우선 |
| cmake | [low-level-dev-skills](https://github.com/mohitmishra786/low-level-dev-skills/tree/main/skills/build-systems/cmake) | 타깃 의존성. bcrypt PRIVATE 링크, 기존 MSVC·CTest 유지 |
| performance-optimization | [gamedev-skills](https://github.com/gamedev-skills/awesome-gamedev-agent-skills) | Release·동일 조건 전후 측정. Unity/Godot profiler 예제는 그대로 적용하지 않음 |
| shader-programming | [gamedev-skills](https://github.com/gamedev-skills/awesome-gamedev-agent-skills) | HLSL·좌표/UV/마스크 계약 검토. 월드 cutout/depth write 유지 |
| game-ui-ux | [gamedev-skills](https://github.com/gamedev-skills/awesome-gamedev-agent-skills) | 메뉴 라벨·입력·화면 배율·설정. 새 웹 UI 프레임워크 도입 없음 |
| imgui-ui-ux-engineering | [muhosekerci](https://github.com/muhosekerci/imgui-ui-ux-engineering) | 기존 ImGui 1.92.9 WIP 기준. 상태·ID·스코프·색/간격 검토 |
| pixel-art-creator | [pixel-plugin](https://github.com/willibrandon/pixel-plugin) | skillfish 설치 후 Codex 로컬 메타데이터 보정. 실제 제작 도구는 Aseprite/pixel-mcp 필요 |
| pixel-art-professional | [pixel-plugin](https://github.com/willibrandon/pixel-plugin) | 팔레트·실루엣·픽셀 규칙·검수 |
| pixel-art-exporter | [pixel-plugin](https://github.com/willibrandon/pixel-plugin) | 프레임/시트·투명도·메타 내보내기 |
| pixel-art-animator | [pixel-plugin](https://github.com/willibrandon/pixel-plugin) | 방향·프레임·타이밍·루프 지침 |

이 스킬의 핵심 지침을 읽고 필요한 범위만 적용했다. 픽셀 스킬로 에셋을 생성하거나 외부 제작 서버를 실행한 것은 아니다. 기준 PPU 48·타깃 960×540·발밑 피벗·alpha cutout은 저장소 [렌더 계약](02-rendering.md)이 우선한다.

### 설치 명령·메타데이터 보정

PowerShell 실행 정책을 피하기 위해 동일 CLI의 `npx.cmd`를 사용했다. 프로젝트 설치이며 전역 설치가 아니다.

```powershell
npx.cmd skills add https://github.com/vercel-labs/skills --skill find-skills --agent codex --yes
npx.cmd skills add https://github.com/affaan-m/ecc --skill cpp-coding-standards --agent codex --yes
npx.cmd skills add https://github.com/mohitmishra786/low-level-dev-skills --skill cmake --agent codex --yes
npx.cmd skills add https://github.com/gamedev-skills/awesome-gamedev-agent-skills --skill performance-optimization game-ui-ux shader-programming --agent codex --yes
npx.cmd skills add https://github.com/muhosekerci/imgui-ui-ux-engineering --skill imgui-ui-ux-engineering --agent codex --yes

npx.cmd skillfish add willibrandon/pixel-plugin pixel-art-creator --project --agent "Claude Code" --yes
npx.cmd skillfish add willibrandon/pixel-plugin pixel-art-professional --project --agent "Claude Code" --yes
npx.cmd skillfish add willibrandon/pixel-plugin pixel-art-exporter --project --agent "Claude Code" --yes
npx.cmd skillfish add willibrandon/pixel-plugin pixel-art-animator --project --agent "Claude Code" --yes
npx.cmd skills add https://github.com/willibrandon/pixel-plugin --skill "Pixel Art Professional" "Pixel Art Exporter" "Pixel Art Animator" --agent codex --yes
```

skillfish 1.0.39는 이 환경에서 Claude Code/Amp를 지원하고 Codex 인자를 거부했다. 따라서 요청 명령은 Claude 프로젝트 경로로 설치하고 Codex에는 skills CLI로 등록했다. creator의 원본 description에 인용되지 않은 콜론이 있어 skills CLI가 해당 항목을 건너뛰었다. `.claude/skills/pixel-art-creator`를 `.agents/skills/pixel-art-creator`로 복사하고 SKILL.md의 name을 `pixel-art-creator`, description을 인용된 YAML 문자열로 보정했다. 지침 본문은 유지했다. 새 세션에서 자동 카탈로그 노출을 확인한다.

검토한 원본 checkout: pixel-plugin `dee350645b705916655c013f208bf5580ecb9317`, gamedev `d4b0e35550c55ae70bdfcab4ef5a0e94610438a9`, imgui `97e7afd7e08b583a138ca3b6bb602f892e120002`. 이는 검토 checkout 기록이며 설치 도구의 computedHash와 Git commit hash를 같은 값으로 취급하지 않는다.

## 3. 기존 전역 도구·정리 대상

| 목적 | 확인된 주요 도구 | 사용 범위 |
|---|---|---|
| 탐색·문서 | document-generate/release, claude-mem smart-explore/pathfinder/learn-codebase | 기억·인덱스 내용을 현재 소스와 대조 |
| 리뷰·결함 | gstack review/investigate/health/cso, code-review | C++·DX11 검사에 필요한 부분만 사용 |
| 개발 검증 | superpowers systematic-debugging, verification-before-completion, TDD·review 지침 | 기존 자체 TestFramework·CTest 유지. 이번 결함 재현·완료 검증에 적용 |
| 단순화 | ponytail·audit/review | 기존/표준/플랫폼 기능 재사용. 저장·인증 실패 생략 금지 |
| 아트 | fal-gamedev/character-design/media, imagegen | 필요할 때 생성·편집. 피벗·팔레트·라이선스 검수 별도 |
| 스킬 관리·시각화 | skill-installer/creator, plugin-management, diagram, visualize, Obsidian | 발견·검토·필요한 자료만 추가 |
| 다른 엔진·서비스 | Unity MCP, Supabase/Postgres, IDA, 웹/iOS 도구 | 실제 해당 도구를 사용하는 작업에만 적용 |

Codex·Claude 설정에는 개발·리뷰·브라우저·문서 플러그인이 다수 있다. 설치·enabled·현재 세션 노출·이번 실행을 동일하게 세지 않는다. 비밀 설정 값은 기록하지 않았다.

gstack은 상위 복사본·중첩 본체·fixture가 반복 노출된다. fixture까지 활성 스킬 수로 세지 않는다. 일부 지침의 `~/.Codex/skills/gstack`와 실제 `.agents/skills/gstack` 경로가 다르므로 사용 전에 실행 경로 확인이 필요하다. 현재 카탈로그/설치 기록상 superpowers Codex 6.4.1/Claude 5.1.0, ponytail 4.10.0/4.9.0, claude-mem 13.24.6/13.21.2 차이가 있다. 실행 프로세스가 각각 그 버전을 로드했다는 확인은 아니다. 사용자 전역 구성은 임의로 삭제·교체하지 않았다.

## 4. 추가하지 않은 후보와 근거

| 후보 | 판단 |
|---|---|
| Unity/Godot/Unreal 전용 스킬·전체 gamedev router | 자체 C++ ECS·DX11 API와 다르다. 필요한 개별 원칙만 선택 |
| C++ formatter (calcitem/sanmill) | Sanmill/Dart 전용 명령 포함. 현재 프로젝트 스타일과 맞지 않아 설치·일괄 포맷 보류 |
| pixel-art-sprites | 요청한 pixel 4종과 역할 중복. 검수 공백이 생길 때 추가 |
| 일반 웹 UI·WebGPU/Three.js·Metal | MyEngine의 native ImGui·HLSL UI를 직접 구현하는 도구가 아님 |
| 추가 이미지 제공자·거대한 제작 팩 | 기존 fal/imagegen·개발 워크플로와 중복. 확인된 부족분에만 추가 |
| cpp-pro/game-developer의 다른 팩 | 현재 C++·UI·셰이더 지침으로 기본 검토 가능. 추가 프레임워크·C++23 가정은 피함 |

## 5. 에이전트 현황과 권장 역할

### 확인된 상태

프로젝트·사용자 홈의 `.claude/agents`, `.codex/agents` 및 관련 사용자 지정 에이전트 경로에서 **등록된 MyEngine 전문 역할을 찾지 못했다**. Codex 설정에서도 별도의 전문 역할 정의를 확인하지 못했다. 현재 대화에는 협업 도구가 있지만 이번 조사에서 하위 에이전트를 실행하지 않았다.

superpowers의 `requesting-code-review/code-reviewer.md`와 구현·검토 프롬프트 템플릿, 시스템 `review-agent` 스킬은 설치되어 있다. 이는 재사용 가능한 리뷰 지침이며, C++/DX11·2.5D·영속성 전문 에이전트가 프로젝트에 등록됐다는 뜻은 아니다.

### 최소 권장 역할

아래는 프로젝트에 맞춘 **역할 제안**이다. 등록 파일이나 자동 병렬 실행은 아직 만들지 않았다. 먼저 [AGENTS.md](../AGENTS.md)의 공통 기준과 기존 리뷰 도구를 활용한다.

| 역할 | 책임 | 입력 | 결과와 경계 |
|---|---|---|---|
| `engine-reviewer` | C++ 소유권·모듈 경계·ECS 단계·DX11 깊이·자원 수명 검토 | 변경 diff, 관련 호출자, 렌더 계약·검증 결과 | 심각도·소스 위치·재현·최소 수정안을 제시. 기능을 임의로 재설계하지 않음 |
| `gameplay-integrator` | 에디터→에셋→씬→Lua/runtime→게임·서버 실행 경로 연결 | 완료 조건, 프로젝트 데이터, 기존 샘플·모듈 | 기존 기능을 재사용한 작은 통합 변경과 실행 시나리오. game 규칙을 엔진으로 역류시키지 않음 |
| `regression-reviewer` | 저장·경제·패킷 오류·종료·복구·픽셀/오디오 회귀 검증 | 재현 절차, 수정 diff, 기존 테스트·캡처 | 검증 결과와 빠진 경계를 보고. 리뷰 역할은 읽기 중심이며 구현 역할과 동일 파일을 동시에 수정하지 않음 |

서버 작업에서 `regression-reviewer`는 인증·경제·영속성 검토를 우선한다. 별도 보안 전용 에이전트가 계속 필요한지는 실제 작업량을 보고 결정한다. 에이전트 수를 늘리는 것 자체를 품질 개선으로 보지 않는다.

외부 역할 템플릿으로는 [wshobson/agents의 cpp-pro](https://github.com/wshobson/agents/blob/main/plugins/systems-programming/agents/cpp-pro.md)를 확인했다. RAII·const·이동 의미론·프로파일 중심 리뷰의 출발점으로 활용할 수 있다. 다만 원본은 특정 모델, C++23까지의 범위, GoogleTest/Catch2/Google Benchmark, 예외 안전성 등을 포함하므로 **그대로 등록하지 않고** C++20·Expected·자체 TestFramework·MSVC에 맞춰 줄여야 한다. 이는 조사한 외부 템플릿이며 설치 결과가 아니다.

## 6. 자체 지침으로 보완할 공백

일반 스킬만으로 MyEngine의 특수 계약을 알 수는 없다. 아래 지식은 현재 저장소 문서에서 유지하고, 같은 절차가 실제로 반복될 때 작은 프로젝트 스킬로 추출한다.

| 지식 | 현재 원천 | 스킬로 분리할 조건 |
|---|---|---|
| 2.5D 깊이·층·alpha cutout·픽셀 스냅·가림 시나리오 | [렌더 설계](02-rendering.md), [현재 구조](13-architecture-and-features.md) | 셰이더·렌더 변경마다 같은 검증 절차가 반복됨 |
| 모듈 추가·공개 의존성·수명·Expected·고정 틱 계약 | [AGENTS.md](../AGENTS.md), 각 CMakeLists | 여러 작업에서 동일한 작성·리뷰 절차가 반복됨 |
| 활성 세션·원장·스냅샷·실패 복구·패킷 경계 | [P0/P1 목록](14-development-priorities.md), 기존 테스트 | 안전한 공통 구현과 회귀 절차가 확정됨 |
| 에셋 피벗·방향·시트·메타·pak·씬 참조 | [에셋 설계](04-asset-pipeline.md), 실제 import 경로 | 콘텐츠 생산에 같은 검사가 반복됨 |

필요한 스킬 설치와 실제 결함 수정까지 진행했다. 전문 에이전트 등록·자동 병렬 실행은 하지 않았으며 기존 협업/리뷰 도구와 이 역할 제안을 필요할 때 사용한다.
