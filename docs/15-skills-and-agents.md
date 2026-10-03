# 15. 개발 스킬과 전문 역할

엔진 실행에는 스킬·에이전트가 필요하지 않다. 개발 보조는 저장소의 C++20·Expected·GUID/VFS·고정 틱·좌표 계약을 먼저 따른다. 설치, 구성 파일, 현재 세션 연결, 실제 실행을 구분한다.

현재 목표는 온라인 2D 픽셀 MMORPG다. [작은 작업 목록](22-2d-mmorpg-roadmap.md)에 맞춰 기획·프로그래밍·UI·에셋·애니메이션·운영/출시 역할을 적용한다. 새 3D 확장보다 2D 계산 공유·공식 온라인 앱 연결을 먼저 검증한다. 기존 스킬로 필요한 지침을 충족하므로 이 변경에서 패키지를 추가하지 않는다.

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

역할 구성과 실제 에이전트 실행은 구분한다. 구성 파일 검증과 제품 통합 검증도 별도로 기록한다. 2D/XYZ 온라인 변경에 맞춰 로컬 개발·게임 제작·릴리즈 스킬의 공통 물리, 활성 캐릭터 판별, 인증 루프백, 입력 재생/ack·원격 픽셀/퇴장, 클라이언트 Lua 격리, 실제 입력 미검증 및 배포 고지 지침을 갱신했다. 현재 작업은 추가 스킬 패키지나 병렬 에이전트 실행을 요구하지 않았다.

저장 Camera2D의 변경에는 기존 myengine-development를 적용했다. 새 카메라 패키지 대신 기존 render::Camera2D·리플렉션·고정 틱·Entity Lua 경로를 재사용했다. 로컬 세 스킬에 공용 카메라 계약·camera2d MCP 주제·실제 앱 픽셀 검사·네이티브 장치/배포 미검증을 갱신했다. 설치/설정이 현재 세션의 MCP 원격 제어를 뜻하지는 않는다.

프로젝트 입력 설정에는 기존 myengine-development와 imgui-ui-ux-engineering를 적용했다. 기본 위젯·두 열·스크롤·초안/취소·중지 중 저장으로 구성하며 새 패키지/병렬 에이전트는 추가하지 않았다. 로컬 개발·게임 제작·릴리즈 스킬에 input MCP 주제·inputMap·공통 틱 소비·합성 메시지/장치 검수의 차이를 갱신했다. UI 스코프 검사와 실제 MSVC 제품 빌드를 실행한다. 스킬의 별도 템플릿 전체 품질 검사에서 g++/c++ 컴파일러 부재는 제품 C++/CTest 통과와 구분해 기록한다.

사용자 입력 액션에는 기존 lua 지침을 추가 적용했다. 새 바인딩 계층 대신 InputBindingModule을 재사용하고 문자열 인자·읽기 전용 틱 상태·비소유 수명·종료 콜백을 검증한다. 로컬 세 스킬과 MCP의 input/lua 참조에 실제 API를 갱신했다. 스킬 설치·Lua 등록·현재 MCP 연결·직접 UI 조작 검수는 서로 다른 상태다.

원시 패드에는 기존 플랫폼 XInput과 InputState 샘플 경계를 재사용했다. 액션에 저장한 데드존을 한 번 적용하고 기존 필터 조회 API는 유지한다. 로컬 세 스킬에 작은 축/트리거·저장 Lua·포커스/연결 종료 회귀와 실제 패드 미연결 상태를 기록했다. 가상 패드 패키지·새 입력 인터페이스·병렬 에이전트는 추가하지 않았다.

입력 작성 창의 보완도 기존 ImGui 지침과 기본 위젯/Shortcut을 재사용했다. 실제 이벤트/크기·글꼴 배율 회귀와 native 캡처를 구분한다. 로컬 세 스킬에 작성/취소·화면 크기 변경·사람의 전체 탐색/모니터 DPI 미검증을 갱신했다. 새 UI 패키지나 병렬 에이전트는 추가하지 않았다.

2D 모션의 첫 단위는 기존 myengine-development·Lua 계약과 SpriteAnimator/ClipCursor를 재사용했다. 공통 조작/방향·후속 요청과 표시/고정 틱 이벤트를 분리했고 로컬 세 스킬에 animation2d MCP 주제·단일 클립 한계·저장 방향별 에셋의 다음 조건을 갱신했다. 새 애니메이션 라이브러리·스킬 패키지·에이전트는 추가하지 않았다. 라이브러리 방향 값, 실제 앱 이벤트/프레임과 완성 8방향 아트 재생을 구분한다.

방향별 작성 단위는 같은 AnimationAsset의 기본/선택적 클립 값과 기존 Undo·ImGui·GUID 경로를 재사용했다. 버전 1/2·직접/반전/기본 대체·실제 로컬/원격 픽셀과 손상 파일의 앱 오류 종료를 확인한다. 로컬 세 스킬과 animation2d MCP 참조를 현재 범위로 갱신했다. 공통 시간 비율/발 피벗·미리보기 연결과 연속 로컬/온라인 표현 검증을 마쳤으며 행동 전환은 다음 단위로 남긴다. 추가 패키지나 병렬 에이전트 실행은 필요하지 않았다.

방향 전환 단위는 기존 ClipCursor·논리 주기·SpriteGeometry를 재사용했다. UI 스킬의 ID/재생·일시정지 상태 검토와 실제 ImGui 위젯/픽셀 검증을 적용했다. 추가 패키지·에이전트·캐시 없이 같은 계산을 렌더/에디터 선택에 사용하며 스킬의 정적 검사/템플릿 gate 한계는 작업 기록에 남긴다.

연속 모션은 기존 myengine-development의 고정 틱·입력 재생·기존 RHI/JSON 오류 계약을 적용했다. 동일 프로세스의 제한된 중간 캡처와 진단 표현 pause를 추가하고 로컬 세 스킬·animation2d MCP 참조를 동기화했다. 새 라이브러리·스킬 패키지·에이전트·전역 온라인 시계는 추가하지 않았다.

행동 저장 단위는 myengine-development와 기존 상태/조건 모델·GUID/VFS·Expected 저장 계약을 재사용했다. 값 정의를 asset 경계로 옮기고 runtime alias를 보존해 역참조를 만들지 않았다. 파일/참조/삭제 보호와 전이별 정책을 검사하며 에디터 작성/실행 연결은 다음 단위로 구분한다. 추가 패키지·에이전트는 필요하지 않았다.

행동 재생 바인딩은 기존 상태 계산·GUID/DB·Lua·문서 CommandStack을 재사용했다. 일반 바인딩과 고정 틱, 문서/캐시 수명 세대를 구분하며 로컬 세 스킬과 MCP animation2d/lua/components 참조를 현재 범위로 갱신한다. 전용 행동 작성·Undo·실제 GUI 수명 검수는 다음 단위이며 새 패키지/에이전트/픽셀 제작 도구를 추가하지 않았다.
