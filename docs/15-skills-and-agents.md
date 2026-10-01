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
| pixel-art-creator | [pixel-plugin](https://github.com/willibrandon/pixel-plugin) | skillfish 설치 후 Codex 로컬 메타데이터 보정. 제공된 Aseprite/pixel-mcp 명령은 해당 도구가 필요. 이번 기본 PNG는 내장 imagegen 사용 |
| pixel-art-professional | [pixel-plugin](https://github.com/willibrandon/pixel-plugin) | 팔레트·실루엣·픽셀 규칙·검수 |
| pixel-art-exporter | [pixel-plugin](https://github.com/willibrandon/pixel-plugin) | 프레임/시트·투명도·메타 내보내기 |
| pixel-art-animator | [pixel-plugin](https://github.com/willibrandon/pixel-plugin) | 방향·프레임·타이밍·루프 지침 |

이 스킬의 핵심 지침을 읽고 필요한 범위만 적용했다. 초기 구조 조사에서는 픽셀 제작을 실행하지 않았다. 이후 기본 콘텐츠 작업에서 픽셀 4종의 실루엣·피벗·타이밍·시트 지침을 적용하고 내장 imagegen으로 PNG 2개를 제작했다. 외부 Aseprite/pixel-mcp 제작 서버는 실행하지 않았다. 기준 PPU 48·타깃 960×540·발밑 피벗·alpha cutout은 저장소 [렌더 계약](02-rendering.md)이 우선한다.

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

## 7. build 산출물 정리 스킬 조사

2026-10-01에 `find-skills`로 leaderboard와 `build cleanup`, `file organizer`, `cmake clean`, `repository cleanup` 검색 결과를 확인했다. 설치 수는 skills.sh 조회값, 별 수와 revision은 GitHub 조회값이며 품질을 보장하는 지표로 취급하지 않는다.

| 후보 | 조회 결과 | MyEngine 적용 판단 |
|---|---|---|
| 기존 `cmake` | 프로젝트에 설치됨 | 현재 빌드 경로·generator·구성별 산출물 판단에 재사용. 실제 파일 정리는 PowerShell 기본 기능으로 충분 |
| [file-organizer](https://www.skills.sh/composiohq/awesome-claude-skills/file-organizer) | ComposioHQ, 약 6.3K 설치·76,183 stars | 일반 파일 정리 보조 후보로 추천. 구조/용량 조사·보존/보관 계획·작업 기록이 유용하나 CMake 의존성·재생성 가능 여부는 별도로 확인해야 함 |
| [repo-cleanup 원문](https://github.com/nickcrew/claude-cortex/blob/bb47af79ad3befe01ae01940fcf5f16e30a1b6df/skills/repo-cleanup/SKILL.md) | NickCrew, 166 설치·47 stars | 사용처·재생성 여부 확인은 적합하지만 [참조 문서](https://github.com/nickcrew/claude-cortex/blob/bb47af79ad3befe01ae01940fcf5f16e30a1b6df/skills/repo-cleanup/references/code-cleanup.md)에 `build/` 전체 삭제 예제와 웹 프로젝트 가정이 있다. 현재 실행 파일·검증 기록이 함께 있는 MyEngine에 그대로 적용하지 않음 |
| project-workspace-cleaner | 검색 결과 161 설치 | 원본 GitHub 저장소 조회가 404로 실패하여 원문 검증·추천·설치에서 제외 |

file-organizer 원문은 `ComposioHQ/awesome-claude-skills` revision `be2a406907dbc61b73e6827ded415c96139d13a2`의 `file-organizer/SKILL.md`를 확인했다. 일반 파일 정리용 설치 명령은 다음과 같다. **조사 단계에서는 추가 설치하지 않았다.** 기존 CMake 지침·현재 저장소 계약·PowerShell로 빌드 정리 범위를 판단할 수 있어 중복 도구를 늘리지 않았다.

```powershell
npx.cmd skills add https://github.com/ComposioHQ/awesome-claude-skills --skill file-organizer --agent codex --yes
```

MyEngine에서는 확장자·날짜·같은 파일명만으로 삭제하지 않는다. `build/dev`의 Debug/Release 실행 파일을 유지하고, 구 빌드 안의 독립 스크립트·테스트 프로젝트·캡처와 서버 저장 데이터는 먼저 별도로 확인한다. CMake 트리 자체를 이동하면 cache의 절대 경로가 맞지 않으므로 필요한 증거/소스만 보관하고 재생성 가능한 산출물을 정리한다.

[CMake 공식 문서](https://cmake.org/cmake/help/latest/manual/cmake.1.html#build-a-project)에 따르면 `--target clean`은 정리만 하고 `--clean-first`는 정리 후 다시 빌드한다. 이 명령은 지정한 빌드 트리에 적용되므로 다른 구 트리 전체 정리의 대체 수단이 아니다. 현재 사용 중인 `build/dev`에 정리 명령을 실행하지 않았다. 실측 분류와 보존 근거는 [작업 기록](16-foundation-worklog.md)에 남겼다.

### 실제 경로 정리에 적용한 스킬

후속 소스·빌드 경로 정리에서는 사용자 전역에 이미 설치된 `C:/Users/harun/.codex/skills/file-organizer/SKILL.md`와 프로젝트의 `.agents/skills/cmake/SKILL.md`를 읽고 적용했다. 추가 다운로드는 하지 않았다. file-organizer의 목록 조사·보관 계획·해시 대조·수정 시각 보존·작업 기록을 적용하고, CMake 의존성·현재 실행 경로 확인은 저장소 계약에 따라 별도로 수행했다.

빌드 명령은 별도 래퍼 없이 CMake의 기본 preset으로 통일했다. 실제 정리는 PowerShell의 경로 검증·파일 복사·이동·삭제로 수행했으며 `build/dev`와 검증 자료는 유지했다. 보관 목록과 SHA-256은 로컬 `archive/build-cleanup-2026-10-01/manifest.json`, 정리 범위·재사용 제약·검증 결과는 [작업 기록](16-foundation-worklog.md)에 있다.

### 프로젝트·씬 파일 작업에 적용한 스킬

에디터 create/open/save 연결에는 프로젝트에 이미 설치된 imgui-ui-ux-engineering과 cpp-coding-standards를 읽고 적용했다. 기존 ImGui·Windows 파일 선택 API·공용 직렬화를 사용하므로 추가 패키지를 설치하지 않았다. Godot의 공식 자료를 기능 벤치마킹 근거로 사용하며 전문 역할의 별도 병렬 실행은 하지 않았다. 스킬 정적 검사 오탐과 g++/c++ 전용 컴파일 게이트의 MSVC 제약, 제품 자체 빌드·테스트·실제 캡처의 범위는 [작업 기록](16-foundation-worklog.md)에 구분해서 기록했다.

## 기본 콘텐츠 작업의 실제 도구 연결

2026-10-01: MyEngine MCP 도구는 현재 대화의 직접 호출 목록에 여전히 없지만, 설치된 공식 MCP SDK의 Client·StdioClientTransport로 저장소 서버에 연결했다. listTools에서 8개 도구를 확인하고 engine_capture_frame으로 실제 Release MyEditor의 셸·씬·Play를 캡처했다. 설정 파일 존재, SDK를 통한 연결, 세션 도구 노출을 구분한다. 서버 소스는 수정하지 않았다.

픽셀 creator/professional/animator/exporter의 SKILL.md를 읽고 이미지 구성·투명 시트·발밑 피벗·프레임 시간에 적용했다. 내장 imagegen으로 원본 PNG를 만들고 기존 SpriteSheet/AnimationClipData와 .meta GUID로 내보냈다. Aseprite나 pixel-mcp 명령을 실행했다고 기록하지 않는다. 새 패키지 설치나 병렬 에이전트 실행은 필요하지 않았다.

Computer Use 스킬(`C:/Users/harun/.codex/plugins/cache/openai-bundled/computer-use/26.928.20755/skills/computer-use/SKILL.md`)을 읽고 @oai/sky를 불러왔으나 list_apps는 `Trusted RPC service is not configured: sky`로 실패했다. cua.getState의 네이티브 앱도 비활성이다. 직접 UI 조작을 다른 입력 주입 경로로 우회하지 않았다. Godot 스프라이트 애니메이션 문서의 Chrome 접근은 보안 정책의 권한 거부로 실패했고 해당 자료를 다른 브라우저/HTTP 경로로 우회 조회하지 않았다. 소스/API 회귀와 실제 앱 렌더 확인은 수행했으며 네이티브 사용자 조작 검증은 별도로 남긴다.

## 오브젝트·조작·맵 연결 작업

이번 작업은 사용자 전역 clean-code(`C:/Users/harun/.codex/skills/clean-code/SKILL.md`)와 lua(`C:/Users/harun/.codex/skills/lua/SKILL.md`)를 읽고 적용했다. 설치된 cpp-coding-standards·imgui-ui-ux-engineering·픽셀 아트 스킬의 기존 계약도 유지한다. game-ui-ux는 확인했으나 이번 직접 ImGui 컴포넌트 편집에 맞는 범위만 참고했다. 기존 ECS·물리·Lua·씬 직렬화·전환으로 요구를 충족하므로 새 패키지나 범용 그래프 라이브러리를 설치하지 않았다. 병렬 에이전트도 실행하지 않았다.

MCP는 공식 SDK를 통한 8개 기존 도구 연결과 실제 MyEditor 프레임 캡처를 사용한다. 직접 세션 도구 노출·에디터 내부 원격 편집을 새로 제공한 것은 아니다. Computer Use의 네이티브 RPC 미구성 및 Godot 페이지 접근 거부는 계속 적용되며 우회 입력/조회 없이 API 회귀와 실제 렌더 검증을 구분한다.

## UI·제작 가이드 스킬 평가와 설치

2026-10-01: find-skills로 skills.sh와 `imgui ui ux design` 검색을 확인하고 원문·실제 프레임워크·검증 지원·중복·의존성으로 판단했다. 설치 수는 참고 수치이며 품질 근거로 쓰지 않았다.

| 후보 | 판단 | 실제 적용 |
|---|---|---|
| 기존 imgui-ui-ux-engineering | Dear ImGui 작업에 직접 맞음. 버전 주의 문서까지 확인 | 문서 상태·도킹·작업 계층·중립 테마·vector+text·클리핑·정적 scope 검사 |
| 기존 game-ui-ux | 게임 HUD/입력/정보 계층 지침은 유효, 웹/React 예제를 네이티브 셸로 대체할 이유 없음 | 콘텐츠 작업 흐름·포커스 원칙만 참고 |
| 기존 Pixel Art creator/professional/animator/exporter | 참조·분리 부위·프레임 시간·PNG 시트와 소스/출력 구분에 적합 | MyEngine 자체 제작 API에 적용. Aseprite 도구 연결/실행은 확인되지 않음 |
| [Vercel web-design-guidelines](https://skills.sh/vercel-labs/agent-skills/web-design-guidelines) | 공식 원문·명확한 접근성/포커스/미디어/레이아웃 리뷰 기준. HTML 제작 가이드에 필요한 중복 없는 검사 | 새로 설치하고 원문 및 최신 guideline을 읽음. label/alt/크기/skip link/focus-visible/video controls/자동재생 제외 반영 |
| 기존 frontend-design 플러그인 | 이미 설치되어 있어 중복 다운로드 필요 없음 | 이번 네이티브 UI에 웹 컴포넌트 라이브러리 추가하지 않음 |
| minimalist-ui / ui-ux-pro-max 등 폭넓은 웹 스킬 | 검색 노출·대규모 설치가 자체 C++ 도킹에 적합하다는 뜻은 아님 | 기존 ImGui 지침과 정적 웹 가이드로 충족하여 추가 설치하지 않음 |

```powershell
npx.cmd skills add https://github.com/vercel-labs/agent-skills --skill web-design-guidelines --agent codex -y
```

설치 위치는 프로젝트 .agents/skills/web-design-guidelines, 원본/해시는 skills-lock.json에 기록한다. 설치 파일은 ignored이며 실행 중 세션의 자동 도구 등록과 동일하지 않다. 필요한 지침을 직접 읽고 적용했으며 에이전트 정의·병렬 실행·React/폰트/아이콘/영상 인코더 패키지는 추가하지 않았다. 영상은 이미 설치된 OpenCV→Windows Media Foundation H.264, GIF는 Pillow로 기존 출력 프레임을 인코딩했다.

ImGui scope 정적 검사는 4개 변경 UI 파일 모두 통과했다. 스킬 quality_suite의 semantic checks 30/30과 공식 header 확인은 통과했으나 전체 게이트는 g++/c++를 찾지 못해 FAIL이다. 제품은 별도 MSVC Debug/Release 빌드·CTest로 검증하며 이 결과로 스킬 전체 FAIL을 성공으로 바꾸지 않는다. 로컬 가이드 브라우저 접근은 권한 거부되었고 UI/비디오 브라우저 검증을 우회하지 않았다.
