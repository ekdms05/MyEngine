# 16. 개발 기반 정리 기록

기준일: 2026-10-01. 목적은 직접 설계한 C++20·DX11 2.5D 엔진으로 픽셀아트 온라인 RPG를 제작할 수 있는 기반을 정리하는 것이다. Simple Is Best를 따른다. 라이브 서비스 완성과 로컬 개발 기반의 검증을 구분한다.

## 작업 기준

변경은 호출 경로·실행 결과·제품 요구 중 하나 이상의 근거를 가져야 한다. 삭제는 사용처 검색과 검증 뒤에 한다. 동작을 바꾸지 않는 단순화와 측정으로 확인한 성능 개선을 구분한다. 사용자의 기존 타이틀·캐릭터·음원 작업은 보존한다.

이번 작업의 검증 범위는 저장·경제 일관성, UDP 입력 경계, 실제 게임 메뉴·설정·오디오 경로, 에디터 UI의 일관성, 코드 책임 분리, 제품 설명, 재현 가능한 개발 도구 구성이다. 외부 서비스용 전송 보안·대규모 운영·콘텐츠 전체 제작은 별도 완료 조건을 갖는다.

## 도구 선택 기록

| 선택 | 근거 | 적용 경계 |
|---|---|---|
| 요청한 pixel-art-creator/professional/exporter/animator | [원본 프로젝트](https://github.com/willibrandon/pixel-plugin)의 생성·팔레트·내보내기·프레임 지침 | skillfish로 프로젝트 설치. Codex에도 등록. Aseprite·pixel-mcp 연결은 설치와 별개이며 현재 확인되지 않음 |
| cpp-coding-standards | 기존 C++20 코드의 소유권·RAII·명확한 인터페이스 검토 | 프로젝트의 Expected 오류 전달·이름 규칙 우선 |
| cmake | 기존 정적 라이브러리와 앱의 타깃별 의존성 검토 | 기존 MSVC·CMake 3.26 환경 유지 |
| performance-optimization | 측정 없는 캐시·풀·병렬화 방지 | Release·동일 조건의 결과로만 성능 주장 |
| shader-programming | HLSL 좌표·UV·깊이 계약 검토 자료 | alpha cutout·depth write 계약을 일반 alpha blending으로 대체하지 않음 |
| game-ui-ux | 메뉴 흐름·키보드·화면 배율·HUD 연결 | 현재 엔진 위젯·픽셀 타깃을 사용. 필요 없는 화면 스택 프레임워크는 도입하지 않음 |
| imgui-ui-ux-engineering | 실제 ImGui 에디터의 계층·상태·ID·스코프·테마 검토 | vendored ImGui 버전을 기준으로 구현·검증 |

[MCP Market 게임 개발 목록](https://mcpmarket.com/ko/tools/skills/categories/game-development)도 조사했다. 목록의 Unity/Godot 도구는 현재 MyEngine과 API·언어가 다르므로 추가하지 않았다. C++ formatter 후보는 Sanmill/Dart 전용 명령을 포함하므로 저장소 전체 포맷이나 다른 프로젝트 도구를 가져오지 않았다. 기존 코드 스타일과 실제 변경 책임을 우선한다.

픽셀 creator 원본의 한 줄 description에는 YAML에서 인용되지 않은 콜론이 있어 `skills` CLI가 건너뛰었다. skillfish 다운로드본의 설명을 인용하고 이름을 폴더명에 맞춰 Codex 등록 메타데이터만 보정했다. 제작 지침은 유지했다. 설치 경로와 실제 사용 가능 조건은 [도구 문서](15-skills-and-agents.md)에 기록한다.

## 변경·검증 기록

| 변경 | 호출 경로·결함 근거 | 조치·검증 |
|---|---|---|
| 정확한 JSON 정수 | int64 Value가 double로 변환되어 2^53 이후 ID/골드 손실, 큰 double→int 변환 위험 | 내부 Number의 int64 보관·from_chars·범위 검사. min/max/2^53+1·범위 밖 회귀 |
| 활성 세션 저장 | GameServer::WriteSessionInto는 Leave만 호출, main 자동/종료 저장은 초기 기록 사용 | FlushSessions/Save를 공용 경계로 사용. Stop은 세션 반영 후 해제. 활성 위치/HP/XP 테스트·실제 봇 재시작 |
| 저장 원자성·오류 | 세 파일의 순차 교체·부분 로드, 서버 로드 오류 경고 후 계속 실행 | 한 version=1 state.json, 전체 후보 로드, shared JsonFile의 flush→교체. 로드/종료 저장 오류 종료. 임시 파일 실패 시 이전 저장 보존 |
| 백업 | 이전 백업 조기 삭제·copy 오류 누락·빈 백업 복원 위험 | 검증한 이전 세대 게시→새 저장→오래된 백업 정리. 빈/불완전 백업 거부. 기존 회전/복원 및 실패 보존 회귀 |
| 저장 파싱 경계 | 기록 ID/카운터/이름 중복·숫자/아이템/좌표 타입을 관용 fallback으로 수용 | 배열·타입·범위·seq·대칭 거래·running balance 검사, 실패 시 메모리 보존. Read/Write 모두 64 MiB 제한 |
| 원장·인벤토리 | GrantItem/ConsumeItem이 먼저 변경하고 Ledger 결과 무시 | 후보 인벤토리→원장 결과 확인→반영, Expected 반환·모든 기존 호출자 갱신. 원장 실패·수량·골드 오버플로 회귀 |
| 정합성·성능 | 여러 스택을 각각 원장 전체 잔액과 비교, 원장 전용 아이템 누락·반복 탐색 | 원장 한 번 replay·양쪽 아이템 합계·안정된 불일치 키. 중복 스택/원장 전용 회귀, 아래 Release 전후 측정 |
| 비밀번호·토큰 | 자체 FNV 스트레칭·예측 가능한 token·만료 없음 | 기존 Windows CNG 사용, PBKDF2-SHA256 600,000회·16B salt·32B digest·32B 난수 token/24h. 구형 로그인 이관·재로드·잘못된 ban 타입·만료 회귀 |
| 외부 노출 제한 | Connect에 평문 비밀번호가 있으며 새 hash만으로 전송이 보호되지 않음 | NetServer는 loopback 바인드. 전송 암호화·패킷 세션 인증·로그인 제한 완료 전 외부 개방하지 않음 |
| UDP 파싱·이동 | 절단/과거 seq 입력·varuint 64bit overflow, 축별 clamp로 대각선 속도 증가 | protocol v1/타입/범위/길이/순서 검사, 공통 NormalizeMove와 양자화된 예측 입력. 실제 UDP 절단·역순·대각 회귀 |
| 스냅샷·예측 | 제한 큰 count와 부분 결과 반영, 발신자 미확인·확인된 입력을 반복 front erase | 64명 상한·ID/seq/중복·후보 검증·서버 endpoint·새 tick 확인, 확인된 prefix 한 번 erase. 재접속 시 예측/스냅샷 초기화 |
| 서버 CLI | atoi/strtoul이 잘못된 값/범위를 조용히 수용·port 축소 | from_chars와 port 0..65535, tickrate 1..120, bots 0..64, autosave 0..86400 검사. 실제 앱 exit 64 확인. port=0은 검증용 임의 포트 |
| 서버 로그·운영 | 로그 초기화 누락, 등록/저장/reload 실패 무시 | Log 수명 명시·중요 Expected 전파, config/metrics 경고. 실제 손상 로드 exit 4·파일 hash 보존 |
| 오디오 수명 | MyGame의 별도 무음 AudioEngine, AudioModule보다 먼저 asset 해제 가능·Music 명령은 callback mutex 미공유 | 기존 AudioModule 조합·headless만 무음. Music 명령/조회는 같은 mutex, Game은 BGM Stop 후 clip 해제. 무음 crossfade/정지 후 clip 해제 회귀·실제 장치 초기화 확인 |
| 고정 틱·월드 | 가변 Update에서 로컬 위치 변경, 새 sprite의 WorldTransform 누락 | 코어 FixedUpdate 이동·WorldTransform 추가. 기존 물리/층 판정 연결은 남김 |
| UI 라벨·좌표 | 두 메뉴가 같은 라벨, 클릭 배율 계산이 실제 정수 blit와 다름 | 항목 데이터 사용·ComputeLayout 재사용. 타이틀/설정 실제 프레임 확인 |
| UI 글리프 | R8 atlas를 RGBA 샘플→붉은 글자/불투명 사각형, +Y up quad로 UI 뒤집힘, panel sort가 text 덮음 | SpriteDraw alphaMask/yDown·UI painter 순서, TextRenderer/UiDrawContext/게임/village 호출자 갱신. DX11 2×2 coverage 실제 픽셀 회귀 |
| UI 배경·설정 | 바닥 피벗 배경을 양의 반높이에 배치, User 스코프 로드 누락 | 배경 위치 수정, 코어 User 로드·설정 변경/종료 저장, Windows 폰트 위치 조회. 격리 user.json의 캐릭터·25% BGM·50% SFX 프레임 확인 |
| 타이틀 상태 | DestroyTitleScene이 월드 전체를 정리하는 책임 혼합 | 추적한 타이틀 엔티티/연기만 파괴. 플레이 종료의 월드 정리는 BackToTitle에 명시. 사용자 타이틀 데이터 보존 |
| 에디터 | 조밀한 패널의 대비/간격·플레이 중 새 씬/저장·확장 버튼 같은 ID 위험 | 기존 dark/orange 테마 개선, 플레이 상태 버튼 비활성/공통 NewScene guard·확장 index ID. MSVC 빌드·스코프 검사·오프스크린 실행 |
| 제품 설명 | 상용 게임명·표현이 기술 요구를 대신함, README 완료 범위 과장 | 자체 소스/문서의 외부 게임명 제거·좌표/깊이/빌보드 기술 설명으로 대체. 현재 기반/미완성 통합을 구분 |
| 구조·개발 도구 | 설계 목표와 앱 연결 상태 혼재, 설치/연결을 동일 취급 | docs 13/14/15·문서 안내·AGENTS, 설치 출처/lock/전제 조건과 최소 전문 역할 제안 기록 |

비밀번호 구현의 근거: [CNG PBKDF2/HMAC provider](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptderivekeypbkdf2), [시스템 난수](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptgenrandom), [OWASP PBKDF2-SHA256 비용](https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html). 운영 요구 없이 새 암호 라이브러리를 추가하지 않고 현재 Windows 플랫폼 기능을 사용했다. 로그인 이관은 다음 저장 성공 시 디스크에 유지된다.

## 유지·보류·삭제 판단

| 판단 | 근거·검색 범위 |
|---|---|
| AssetHandle::Pin/Unpin 삭제 | engine/apps/game/samples/tests의 정의·호출 검색에서 선언/빈 TODO 정의 4건뿐. 동작 없음. 참조 카운트/RAII 수명 유지 |
| SetWhiteTexture setter·GameApp 보관 포인터 삭제 | 앱 안에서 정의/즉시 CLI 주입 외 사용 없음. 기존 texture 로딩·소유권 유지 |
| 중복 AudioEngine 생성/Update/해제 제거 | AudioModule에 이미 초기화·출력·설정·PostUpdate·역순 종료가 있어 재사용 |
| UI 시스템/범용 인터페이스 추가 보류 | 기존 SpriteBatch·TextRenderer·ImGui가 실제 문제를 해결. 한 구현용 factory·새 화면 프레임워크 필요 없음 |
| render↔scene 대규모 재작성 보류 | 소스 결합은 확인했으나 순수 입력 계약/독립 링크 검증이 선행되어야 함. include로 숨기지 않도록 P2로 기록 |
| 기존 RPG/social/mmo 모듈 유지 | 라이브러리 호출자·테스트·요구가 있다. 앱 미연결은 삭제 근거가 아니며 실제 명령/UI 통합으로 분류 |
| DB·분산 서버·다른 GPU API·캐시/풀/스레드 보류 | 현재 플랫폼·측정된 병목·운영 동시성의 증거 없음. 해당 요구가 확정되면 별도 구현 |
| 원본 에셋 보존 | 기존 사용자 수정·PNG·음원을 변경/재생성하지 않음. 여러 포즈가 한 PNG로 보이는 문제와 시트/피벗/배경 정리는 정식 아트 과제로 남김 |
| 외부 코드·라이선스 보존 | third_party 저작자/라이선스와 외부 스킬 본문 유지. 자체 제품 설명·커밋에는 도구 공동 작성자/생성 홍보 문구를 넣지 않음 |

## Release 성능 측정

측정은 Windows / Intel Core i7-12700 / MSVC Release(/O2)에서 같은 입력으로 수행했다. GPU FPS 측정이 아니다. 캐릭터 1명, 서로 다른 아이템 스택 100개, 원장 10,000건, 대조 100회/라운드·3라운드다. 비밀번호·등록·파일 I/O는 측정 영역에서 제외한다. 이 정상 입력에서는 이전/현재 대조가 모두 true인지 먼저 확인한다.

| 라운드 | 이전 스택별 replay (µs/회) | 현재 합계 대조 (µs/회) |
|---|---:|---:|
| 1 | 507.568 | 38.452 |
| 2 | 502.131 | 37.183 |
| 3 | 506.325 | 37.986 |

중앙값 약 506.325→37.986µs, 약 13.3배다. 중복 스택을 올바르게 합산하는 정합성 수정과 반복 ledger 탐색 감소를 함께 확인했다. 다른 장면·전체 CPU/GPU·MMO 수용량 개선으로 확대하지 않는다. 원장의 매 변경 시 선형 탐색은 남아 있으며 측정 없이 캐시를 추가하지 않았다.

재현 소스: [bench-reconcile.cpp](../tools/bench-reconcile.cpp). 기존 Release 엔진을 먼저 빌드하고 Visual Studio 개발자 명령 프롬프트에서 다음을 실행한다.

```powershell
cl /std:c++20 /O2 /EHsc /MD /utf-8 /I engine/core/include /I engine/persist/include tools/bench-reconcile.cpp build/dev/engine/persist/Release/mye_persist.lib build/dev/engine/core/Release/mye_core.lib bcrypt.lib /Fe:build/foundation/bench-reconcile.exe /Fo:build/foundation/bench-reconcile.obj
build/foundation/bench-reconcile.exe
```

이번 환경에서는 같은 소스/Release 라이브러리를 연결한 임시 CMake 타깃으로 실행했다. 이전 알고리즘은 변경 전 PersistenceService::Reconcile의 스택별 GoldBalance/ItemBalance 호출을 벤치마크에만 보관했다.

## 검증 결과와 한계

- 전체 Debug/Release 빌드 성공. CTest 각각 13/13, 내부 테스트 511/511. 기존 custom TestFramework를 유지했다. 초기 재현에서 정합성·로드 실패·경제 경계 테스트가 실패했으며 수정 후 통과했다.
- `tools/verify-foundation.ps1`을 Debug/Release 서버에 실행: 잘못된 CLI exit 64, 등록/캐릭터 생성, 봇 자동/종료 저장·재시작, 손상 state.json exit 4와 파일 hash 보존 통과. 매 실행 새 build 하위 데이터를 사용한다.
- MyGame headless 타이틀/설정/로컬 플레이, MyEditor headless viewport 캡처 성공. R8 투명/128/255 coverage와 UI 방향은 GPU 출력 회귀로 검사한다. 격리 User 설정의 선택·25%/50% 음량 표시는 캡처로 확인했다.
  [타이틀 프레임](images/foundation-title.png)은 실제 960×540 출력이다. 아트 생성이나 리터칭을 하지 않았다.
- 일반 MyGame 실행 로그에서 `AudioModule: audio device 초기화 완료`, title BGM WAV import, 정상 종료를 확인했다. 사람의 청취·실제 출력 음질/볼륨 평가는 수행하지 않았다. headless 무음 검증과 구분한다.
- ImGui 변경 파일의 `check_imgui_cpp.py` 스코프 검사 통과. 스킬의 quality suite는 구조/참조 semantic 30/30·공식 revision fetch까지 통과했으나 g++/c++가 없어 **템플릿 compile gate 실패**. MyEngine 실제 vendored 코드의 MSVC 빌드는 통과했다. 다양한 DPI·좁은 창·전체 에디터 상호작용은 미검증이다.
- `tools/mcp`의 `npm.cmd run build`와 `npm.cmd run smoke`: 8개 도구·ALL PASS. 현재 세션 MCP 연결/에디터 원격 제어 완성을 뜻하지 않는다.
- 기존 third_party sol2 UTF-8 escape 경고, 일부 테스트/village의 fopen/getenv C4996 경고는 남는다. 무관한 외부 코드나 저장소 전체 재포맷으로 숨기지 않았다.
- 현재 UDP 전송은 루프백·64명 상한이며 TLS/패킷 세션 인증·로그인 제한·timeout·틱 협상·신뢰 이벤트는 미완성이다. 백업은 파일 세대 복구이며 운영 DB/WAL/PITR 보장이 아니다.
- 에디터 실제 프로젝트/씬 열기, MyGame 물리·Lua/runtime·게임 명령·사회/사냥 콘텐츠, 정식 픽셀 아트·배포 폰트·전체 프로파일은 [우선순위](14-development-priorities.md)에 남긴다. 라이브러리 존재를 게임 완성으로 표시하지 않는다.

작업 시작 전 수정은 build/foundation/preexisting.patch로 따로 보관하고 기존 사용자 타이틀/아트/음원·도구를 보존했다. 커밋은 검증한 기반과 문서/콘텐츠를 함께 남긴다. 작성자 설정·기존 이력·제3자 출처는 변경하지 않는다.

문서 참조 검사에서 초기 MMO 설계의 존재하지 않는 참조 152건을 발견했다. 같은 주제를 다루는 현재 파일이 있는 112건은 실제 파일로 연결하고, 독립 서버 토폴로지 등 작성되지 않은 문서 40건은 미작성으로 표시했다. 빈 문서나 존재하지 않는 기능을 만들어 링크를 채우지 않았다. 코드 블록의 파일명 예시는 링크 검사에서 제외한다. 샘플 에셋 README의 상용명·역할 분담 주석도 실제 데이터/로딩 설명으로 바꿨다.

최종 정적 확인: README/AGENTS/docs 29개 UTF-8 읽기와 실제 로컬 링크 검사에서 깨진 링크 0건, `git diff --check` 통과. 자체 문서·코드·샘플/도구의 상용 게임명 검색 결과 0건. 사용자 도구 두 파일은 `node --check`로 구문만 확인하고 에셋을 재생성하지 않았다. 저장소 전역/제3자 코드의 명명·재포맷은 하지 않았다.
