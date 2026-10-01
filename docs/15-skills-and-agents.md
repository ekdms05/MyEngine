# 15. 개발 스킬과 전문 역할

엔진 실행에는 스킬·에이전트가 필요하지 않다. 개발 보조는 저장소의 C++20·Expected·GUID/VFS·고정 틱·좌표 계약을 먼저 따른다. 설치, 구성 파일, 현재 세션 연결, 실제 실행을 구분한다.

## 필요한 스킬

| 작업 | 적용 지침 | 판단 |
|---|---|---|
| 네이티브 에디터 | imgui-ui-ux-engineering, clean-code | 현재 ImGui·키보드·간격·DPI·수명 계약에 직접 적용 |
| C++·빌드·최적화 | cpp-coding-standards, cmake, performance-optimization | C++20·MSVC·기존 테스트·측정 우선 |
| Lua | lua, myengine-development | C API·VM 수명·콜백 오류·에디터 제공 범위 검증 |
| 게임 UI·맵 기획 | game-ui-ux, level-design | 게임 제작 흐름·가독성·공간 설계 |
| 외부 에셋·애니메이션 | pixel-art-creator/professional/exporter/animator, create-game-assets | 외부 제작 도구에서 사용하고 PNG/.anim/GUID 계약으로 임포트 |
| 제작·출시 | myengine-game-production, myengine-release | 프로젝트 검사 → 제작 → 회귀 → 라이선스/패키지 → 새 환경 실행 |
| 발견 | find-skills | 원문·의존성·중복·적용 경계를 확인한 뒤 필요한 것만 설치 |

2026-10-02 로컬 구성에 `level-design`, `create-game-assets`를 추가했다. [skills.sh](https://www.skills.sh/) 후보와 원문 [gamedev-skills/awesome-gamedev-agent-skills](https://github.com/gamedev-skills/awesome-gamedev-agent-skills) 지침을 검토하여 기존 C++/ImGui와 겹치지 않는 기획·에셋 생산 경로를 선택했다. 개발 스킬·설치 메타데이터는 로컬에 유지한다. 원문의 다른 엔진 전용 명령이나 새 프레임워크 설치 요구는 MyEngine에 그대로 적용하지 않는다.

## 역할과 책임

픽셀 프롬프트 제작은 게임 프로젝트에 `myengine-pixel-production` 지침과 픽셀 아티스트/애니메이터 역할을 구성한다. 현재 세션에서 이미지 생성 도구와 Aseprite 연결 여부를 먼저 확인하고 설치된 전문 지침의 도구 이름을 연결 상태로 간주하지 않는다. PNG/시트는 컨셉·픽셀 그리드·팔레트/알파·방향·프레임/시간·발 피벗을 검수한 뒤 공식 임포트한다. 생성 초안·아트 승인·엔진 로딩·물리·온라인 검증을 분리한다. 새 이미지 의존성이나 엔진 내 제작기를 추가하지 않는다.

릴리즈만 사용하는 게임 제작자는 공개 문서와 배포본에서 검증하고, 엔진 제작자는 소스 원인/수정·회귀를 담당한다. 원문 피드백은 보존하고 같은 로컬 문서 폴더에 ID·버전·변경 이유·검증 증거·남은 완료 조건·다음 재검증을 담은 제작자 답변을 둔다. 소스 대상 MCP를 소비자에게 연결하여 역할 경계를 우회하지 않는다.

로컬 `.codex/agents`에 다음 전문 역할 구성 파일을 두었다. 다음 세션에서 도구가 읽을 수 있는 구성이지, 상시 실행 중인 서비스라는 의미는 아니다.

| 역할 | 결과와 경계 |
|---|---|
| myengine-planner | 제작 의도·범위·완료 조건 |
| myengine-game-designer | 게임 규칙·맵 동선·포털·스폰 시나리오 |
| myengine-game-programmer | C++/Lua·컴포넌트·고정 틱 게임 규칙 |
| myengine-ui-designer | 에디터/게임 UI·설명·간격·키보드·DPI |
| myengine-asset-artist | 원본·라이선스·팔레트·피벗·시트·GUID |
| myengine-animator | 프레임·타이밍·이벤트·전환·외부 리깅 |
| myengine-engine-programmer | 수명·모듈·좌표·데이터·신뢰 경계 |
| myengine-release-reviewer | 회귀·패키지·해시·문서·새 PC 조건 |

역할은 실제 작업이 있을 때 필요한 것만 실행하고 동일 파일의 동시 수정을 피한다. 영속·인증·거래는 단순화를 이유로 검증을 생략하지 않는다. 최신 API와 MCP를 바꾸면 `myengine-development`, `myengine-game-production`, `myengine-release`의 실제 명령·경계도 함께 갱신한다.

이번 작업에서는 Lua 바인딩, 에셋 관리, MCP/배포의 전문 작업을 분담했다. 구성 파일 검증과 실제 제품 통합 검증은 별도로 기록한다.
