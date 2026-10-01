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

## build 정리 조사 (2026-10-01)

요청은 구 빌드 파일 정리에 사용할 스킬 탐색이다. [스킬 비교](15-skills-and-agents.md)의 원문을 검토하고 현재 실행 경로·CMake cache·추적 파일의 경로 참조·빌드 내부의 별도 소스/데이터를 읽기 전용으로 확인했다. 파일 삭제·이동·추가 스킬 설치는 수행하지 않았다.

크기는 junction/symlink를 따라가지 않고 일반 파일의 길이를 합산한 논리 크기다. 디스크 할당량·실제 회수 가능한 공간과 같다고 보장하지 않는다. 조사 시 재분석점을 발견하지 않았고 Git 추적 대상 `build/` 파일은 0개였다.

| 분류 | 실측 | 근거·처리 기준 |
|---|---|---|
| build 전체 | 31,235,064,416 bytes, 약 29.09GiB | 날짜만으로 사용 여부를 판단하지 않음 |
| 현재 `build/dev` | 2,272,315,768 bytes, 약 2.12GiB | README·AGENTS·프로젝트 `.mcp.json`·package/verify 도구의 사용 경로. Debug/Release MyEditor.exe 모두 존재하므로 유지 |
| 구 CMake 트리 57개 | 28,770,158,400 bytes, 약 26.80GiB | 모두 MyEngine을 source로 하는 VS 18 2026 cache가 있음. 아래 범위의 추적 파일에서 직접 경로 참조는 없었지만 비추적 smoke 프로젝트의 의존성은 발견됨. 정리 후보이며 전부 불필요하다는 판정은 아님 |
| `build/foundation`, `build/docs-audit` | 각각 63,363,823 / 2,486 bytes | 이전 수정 patch·검증 로그·벤치마크·서버 재현 데이터. 특히 foundation/preexisting.patch와 문서의 재현 경로를 보존 |
| 기타 디렉터리·루트 파일 | 루트 29개 파일 87,523,729 bytes 등 | 캡처·보조 스크립트·서버 저장 데이터·독립 smoke 소스가 섞임. 생성 산출물과 별도 분류 |

구 CMake 트리 후보 목록:

```text
finish, hangfix, impl-diag, impl-events, impl-math, impl-platform, impl-rhi, scaffold
m1, m1-asset, m1-input, m1-review, m1b, m1b-imgui, m1b-render
m2-review, m2a, m2a-async, m2a-ecs, m2a-jobs, m2b, m2b-phys, m2b-tilemap, m2c, m2c-gltf, m2c-render
m3-review, m3a, m3a-ase, m3a-hot, m3a-refl, m3b, m3b-anim, m3b-audio, m3c, m3c-bind, m3c-runtime
m4-review, m4a, m4a-inspect, m4a-shell, m4b, m4b-panels, m4b-viewport
m5-review, m5a, m5a-text, m5a-ui, m5b, m5b-nav, m5b-tools
m6-review, m6a, m6a-dialogue, m6a-world, m6b, m6b-npc
```

참조 조사는 Git 추적 UTF-8 텍스트에서 위 경로의 `build/이름`·`build\이름` 표기를 검색했으며 third_party·assets·samples/assets와 바이너리는 제외했다. 프로젝트 MCP는 `MYE_BUILD_DIR=build/dev`지만 MCP 구현 자체의 환경변수 없는 기본값은 `build`다. 새로운 정리 규칙에서 기본값과 프로젝트 설정을 혼동하면 안 된다. 조사 당시 build 하위 exe 경로를 가진 실행 프로세스는 발견되지 않았다. 외부 IDE/바로가기·다른 사용자의 참조까지 확인한 것은 아니다.

보존 검토가 필요한 실제 사례:

- `build/m3b_smoke/CMakeLists.txt`와 `smoke.cpp`는 독립 소스다. CMakeLists가 `build/m3b`의 Debug 라이브러리를 직접 참조하므로 m3b 산출물을 지우면 기존 smoke 구성은 다시 사용할 수 없다. 소스 보관과 현행 빌드 연결/대체 여부를 먼저 정한다.
- `build/m4b/testproj`에는 `.myeditor/session.json`과 `assets/chars/hero.png`가 있다. CMake 생성 파일로 취급하지 않는다.
- `build/m6b/final_verify.ps1`은 `build/m6b/dumps`를 읽는다. 스크립트와 필요한 캡처를 함께 보관할지 판단한다. 구 트리에서 별도 확인할 소스·스크립트·JSON·이미지 후보를 74개 발견했으며 전체 파일 유형의 보존 판정을 완료한 것은 아니다.
- `_netdemo`, `_nete2e`, `_nete2e2`에는 계정·캐릭터·원장·백업 JSON이 있다. 폴더 이름이나 오래된 날짜만으로 폐기하지 않는다.

정리 순서는 현재 dev 유지 → 필요한 비생성 소스·데이터·증거 보관 → 확인된 구 산출물만 경로별 정리 → dev 빌드/CTest 확인이다. 재귀 삭제/이동 전 실제 절대 경로가 MyEngine/build 내부인지와 재분석점 여부를 다시 검사한다. `build/*` 일괄 삭제·CMake cache가 있는 디렉터리의 무조건 삭제·현재 dev의 clean은 이번 조사에 포함하지 않았다.

이번 변경은 조사 문서뿐이다. UTF-8 읽기·문서 링크·diff 형식만 확인하고 앱 빌드/CTest를 새로 실행하지 않았다. 이전 실행 결과를 정리 후 검증 결과로 재사용하지 않는다.

## 제품 기준 문서 정리 (2026-10-01)

작업 기준은 직전 main 커밋 bb5b3f4이며 시작 시 작업 트리는 깨끗했다. 사용자의 요청에 따라 개발 순서로 기능을 구분하던 표·명칭을 제품의 현재 책임·사용법·검증 기준으로 바꿨다. 구현되지 않은 앱 연결을 완료 상태로 재분류하지 않았다.

| 변경 | 근거·판단 |
|---|---|
| README·문서 안내·시스템 문서·코드 탐색 지도 | 과거 완료표와 서로 다른 시점의 기능 표가 현재 앱을 설명하지 못했다. CMake 타깃·공개 헤더·앱 등록·최종 소비자를 기준으로 다시 작성하고 중복 진행 이력을 제거했다. 기존 파일 경로는 링크 호환을 위해 유지한다. |
| MMO 문서 | 현재 앱의 실행 범위와 서비스 확장 요구를 구분했다. 단계 코드 대신 기능 이름·의존 기능·검증 기준을 남겼다. 서버 64명·루프백·JSON 저장을 대규모 운영 보장으로 확대하지 않는다. |
| 제작 도구 문서 | 실제 MCP 8개 도구·설치 스킬·기존 임포트 경로를 기준으로 설명했다. 미구현 engine/ai·생성 게이트웨이의 필수 도입안과 확인되지 않은 제공자 모델·가격·API 표를 제거했다. 기존 도구로 수행하는 작업에 새 엔진 모듈을 요구할 사용처·측정 근거가 없다. 추가 조건은 해당 문서에 기록했다. |
| 앱·샘플·CMake·MCP 안내 | 사용자 창/패널 제목·검증 로그·진단 문구를 기능 이름과 실제 지원 범위로 바꿨다. CMake 명령·타깃·링크 의존성과 CLI 옵션은 변경하지 않았다. 샘플 검증 로그의 접두사는 CHARACTER-VERIFY/VILLAGE-VERIFY로 바뀌며 필드는 유지한다. 추적 소스·도구·CTest에서 기존 접두사를 파싱하는 소비자는 없었다. 외부/비추적 검사 스크립트는 새 접두사로 맞춰야 한다. |

### 소스 대조로 바로잡은 설명

- ECS는 EnTT 래퍼가 아니라 자체 sparse-set이다. 과거 설계 선택을 현재 구현으로 나열하던 설명을 교체했다.
- 깊이 정규화는 sortKeyY - viewBottomY다. [DepthEncoder.cpp](../engine/render/src/DepthEncoder.cpp)의 기존 구현·바이어스·밴드 경계에 맞춰 [렌더 계약](02-rendering.md)과 헤더 주석을 정정했다. 좌표·PPU·밴드·알고리즘은 변경하지 않았다. 씬/렌더 층 변환의 상한 차이도 기록했다.
- [DX11 구현](../engine/rhi/src/dx11/Dx11Device.cpp)의 WriteTimestamp는 빈 본문, ResolveTimestamps는 false를 반환한다. 이전 구조 문서의 GPU 타임스탬프 지원 설명을 stub으로 정정했다. DrawIndexedInstanced 호출 구현과 인스턴스 슬롯/StructuredBuffer 미지원도 구분했다.
- village_demo의 BuildWorld는 지도·스폰을 C++로 구성하고 BuildRuntimeAndScripts는 로컬라이즈 JSON·Lua를 시작 시 읽는다. JSON 지도 자동 반영·전체 자동 핫 리로드 주장은 제거했다. character_demo의 명시적 재임포트 검증과 구분한다.
- [PluginHost](../engine/plugin/include/mye/plugin/PluginHost.h)는 LoadDll을 제공한다. DLL 로더를 후속 기능으로 설명하던 문서·주석을 수정하고 DLL 정적 링크 시 타입 레지스트리 공유 한계를 남겼다.

### 검증

- 기존 build/dev의 Debug·Release 빌드 성공, CTest 각각 13/13. mye_tests의 실제 출력은 각각 511/511 passed였다. 샘플 테스트에서 새 CHARACTER-VERIFY/VILLAGE-VERIFY 로그도 확인했다.
- tools/mcp에서 npm.cmd run build와 npm.cmd run smoke 성공. 프로토콜 초기화·8개 도구·상태/로그·잘못된 실행 경로 안내는 ALL PASS였다.
- UTF-8 읽기·실제 로컬 문서 링크·제품 문서/CMake 설명의 개발 단계 표기·git diff --check를 검사했다. 수정 C++는 주석·표시 문자열 외 토큰이 같고, 수정 CMake는 주석 외 명령·타깃·의존성이 같음을 별도로 대조했다.
- 빌드/CTest 로그는 build/docs-audit/product-debug-build.log, product-debug-ctest.log, product-release-build.log, product-release-ctest.log에 남겼다. 생성 산출물을 Git에 포함하지 않는다.

변경은 문서·주석·표시 문자열이다. 새 기능·추상화·패키지를 추가하지 않았고 사용자 에셋·저장 데이터·구 build 폴더를 삭제하지 않았다. 이전 정리 조사에 필요한 실제 구 디렉터리명·경로와 기존 테스트 산출물 이름은 보존한다. 기존 C4996 경고는 남는다.

에디터 실제 파일 열기, MyGame의 전체 온라인 콘텐츠, 보호되는 전송·신뢰 세션, GPU 타임스탬프·일반 readback은 여전히 개선 항목이다. 이번 검증은 전체 마우스/DPI 워크플로·청취·운영 MMO 부하 검증을 새로 수행한 것이 아니다. 상세 완료 조건은 [개발 우선순위](14-development-priorities.md)를 따른다.

## 소스·빌드 경로 정리 (2026-10-01)

작업 기준은 main d965f98이며 시작 시 추적 파일의 변경은 없었다. 사용자가 후속으로 소스·빌드 경로 정리를 요청하여 앞선 읽기 전용 조사에서 확인한 구 산출물을 실제로 정리했다. 기존 `file-organizer`·`cmake` 스킬을 읽고 적용했으며 추가 패키지나 자동 정리 작업은 설치하지 않았다.

### 경로 선택과 사용처 근거

| 변경 | 근거·선택 이유 |
|---|---|
| 현재 소스 경로 유지 | engine은 공유 기반, game은 게임 규칙, apps는 실행 조합, samples·tests는 검증이라는 기존 책임 경계가 있다. 소스 이동으로 공개 include·CMake 의존성을 다시 엮을 필요가 없어 폴더 이름이나 파일 크기만으로 재배치하지 않았다. |
| build/dev에 구성·빌드·테스트 통일 | README·AGENTS·프로젝트 MCP 설정·tools/package/package.ps1·tools/verify-foundation.ps1이 이미 같은 트리를 사용한다. CMakePresets.json으로 기존 VS 18 2026 x64 트리를 구성하며 Debug·Release는 같은 트리의 구성이다. 별도 빌드 래퍼를 만들지 않았다. VS 2022 수동 구성 경로도 README에 남겼다. |
| MCP 기본값 수정 | 프로젝트 설정은 build/dev지만 resolveBuildDirRel의 환경변수 없는 기본값은 build였다. 모든 호출자는 index.ts에서 만든 문맥을 build/run/capture/test/status에 전달하므로 공통 경계인 root.ts에서 기본값을 고쳤다. 사용자 지정 경로와 루트 밖 접근 거부는 유지한다. |
| 구 소스·데이터·증거를 archive로 분리 | 구 빌드에는 독립 CMake 소스·스크립트·에셋·에디터 설정·계정/캐릭터/원장 JSON·캡처가 함께 있었다. 활성 빌드로 보이는 폴더를 남기지 않으면서 보존하기 위해 원래 상대 경로를 유지한 로컬 archive/build-cleanup-2026-10-01로 옮겼다. 사용자 데이터가 있어 archive는 Git에서 제외한다. |
| CMakeUserPresets.json을 Git에서 제외 | 공용 CMakePresets.json과 개인 환경 설정을 구분한다. 별도 사용자 preset 파일은 만들지 않았다. |

추적 UTF-8 텍스트의 구 빌드 경로를 다시 검색했으며 직접 참조는 이 문서의 과거 조사 기록뿐이었다. build 하위 실행 파일을 사용하는 프로세스와 cmake/MSBuild/cl/link/ctest 실행이 없는지도 정리 직전에 확인했다. 외부 IDE·사용자 바로가기의 참조는 확인하지 못했다.

### 분류·보존·삭제 결과

파일 크기 합계는 논리 크기이며 실제 디스크 회수량과 같다고 보장하지 않는다. 구 트리 57개 모두 CMakeCache.txt의 source가 E:/MyEngine, project가 MyEngine임을 확인했다.

| 처리 | 실측 결과 | 이유·다시 필요한 조건 |
|---|---:|---|
| 구 트리의 컴파일 산출물 삭제 | 24,709개 / 28,592,874,527 bytes, 약 26.63GiB | 구 트리의 구성별 출력 폴더·CMakeFiles 안에서 obj/lib/pdb/ilk/exe/dll/exp/res/tlog/idb/ipdb/iobj/pch/lastbuildstate/recipe만 분류했다. 현재 앱은 build/dev를 사용한다. 과거 바이너리가 필요하면 해당 소스 revision·도구 환경으로 새 트리에 재빌드해야 하며 현재 소스의 출력과 같다는 보장은 없다. |
| 구 트리에서 판단이 불확실한 파일까지 보관 | 9,269개 / 177,283,873 bytes | 독립 소스·설정·데이터·로그·캡처뿐 아니라 작은 생성 메타데이터도 보관했다. 모든 보관 복사의 SHA-256이 원본과 일치한 뒤에 구 트리를 제거했다. |
| 독립 폴더·루트 파일 그대로 이동 | 147개 / 136,769,491 bytes | m3b_smoke, _assetview, _mcpdrive, _netdemo, _nete2e, _nete2e2, _vis, build 루트 파일 29개와 저장소 루트 파일 5개를 보존했다. 계정·원장 데이터는 내용 출력이나 Git 추가를 하지 않았다. |
| 최종 보관본 확인 | 9,416개 / 314,053,364 bytes | 모든 보관 파일의 크기·SHA-256 대조 통과. 수정 시각(UTC) 대조도 불일치 0개다. manifest.json에 원래 경로·보관 경로·크기·시각·해시를 기록했다. |

build에는 dev·foundation·docs-audit만 남겼다. dev의 Debug·Release 실행 파일, foundation의 기존 변경 patch·재현 자료·서버 실험 데이터, docs-audit의 검증 기록은 유지했다. engine/game/apps/samples/tests/assets/third_party의 소스·에셋은 정리 대상으로 삼지 않았다. 저장소 루트의 기존 BMP 4개와 vc140.pdb도 삭제하지 않고 보관했다.

복사·이동·재귀 삭제는 PowerShell 기본 명령으로 수행했다. 실제 절대 경로가 허용한 저장소/build/archive 내부인지 확인하고 재분석 지점을 거부했다. 삭제 직전에 전체 파일 목록·크기·수정 시각을 다시 대조하여 조사 이후 변경된 트리를 지우지 않도록 했다. 분류 목록·실행 스크립트·로그는 로컬 build/docs-audit/cleanup-inventory.json, cleanup-plan.json, cleanup-references.json, cleanup-build.ps1, cleanup-build.log에 있다.

### 보관 자료 재사용 제약

보관한 CMake cache·Visual Studio 프로젝트의 절대 경로는 과거 기록이다. 보관 폴더를 이동한 빌드 트리로 실행하지 않는다. 필요한 소스·데이터만 manifest로 식별하여 별도 위치에 복사하고 새 빌드를 구성한다.

독립 m3b_smoke는 구 build/m3b 라이브러리를 직접 참조하므로 소스 보관을 현재 실행 가능 상태로 표시하지 않았다. 현행 CTest를 기본 검증으로 사용하며 이 소스가 다시 필요하면 현재 CMake 타깃에 연결하고 별도로 검증한다. m6b/final_verify.ps1의 구 캡처 경로도 보존된 과거 관계이며 재사용 전에 수정해야 한다. 서버 데이터는 스키마·버전·복구 절차를 확인한 뒤 격리된 실험 경로에서만 읽는다.

### 정리 후 검증

- cmake --list-presets=all과 cmake --preset dev 통과. 기존 build/dev 트리를 재사용했다.
- Debug·Release 빌드와 ctest --preset debug/release 각각 통과: CTest 13/13, mye_tests 출력 511/511 passed. 기존 MSVC C4996 경고는 남으며 이번 정리에서 외부 코드나 무관한 경고 수정을 하지 않았다.
- tools/mcp에서 npm.cmd run build·npm.cmd run smoke 통과. 기존 smoke에 기본 build/dev와 MYE_BUILD_DIR override 검사를 추가했고 총 10개 검사 ALL PASS다. 자식 서버는 build/_smoke_mcp_none을 사용하여 실제 빌드를 실행하지 않았다.
- 보관본 전체의 크기·해시와 수정 시각 확인 통과. 정리 자체는 GPU·오디오·앱 통합 기능 변경이나 운영 성능 개선을 의미하지 않는다.
- README·AGENTS·docs 29개의 UTF-8 읽기와 실제 로컬 링크 556개 확인: 누락 0개. git diff --check 통과. archive와 CMakeUserPresets.json의 Git 제외도 확인했다.

빌드·테스트 로그는 build/docs-audit/cleanup-configure.log, cleanup-debug-build.log, cleanup-debug-ctest.log, cleanup-release-build.log, cleanup-release-ctest.log에 남겼다. 보관본과 검증 산출물은 Git에 포함하지 않는다.

## 2026-10-01 — MyEditor 프로젝트·씬 파일 흐름

기준 소스는 `2939a46`이다. 요구는 에디터에서 프로젝트 만들기·불러오기·저장하기를 실제로 연결하고 이후 기능은 Godot를 벤치마킹하는 것이다. 문서의 예정 기능을 완료로 표시하는 대신 메뉴→파일 경계→문서 World→씬 직렬화→재열기→Play 소비 경로를 확인했다.

### 발견한 문제와 선택 근거

| 재현·소스 근거 | 변경 | 이유·경계 |
|---|---|---|
| ProjectContext::Open은 경로만 기록하고 OpenScene도 실제 LoadFromFile을 호출하지 않았다. | 버전 1 `.myeproj`와 기본 씬 생성, 파일/폴더 열기, 시작 씬 후보 로드 후 프로젝트 교체 | 기존 SceneSerializer와 JSON을 재사용한다. 실패한 프로젝트 로드가 현재 편집 문서를 잃게 하지 않는다. 레거시 assets 폴더는 열기만으로 수정하지 않는다. |
| Document는 Undo만 소유하고 EditorModule이 모든 문서에 SceneModule::World를 배선했다. | 문서별 World·월드 EventBus·Undo 스택, 활성 문서와 패널/렌더/명령 컨텍스트 함께 전환 | 탭별 씬이 실제로 분리된다. World를 버스보다 먼저 파괴하며 이전 월드의 선택·선택 이력을 비운다. |
| SaveActive는 이름 없는 씬의 임의 기본 경로를 사용했다. 프로젝트 선택 UI가 없었다. | 파일 메뉴·툴바·단축키, 새 프로젝트 폼, Windows IFileDialog 파일/폴더 선택, Save As, 문서 탭 | 현재 ImGui와 운영체제 대화상자를 사용한다. 새 UI 라이브러리·파일 선택 프레임워크를 추가하지 않는다. COM 수명은 해당 호출 범위에서 관리한다. |
| SceneSerializer의 저장은 대상 파일을 먼저 truncate했다. persist 내부에 별도 JSON 원자적 쓰기 경로가 있었다. | persist의 공용 경계를 core/JsonFile로 옮기고 씬·프로젝트·세션 저장에서 재사용 | engine/scene이나 editor가 persist를 역참조하지 않는다. UTF-8 경로·64 MiB 제한·flush 후 교체를 한 경계에서 관리한다. CREATE_NEW로 기존 staging 파일의 내용을 보호한다. |
| SceneSerializer는 미등록 컴포넌트와 직렬화 오류를 건너뛰어 이후 저장에서 내용이 사라질 수 있었다. | 버전·레코드·ID·부모·순환 계층·컴포넌트 구조 사전 검사, 직렬화/역직렬화 실패 전파 | 지원하지 않는 데이터를 성공한 빈 씬처럼 저장하지 않는다. ProjectContext는 별도 후보 World를 읽는다. ReadInto 자체를 모든 기존 호출자에 대한 트랜잭션으로 바꾸지는 않았다. |
| Parent는 리플렉션 열거와 별도로 기록되며 PlayWorld의 동적 Parent 풀이 없을 수 있었다. | 검증한 부모 연결을 World::Add<Parent>로 복원 | 기존 네이티브 자동 등록 경로를 재사용하여 Play 왕복에서 부모 관계가 유실되지 않게 한다. |
| PlayModeController::Play는 Restore 실패를 버리고 Playing으로 진행했다. | Play를 Expected로 반환하고 후보 World 복원 성공 후 상태 변경; 모든 호출자 갱신 | 잘못된 계층의 Play 진입을 거부하며 편집 World와 Edit 상태를 보존한다. 임시 이벤트 버스도 World 뒤에 파괴한다. |
| 시작 씬 Save As 후 문서 dirty만 해제하면 프로젝트의 이전 mainScene 경로로 재실행할 수 있었다. | 메타데이터 dirty 별도 추적, 성공한 프로젝트 저장에서만 해제 | 창 닫기/프로젝트 전환에서 씬 내용뿐 아니라 시작 경로 변경도 저장·버리기·취소로 처리한다. |
| 같은 Windows 파일의 대소문자 별칭이 문자열 비교에서 서로 다른 문서로 취급될 수 있었다. | filesystem::equivalent로 이미 열린 파일 확인 | 중복 문서와 다른 문서의 저장 대상 덮어쓰기를 공통 파일 경계에서 막는다. |
| CLI `.myeproj`를 코어 projectDir로 전달하면 `file.myeproj/config/project.json`을 조회했다. | 앱의 프로젝트 인자 해석을 한 함수로 모으고 코어에는 폴더, EditorModule에는 선택한 프로젝트 경로 전달 | 설정 경로는 디렉터리 계약을 지키며 여러 프로젝트 파일 중 직접 선택한 파일도 유지한다. 공용 코어에 에디터 파일 형식을 추가하지 않았다. |
| 레이아웃 저장 실패와 콘텐츠 저장 성공이 하나의 결과로 섞일 수 있었다. | 콘텐츠 저장 결과를 반환하고 이후 로컬 레이아웃 실패는 콘솔 경고로 명시 | 이미 저장된 씬·프로젝트를 저장 실패로 잘못 안내하지 않는다. 프로젝트 전환 전 레이아웃 오류와 종료 시 오류도 별도로 처리한다. |

주요 구현은 `engine/editor/src/Project.cpp`, `EditorApp.cpp`, `EditorFileActions.cpp`, `EditorModule.cpp`, `PlayMode.cpp`, `engine/scene/src/scene/SceneSerializer.cpp`, `engine/core/src/JsonFile.cpp`, `apps/editor/src/main.cpp`다. AssetBrowser의 파일 경로·파일명도 같은 UTF-8 변환을 사용한다. 새 파일 EditorFileActions는 네이티브 대화상자 수명과 파일 작업 UI를 맡으며 새 서비스·인터페이스·팩토리를 만들지 않았다. CMake는 core와 editor의 실제 소스 및 PRIVATE Windows COM 링크만 추가했다.

persist의 중복 ReadJsonFile/WriteJsonFile 구현은 호출자를 확인한 뒤 공용 함수 별칭으로 대체했다. 기존 계정·캐릭터·원장 스키마와 호출 인터페이스는 유지한다. 이 과정에서 Windows 헤더의 전이 include가 사라져 AccountStore의 bcrypt 빌드가 실패했다. 실제 플랫폼 소비자인 AccountStore에 Windows.h를 명시하여 해결했다. PlayMode의 Error/Expected include 이름을 확인하지 않고 추가한 빌드 실패도 기존 Base.h 정의를 확인하여 수정했다. 제거한 EditorModule의 미사용 sceneModule 필드·include·편집 World 연결은 새 문서 소유권과 충돌하므로 삭제했다. 다시 앱 공용 월드가 필요하면 문서와 별도 런타임 수명을 명시하여 연결해야 한다.

### Godot 참고와 적용 범위

확인일 2026-10-01. [Project Manager](https://docs.godotengine.org/en/stable/tutorials/editor/project_manager.html)에서 이름·빈 폴더 생성과 파일/폴더 import, [File paths](https://docs.godotengine.org/en/stable/tutorials/io/data_paths.html)에서 프로젝트 루트·로컬 데이터 분리, [Nodes and Scenes](https://docs.godotengine.org/en/stable/getting_started/step_by_step/nodes_and_scenes.html)에서 씬 문서·저장·프로젝트 내부 Save As, [Project organization](https://docs.godotengine.org/en/stable/tutorials/best_practices/project_organization.html)에서 관련 씬·에셋의 프로젝트 내부 배치를 참고했다. 채택·차이는 docs/07에 표로 기록했다. 단축키는 MyEditor의 기존 입력과 충돌을 피하도록 정했으며 조회가 실패한 Godot 단축키 문서를 근거로 삼지 않았다.

MyEngine의 ECS·C++20·DX11·GUID/VFS·좌표/깊이 계약을 유지한다. Godot 코드는 복사하지 않았다. 별도 런처·렌더러 선택·새 res:// 해석기는 현재 요구나 구현된 두 번째 백엔드가 없어 추가하지 않았다. 최근 목록·문서 재개와 크래시 복구는 사용 이력·저장 정책·장애 검증을 갖춘 후속 항목이다. 게임 시작 씬과 현재 씬 실행 구분은 에셋·게임 플레이 시스템 연결 후 검증한다. AGENTS.md에 이후 기능도 공식 자료·실제 호출 경로·차이·보류 이유를 기록하도록 반영했다.

### 검증

기존 자체 테스트 프레임워크의 EditorWorkflowTests에 3개 회귀 시나리오를 추가했다. 실제 EditorApp 파일 작업 API와 프로젝트/문서/명령/직렬화/Play 경계를 호출하며 사용자 데이터 대신 `build/dev/test-data/editor-projects`의 새 경로를 쓴다.

- 한글 이름·폴더 생성, 엔티티·부모 관계 편집, 전체 저장·프로젝트 재열기·Play/Stop 왕복과 편집 월드 보존.
- 이름 없는 문서의 저장 거부, 문서별 World·Undo·선택 이력 분리, 정규 경로 및 Windows 대소문자 파일 별칭 중복 방어, 프로젝트 외부 Save As 거부.
- 기존 staging 디렉터리·파일에 따른 씬 저장 실패에서 기존 파일·문서 경로·dirty와 임시 데이터 보존. 프로젝트 메타데이터 저장 실패에서 이전 mainScene·미저장 상태 보존. 로컬 레이아웃 실패는 콘텐츠 저장 성공과 구분.
- 손상 JSON, 미래 버전, 알 수 없는 컴포넌트, 잘못된 컴포넌트 구조, 중복 ID, 없는 부모·순환 계층에서 현재 문서 보존. Play 복원 실패는 Edit 유지.

Debug/Release 전체 빌드와 양쪽 CTest 13/13, 내부 검사 514/514 통과했다. 빌드·테스트 로그는 `build/docs-audit/editor-project-{debug,release}-{build,ctest}.log`, 상세 내부 결과는 `editor-project-{debug,release}-details.log`에 보관한다. 기존 sol2 C5321·테스트의 C4996 경고는 범위 밖으로 유지했다. MCP 소스는 이번 변경에서 수정하지 않았으며 직전 경로 정리 검증과 구분한다.

실제 Release MyEditor를 한글 `.myeproj` 경로로 실행하여 재열린 renamed.scene·Hierarchy·프로젝트 이름·메뉴/툴바를 1920×1080, DPI 1.00에서 캡처하고 정상 종료(exit 0)했다. 로그에서 코어 projectDir가 프로젝트 폴더로 지정되는 것도 확인했다. `docs/images/editor-project.png`는 실제 BMP 캡처를 PNG로 변환한 자료다. 한글 문서의 부모 엔티티는 접힌 상태로 표시된다. 에디터의 스프라이트 텍스처 해석기는 아직 연결되지 않았으므로 이 화면을 완성된 게임 장면 렌더로 주장하지 않는다.

숨긴 창에 Win32 마우스 메시지를 보낸 대화상자 자동화는 네이티브 파일 창을 열지 못했다. 이 시도는 통과가 아니며 실제 파일 선택/취소·미저장 메시지 버튼 조작·여러 DPI·완전한 마우스/키보드 경로는 추가 검증이 필요하다. `build/docs-audit/editor-native-dialog-check.ps1/.log`와 `editor-project-native-dialog.log`에 실패한 시도를 남겼다. 위 회귀 테스트를 네이티브 UI 조작 완료로 확대하지 않는다.

기존 imgui-ui-ux-engineering·cpp-coding-standards 지침을 읽고 적용했으며 새 패키지는 설치하지 않았다. 스킬 quality suite의 구조/의미 참조 검사는 30/30 통과하고 공식 v1.92.0/master 조회도 성공했으나 g++/c++만 찾는 컴파일 게이트는 MSVC 환경에서 실패했다. 제품은 현재 vendored ImGui와 MSVC로 실제 빌드했다. 별도 정적 검사에서 EditorApp/AssetBrowser는 통과했지만 EditorFileActions의 BeginPopupModal은 존재하지 않는 EndPopupModal을 요구하여 실패했다. 실제 vendored imgui.h의 854~858행 계약은 BeginPopupModal→EndPopup이며 구현은 이를 따른다. 검사 결과를 전체 PASS로 표시하거나 올바른 API를 검사기에 맞춰 바꾸지 않았다. 결과는 `editor-project-imgui-quality.log`, `editor-project-imgui-static.log`에 있다.

### 남은 한계

파일별 저장은 원자적 교체를 사용하지만 프로젝트의 여러 씬·메타데이터 전체는 단일 트랜잭션이 아니다. 뒤 파일 실패 전에 앞 파일이 저장될 수 있다. 한 파일에 한 작성자, 64 MiB 상한이며 기존 tmp는 자동 복구·삭제하지 않는다. 실제 정전·디스크 장애·동시 에디터 쓰기를 검증하지 않았다. 최근 목록·자동 저장·문서 탭 재개/닫기 UX, 게임 전용 컴포넌트 등록, 에셋 해석, 물리·Lua/runtime의 동일 Play 조합은 후속 작업이다. 이 변경을 온라인 게임이나 엔진 전체 완성으로 표시하지 않는다.

마지막 문서 검사는 README·AGENTS·docs 29개의 UTF-8 읽기와 코드 블록을 제외한 로컬 링크 562개에서 누락 0개였다. 처음 링크 정규식이 C++ lambda의 `[&](float dt)`를 링크로 오인한 결과는 코드 블록 제외 후 다시 확인했다. git diff --check도 통과했다. SceneModule 등록은 CommandBuffer reparent hook의 공용 초기화가 존재하므로 유지하고, 그 별도 World를 문서 편집 World로 설명하지 않도록 구조 문서에 명시했다.


## 2026-10-01 — 초원마을 기본 콘텐츠와 애니메이션 제작 흐름

### 확인한 결함과 수정 경계

실제 경로를 apps/editor → EditorModule/EditorApp → Project/Document → SceneSerializer → RenderExtract/HybridRenderer로 추적했다. 기존 SpriteSheetImporter·ClipPlayback·SpriteAnimator·AnimEditing·AssetMeta·VFS·AssetManager·Progression의 정의와 호출자를 먼저 확인했다.

| 확인 근거 | 변경 | 선택 이유 |
|---|---|---|
| 에디터 HybridRenderer에 texture resolver가 없고 ScanDirectory는 루트만 기록 | 기존 DB의 .meta 스캔, VFS/PNG importer, retained TextureHandle과 resolver 연결 | 씬 GUID를 실제 PNG 소비자까지 연결. 별도 앱 식별자/로더를 만들지 않음 |
| LoadSync/Async가 .meta와 다른 임시 GUID 생성 | 메타 식별자 읽기, CachedGuid 재사용, 0/중복 GUID 거부 | DB와 핸들 식별자가 일치하고 재실행/스캔에서 참조 유지 |
| 전역 static AnimEditSession, 씬 Undo에 에셋 작업 저장, 실제 파일 열기/저장·PNG 미리보기 없음 | 프로젝트 소유 애니메이션 Document와 독립 CommandStack, 기존 편집 헬퍼·ClipPlayback 사용 | 수명·dirty·Undo 경계를 일치시키고 중복 프리뷰 시간 계산 삭제 |
| .anim 파일 계약이 없고 런타임 포인터만 사용 | AnimationAsset 값 타입의 version 1 JSON, persistent animation GUID와 런타임 참조 분리 | 기존 SpriteSheet/Clip 재사용. 이미지 인코더·범용 직렬화 프레임워크 추가 없음 |
| 배율을 준 첫 실제 캡처에서 배경이 잘리고 캐릭터가 거대함 | SceneSerializer의 WorldTransform/Children 복구, SpriteCorners를 렌더/선택에서 공유 | LocalTransform 직렬화 후 파생 데이터가 없고 렌더가 scale/rotation을 무시한 공통 원인을 수정. 파일/Play/프리팹 경계에 적용 |
| Inspector 등록 타입에 편집 필드 정보가 없고 Undo 뒤 dirty 갱신 누락 | 기존 flat 씬 포맷을 유지하면서 필드 메타 추가, Transform 쓰기 후 dirty 갱신 | 파일 호환성과 Inspector/기즈모/Undo의 실제 행렬 갱신을 함께 유지 |
| 문자만 Lv1이라 표시하면 실제 상태를 검증할 수 없음 | 기존 gameplay::Progression level=1/xp=0 등록·저장·Inspector·Play 복제 | 콘텐츠 정보와 실제 ECS 상태 일치, 새 게임 고유 엔진 컴포넌트 불필요 |
| Play Tick이 렌더 단계, StepFrame은 no-op | 코어 FixedUpdate에 기존 애니메이션/Transform 연결, Step 요청 한 번 소비 | 프레임률에 의존하는 시뮬레이션 방지. 물리/Lua 연결 완료로 확대하지 않음 |
| 셸 캡처의 마을이 headless보다 밝음 | MyEditor swapchain을 BGRA8Unorm으로 정합 | 기존 PNG·RT의 UNORM 색을 ImGui 출력에서 sRGB로 다시 인코딩하지 않음 |

Animation 문서는 assets/ 내부만 열기/저장하도록 공통 파일 경계에서 검사한다. 프로젝트 루트 안이더라도 assets/ 밖이면 GUID 스캔·씬 지정이 불가능하기 때문이다. 다른 열린 문서의 파일 덮어쓰기는 거부하고, 새 경로의 기존 .anim 덮어쓰기는 패널에서 확인한다. 원자적 JSON 교체 실패는 원본·경로·dirty를 보존한다. 새 문서를 이름 없이 프로젝트 저장 성공으로 처리하지 않는다.

런타임 Texture/Animation 자원은 에디터 모듈이 소유하고 World에는 비소유 참조만 연결한다. 새 프로젝트·새로 고침에서 재바인딩하며, 종료는 문서/UI→렌더→에셋 핸들/매니저→디바이스 순서다. 게임 콘텐츠는 game/starter/, 실행 조합은 apps/editor/, 공유 데이터·동작은 engine/에 둔다. 새 패키지·병렬 에이전트·임의 이미지 라이브러리는 추가하지 않았다.

### 기본 콘텐츠 제작과 보존

내장 imagegen으로 원본 배경과 투명 캐릭터 시트 두 장을 제작했다. 타사 게임/캐릭터/로고/외부 아트 팩을 입력하거나 복사하지 않았다. imagegen을 사용한 결과를 수작업 원본이나 Aseprite 도구 실행으로 표시하지 않는다. 사용 프롬프트는 아래에 보관한다.

- 배경: 1672×941 RGB, 초원·3개 집·우물·꽃·울타리·개울·다리. SHA256 `F233B9CD40E81AA5FAAA40A9AAFEAC24518DFF5DA723DC76E21A18F385C02F18`.
- 캐릭터: 2170×725 RGBA, 정면 초보 모험가 8개 포즈. SHA256 `C8CAF294F9A78B95ECDAD76FE4EA634CE74388B81974B5AA954572B52C6C71F6`.
- 요청한 균등 격자/베이스라인은 생성 결과와 달랐다. 원본 PNG는 그대로 복사하고 alpha≥128 실루엣의 프레임 영역과 2px 여유·발밑 피벗을 읽어 .anim에 기록했다. Python/Pillow는 알파 범위·캡처 픽셀 차이 분석에만 썼으며 이미지를 편집/리샘플링하지 않았다.
- idle은 시트 0~3, 0.30초씩; walk는 4~7, 0.14초씩. walk 타임라인 0/2의 footstep(grass, 1) 마커는 기존 이벤트 계약을 사용하며 사운드 연결은 포함하지 않는다.
- 배경 GUID `13fa9057-03a9-4586-bff7-4bf66394bd47`, 캐릭터 GUID `014fc10e-782f-43da-9641-6fa0d79a4b05`; idle/walk GUID는 각각 `1496e3ee-ab5b-4215-8ddd-cfdbcd36a46d`, `6cb65e2a-ab6a-441f-93ab-61fff3b981c6`.

기본 프로젝트의 원본은 game/starter/meadow_village이다. 빌드는 실행 파일 옆 templates/meadow_village에 복사하고, 프로젝트 인자 없는 첫 실행은 코어 userDir/projects/MeadowVillage로 한 번 복사한다. 기존 프로젝트가 있으면 다시 덮어쓰지 않는다. 새 프로젝트의 기본 에셋 포함 체크도 같은 ProjectContext::Create 경로를 쓴다. 템플릿 파일 검증과 복사는 기존 std::filesystem·JsonFile을 사용한다. 복사 중 디스크 실패 시 새 대상 폴더의 일부 파일이 남을 수 있으며 자동 삭제하지 않는다.

### 실제 도구 적용과 실패 기록

픽셀 4종 SKILL.md의 실루엣·색·프레임·피벗·타이밍 지침과 기존 imgui-ui-ux-engineering/cpp-coding-standards를 적용했다. Aseprite/pixel-mcp는 없으므로 해당 명령은 실행하지 않았다. 현재 대화에 MyEngine MCP 도구가 직접 노출되지 않은 상태는 유지된다. 설치된 공식 MCP SDK Client/StdioClientTransport로 tools/mcp/dist/index.js를 실행하여 8개 도구 목록과 실제 engine_capture_frame 호출을 확인했다. 서버의 소스·프로토콜·도구를 새로 만들지 않았으므로 MCP 소스 변경용 npm build/smoke는 이번 변경에서 다시 실행하지 않았다.

Computer Use SKILL.md를 읽고 @oai/sky를 불러왔다. list_apps는 `Trusted RPC service is not configured: sky`로 실패했고 cua.getState에서도 네이티브 앱이 비활성이다. 도구가 없는 상황을 PowerShell UIAutomation/SendInput으로 우회하지 않았다. Chrome의 Godot 공식 스프라이트 애니메이션 문서 열기는 브라우저 보안 정책이 사용자 권한 거부를 반환했다. 이 거부를 다른 브라우저·HTTP·간접 요청으로 우회하지 않았고 새 공식 문서를 읽었다고 기록하지 않는다. 이전 프로젝트 파일 작업에서 확인한 공식 Project Manager/data_paths/nodes_and_scenes/project_organization 계약은 docs/07에 출처·차이를 유지한다. 새 애니메이션 비교 검증은 문서 접근이 가능할 때 진행한다.

### 검증 결과

Debug/Release 전체 빌드와 양쪽 CTest 13/13, 내부 검사 519/519 통과했다. 기존 자체 프레임워크에 5개 검사만 추가했다. 기본 프로젝트의 실제 재실행 보존, 씬/애니메이션 Undo·dirty 분리, 레벨 1 저장/Play 복제, animation/scene 저장 왕복과 .tmp 실패 보존, 잘못된 영역/시간/이벤트/경로, GUID 유지·중복 실패 시 이전 인덱스 보존, Transform 복구/Undo·SpriteCorners를 확인한다.

GUID 회귀 검사를 처음 추가할 때 GPU 없는 동기 TextureImporter를 Loaded로 기대해 518/519가 실패했다. 기존 동기 PNG importer는 디바이스가 필수라는 계약을 확인하고 실제 DX11 디바이스로 검사 조건을 수정했다. 실패를 성공처럼 숨기거나 importer 계약을 테스트에 맞춰 완화하지 않았다. 이후 전체 Debug/Release 검사를 통과했다. 로그는 build/meadow-validation/{debug,release}-{build,tests}.log에 있다. 기존 sol2·getenv/fopen 경고는 별도로 남으며 성능 개선 수치를 새로 주장하지 않는다.

초기 실제 캡처 `MyEditor-2026-10-01T07-36-02-321Z.png`의 잘못된 배율은 Transform 공통 경계 수정 뒤 재캡처로 확인했다. 이후 패널의 일부 신규 한글이 Python→PowerShell 기본 인코딩 때문에 물음표로 기록된 것을 실제 UI에서 발견해 UTF-8 전송·i18n panel.anim 키로 수정했다. 최종 Release 캡처에서 한글과 PNG 미리보기를 확인했다.

| 실제 MCP 캡처 | 결과 |
|---|---|
| MyEditor-2026-10-01T08-10-38-336Z.png | 창 있는 셸·마을·캐릭터·Animation 패널, frame 30, exit 0. 원본 1920×1080을 MCP가 960×540으로 축소. docs/images/editor-animation.png로 그대로 복사 |
| MyEditor-2026-10-01T08-11-00-928Z.png | headless 실제 DX11 씬, 960×540, frame 30, exit 0. docs/images/editor-meadow.png로 그대로 복사 |
| MyEditor-2026-10-01T08-11-01-933Z.png | --play frame 600, exit 0. 짧은 실행에서 대기 첫 포즈와 같은 프레임이므로 이 캡처만으로 진행을 판정하지 않음 |
| MyEditor-2026-10-01T08-12-45-557Z.png | --play frame 3200, exit 0, 프로세스 648ms. Edit 캡처 대비 1484픽셀 변화, 경계 (465,242)-(495,313)가 캐릭터에 한정. 실제 애니메이션 진행 확인 |

캡처와 MCP 호출 로그는 tools/mcp/.state/의 로컬 검증 자료이며 실제 사용자 데이터 대신 build/meadow-validation/project를 사용했다. GUI 패널의 마우스/키보드 수정·저장/취소는 native RPC 부재로 실행하지 못했다. 실제 앱 렌더와 API 회귀를 사용자 전체 조작 검증으로 확대하지 않는다.

### 유지·삭제 이유와 다음 완료 조건

삭제한 것은 세션 전역 임시 클립과 중복 시간 계산·실제 구현보다 앞선 설명이다. 기존 ClipEditing/DirectionalSet 헬퍼·애니메이션 상태 머신·타일/물리 라이브러리는 호출자/검증이 있으므로 보존한다. 문서에 근거 없는 8방향/상태 머신 편집 UI를 나열하지 않는다. 문서 전체 재시작이나 일반 포맷 정리는 하지 않았다.

기본 콘텐츠는 고해상도 픽셀 스타일의 합성 배경과 독립 캐릭터다. 지형·집·수목의 개별 에셋 분리, 충돌/내비게이션·다리층·입력 이동은 실제 제작 데이터 계약이 필요해 D-09 P1로 분류했다. 새 타일/물리 구현 대신 기존 TileEditing·다층 충돌·경로 탐색을 연결한다. 정면 대기/걷기를 8방향·장비·공격으로 확장하는 아트는 그 다음 콘텐츠 작업이다.

.meta 설정 적용·자동 watcher·MyGame GUID 수렴, 미저장 문서 재개/자동 저장·복구, 이미지의 point 미리보기·정확한 픽셀 선택·다양한 DPI·한글 외 신규 패널 라벨 번역은 후속이다. 이벤트 편집은 삭제/추가 방식이고 사운드 자동 매핑은 하지 않는다. 온라인 게임 시스템·물리/Lua Play 연결이나 정식 MMORPG 완성으로 표시하지 않는다.

문서 최종 검사는 README·AGENTS·docs·기본 프로젝트 안내 30개를 UTF-8로 읽고 코드 블록/인라인 코드를 제외한 로컬 링크 557개에서 누락 0개였다. git diff --check도 통과했다. 기본 에셋 PNG의 SHA256이 imagegen 원본과 같아 원본 보존을 확인했다. 문서 화면은 MCP가 저장한 앱 캡처 PNG를 그대로 복사했다.

기존 character_demo hotreload 시나리오는 samples/character_demo/assets/scripts/player.lua를 잠깐 덮어쓴 뒤 복원한다. Debug/Release CTest를 동시에 실행하면 한 프로세스가 다른 프로세스의 임시 스크립트를 원본으로 보관할 수 있었다. 검사 전 clean 상태였고 변경 내용이 샘플의 v2 코드와 일치함을 확인해 테스트가 바꾼 파일만 HEAD 원본으로 복구했다. 최종 CTest는 Debug 다음 Release 순서로 재실행하고 각 상세 로그를 build/meadow-validation/{debug,release}-details.log로 보관했다. 샘플의 사용자 콘텐츠는 변경하지 않는다. 후속 검증은 이 소스 fixture를 build/의 실행별 작업 복사본으로 격리하는 것이 완료 조건이다.

AnimationAsset Undo는 기존 값 스냅샷 방식을 쓴다. 기본 8프레임 콘텐츠에서는 단순하고 수명이 명확하며, 최대 크기 시트·긴 편집 이력에서 메모리가 병목이면 변경 프레임만 기록하는 방식으로 교체한다. 지금은 측정 없는 캐시·풀·스레드·범용 명령 프레임워크를 추가하지 않았다.

### 제작 프롬프트

#### 배경

```text
Use case: stylized-concept
Asset type: playable 2.5D pixel-art RPG starter village background, landscape 16:9 composition.
Primary request: Create a beautiful high-detail original pixel-art meadow village game map. Orthographic elevated three-quarter view, screen-aligned ground, no perspective vanishing point. A small quiet village with three timber-and-plaster cottages with terracotta roofs across the upper half, a stone well left of center, flower gardens and wooden fences, a winding ochre dirt path from bottom center to a central plaza, a shallow sparkling brook and small wooden bridge at the right, clustered deciduous trees framing the top corners. Keep a broad clear walkable dirt/grass patch around the exact center and lower half to place a playable character later.
Style/medium: meticulously hand-placed-looking crisp pixel clusters, sharp stepped edges, detailed grass and foliage, coherent limited color ramps, NO smooth brush strokes, no blur, no antialiasing or depth of field. Game-ready cohesive pixel art, original fantasy rural setting.
Lighting/mood: warm peaceful late morning, subtle cool green-purple shadows, warm yellow-green highlights, consistent light from upper left.
Composition: landscape canvas, complete map filling canvas, no border, no UI, no characters, no text, no logos. Readable roofs, path and silhouettes even at small game scale.
Color palette: emerald and sage grass, warm ochre paths, ivory plaster, walnut timber, terracotta roofs, cornflower-blue water and wildflowers.
Constraints: no references to existing games or franchises, no watermark. Buildings and trees fixed scenery, terrain deliberately clear at center.
```

#### 캐릭터

```text
Use case: stylized-concept
Asset type: transparent PNG pixel-art RPG character animation sprite sheet.
Primary request: original level-one young novice adventurer, chestnut short hair, teal tunic with ivory undershirt, leather belt and tiny satchel, brown boots, no weapon. High-detail crisp pixel clusters, warm highlights cool shadows, readable silhouette, game-ready. Front view facing down-screen, slightly elevated orthographic game camera. Exactly EIGHT full-body sprites laid out in ONE horizontal row, eight equal-width cells. First four cells form a subtle breathing idle cycle (same feet alignment); last four cells form a walking cycle (left leg forward, passing, right leg forward, passing), visibly distinct foot and arm positions. Same character, scale, outfit and lighting every cell. Each sprite fully contained with generous blank transparent space on all sides, same baseline, no overlap. Canvas very wide, 8:1 aspect ratio. Character fills about 65% of each cell width and 80% height. Pixel art only, sharp square pixels and stepped edges, no smooth gradients, no blur, no cast shadows outside character. Background actually transparent. No cell borders, no labels, no lettering, no UI, no watermark, no existing franchise references.
```

커밋 후 첫 푸시는 원격 main의 LICENSE 수정 dc2b377 때문에 fast-forward 조건을 만족하지 못했다. 원격 변경이 LICENSE 1개임을 확인하고 해당 커밋 위로 이번 작업을 rebase하여 보존했다. 검증한 코드·에셋은 그대로이며, 라이선스 수정 내용을 되돌리거나 강제 푸시하지 않았다.


## 2026-10-01 — 개별 오브젝트·조작·동작·맵 연결

### 구조와 변경 근거

기본 콘텐츠는 255개 엔티티의 마을(지형 240개, 건물·수목·소품·출입구·도착점·레벨 1 플레이어)과 57개 엔티티의 실내로 구성했다. 배경 한 장에서는 개별 이동·충돌·이벤트 지정이 불가능했으므로 기존 SpriteRenderer의 아틀라스 UV와 SceneSerializer로 분리했다. Terrain 그룹과 이름이 있는 오브젝트를 Hierarchy에서 선택한다. 청크 타일 파일 형식을 새로 만들지 않았으며, 기존 TileEditing의 저장/브러시 연결은 후속이다. 기존 배경 PNG는 아트 비교·재구성 참고로 보존하고 실행 씬의 배경 렌더는 제거했다. 기존 사용자 프로젝트는 자동 덮어쓰지 않으며 새 프로젝트의 기본 에셋에서 갱신된 씬을 제공한다.

호출 경로를 ProjectContext/Document 등록→SceneSerializer→PlayMode 스냅샷→고정 틱 ObjectSystem→PhysicsWorld2D·ScriptSystem→이벤트 동작/ScenePortal→SceneTransitionManager의 후보 로드/교체→RenderExtract/HybridRenderer까지 확인했다. 게임 고유 맵·문구·캐릭터 데이터는 game/starter에, 공유 컴포넌트·실행은 engine/runtime에, 키보드와 화면은 editor/apps에 둔다. editor→runtime은 CMake PRIVATE 의존이며 엔진에서 앱을 역참조하지 않는다.

ObjectName은 씬의 유일 이름이다. 기존 Collider2D/Shape2D/KinematicBody2D에 리플렉션을 등록해 설정을 저장하고, velocity/lastMove/hitWall은 실행 전용으로 유지했다. CharacterController2D·InteractionTarget·ScenePortal·ObjectBehavior를 추가했다. 조작은 정규화한 방향과 초당 speed를 기존 물리에 전달하며, E는 프레임에서 포착해 고정 틱에서 한 번 소비한다. 한 씬의 활성 로컬 컨트롤러는 1개이고 world XY 물리 때문에 부모 없는 루트로 제한한다. 입력 없는 틱도 물리·트리거·Lua·애니메이션을 같은 경로에서 진행한다.

동작은 Start/Interact/TriggerEnter/TriggerExit와 Message/SetVisible/MoveTo/ChangeMap/LuaCallback 연결 64개까지다. 인스펙터의 이벤트 카드→선→동작 카드는 순서대로 실행한다. 기존 PropertyEditCommand의 컴포넌트 루트 값 경로로 Undo·dirty·씬 저장을 공유한다. Lua 편집은 ObjectBehavior.luaSource(64 KiB)에 저장하고 기존 ScriptComponent/ScriptSystem의 보호 호출로 실행한다. 자유 노드 VM·조건 컴파일러·별도 편집 문서 상태를 만들지 않았다. 반복적인 디자이너 요구가 생기면 실제 Lua 동작을 카드로 추가한다. 함수 존재 여부는 현재 정적 검사하지 않는다.

PlayWorld마다 VM·물리를 소유하며 ECS 순회 중 컴포넌트를 추가하지 않도록 스크립트 엔티티를 먼저 수집한다. 오류 구독은 LoadClass 전에 연결한다. Stop/맵 교체/종료에서는 on_destroy→스크립트 추적 테이블→ScriptComponent→VM을 World/이벤트 버스보다 먼저 해제한다. Play 중 에셋 새로고침은 막아 비소유 애니메이션·스크립트 참조 수명을 보존한다. PNG/.anim 연결은 기존 AssetDatabase GUID와 MYE_ASSET 드롭·PropertyEditCommand를 사용한다. 애니메이션 GUID를 비우면 기존 런타임 포인터도 해제한다.

마을 문은 cottage.scene의 Cottage Spawn, 실내 출구는 meadow_village.scene의 Village Spawn으로 연결한다. 기존 전환 상태 머신의 fade-out/load/activate/fade-in을 사용한다. 정규화된 파일이 프로젝트 assets 내부인지, 씬·컴포넌트·유일 이름·도착점·활성 플레이어가 유효한지 후보 World에서 검사한 뒤 PlayWorld만 교체한다. 실패는 기존 맵을 보존한다. 레벨/XP는 전달하되 인벤토리·퀘스트·온라인 존 이전을 완료로 표시하지 않는다. 현재 작은 씬의 동기 로드와 동작 시 이름 선형 탐색은 단순하게 유지하고, 지연/검색 비용이 측정되면 기존 비동기 로딩·이름 인덱스로 바꾸는 조건을 ponytail 주석에 남겼다.

### 실제 결함과 수정

공용 SceneSerializer의 생성 전 패스에 일반 필드 타입·유한 값·정수/float 범위·열거 이름·버전·중첩 깊이 검증을 추가했다. 기존 JsonArchive가 정수 필드도 double JSON으로 쓰는 것을 처음에 놓쳐 정상 왕복을 IsInteger로 거부했다. 기존 출력 계약을 확인해 유한한 정수 값으로 검사하고 테스트를 재실행했다. 커스텀 훅은 평탄화 키가 리플렉션 필드와 달라 객체/버전 검증과 기존 훅·오브젝트 값 검사로 구분한다. 64비트 정수 아카이브의 기존 double 정밀도 한계까지 새로 해결한 변경은 아니다.

Project Open/OpenScene/SaveScene와 Play/목적지 로드에 공용 ObjectComponents 검증을 적용했다. 불완전한 포털의 저장은 디렉터리/파일 쓰기 전에 실패해 원본 파일과 dirty를 유지한다. 미등록 타입을 AddComponent 목록에서 제외해 소멸자가 필요한 값의 잘못된 동적 구성도 막았다.

2D/3D는 카메라·선택·렌더까지 연결했다. 기존 Camera2D·픽셀 스냅·깊이 계약을 유지하고 3D에서는 LH 원근 View/Projection과 full world XYZ/clip Z/W를 쓴다. 3D 스프라이트 선택/외곽선과 Z=0 작업 평면 역투영, 2D 이동 기즈모·충돌 영역 표시를 제공한다. 실제 3D 캡처에서 같은 평면의 지형이 건물/울타리를 부분 가림하는 것을 확인해 공용 QuadVS에 기존 정렬 깊이 기반 최대 1e-5 NDC 동률 바이어스를 넣었다. XY/W·물리 위치·2D 결과는 바꾸지 않았고 재캡처에서 전체 건물/울타리를 확인했다. 메시 제작·3D 물리·3D 기즈모는 연결된 기능으로 표시하지 않는다.

실제 Play/실내 전환 캡처에서 캐릭터가 사라지는 현상을 조사했다. GatherColliders가 origin+offset으로 구한 콜라이더 중심을 PhysicsWorld2D::Step이 Transform 원점에 그대로 기록해, 입력이 없어도 매 틱 offset이 누적되는 것이 원인이었다. 모든 Step 호출자(PhysicsSystem, ObjectSystem, bridge/village_demo, 기존 물리/씬 검사)를 확인하고 공용 Step에서 offset을 빼서 원점으로 되돌렸다. 임시 Lua/로그로 도착 위치를 확인한 뒤 진단 코드는 제거했다. 비영점 offset·무입력 여러 틱 회귀와 최종 실제 앱의 제자리/실내 캐릭터 캡처를 남겼다.

기존 기본 프로젝트 검사는 엔티티를 2개로 고정해 분리된 마을에서 522/523으로 실패했다. 엔티티 증가·저장·재열기의 목적에 맞게 기존 수+생성 1개로 확인했다. Debug Expected::Value의 실패 지점을 로그에서 찾기 위해 테스트 stdout만 비버퍼로 설정했다. 신규 범위 검사 테스트에서 읽기 전용 json::Value를 직접 수정하려 해 컴파일 실패했고, 기존 Parse API의 실제 JSON 입력으로 수정했다. 테스트 실패를 숨기거나 검증 계약을 완화하지 않았다.

### 에셋과 도구

village_objects.png는 RGBA 1024×1536, terrain_tiles.png는 1254×1254의 4종 지형 아틀라스다. GUID는 각각 ea8b5521-683c-48ee-b36a-e224d835d054, ca04f01a-4472-4678-979b-5cf3973dd9c4로 .meta에 보존했다. 원본은 C:/Users/harun/.codex/generated_images/01a0f35b-986b-7332-9a3d-d201d45cb524/의 exec-59362834-c478-4a0c-be34-b6320a254c00.png와 exec-eaa3a8ad-731f-4d22-b8cd-84528da6a39a.png다. 첫 오브젝트 결과 exec-d3c2c49f-51d1-47fe-855d-156e18d482da.png는 셀 경계를 넘는 개체가 있어 사용하지 않았다. 2열×3행 각 512셀 안에서 전체 개체·넉넉한 여백·최대 380픽셀 실루엣을 요구해 보정했다. 실제 alpha 0..254와 각 셀의 alpha>128 경계로 UV를 확인했다. Python/PIL은 읽기 검증과 JSON/UV 계산에만 사용했으며 이미지 편집은 imagegen, 파일은 원본 그대로 복사했다. 지형 셀 배율은 48/627이고 물 타일은 아틀라스에 제공하되 기본 마을에 배치하지 않았다.

clean-code·lua 스킬과 기존 C++/ImGui/픽셀 스킬의 계약을 적용했다. 기존 라이브러리로 충족해 추가 패키지나 에이전트를 실행하지 않았다. MCP 소스는 바꾸지 않고 설치된 공식 SDK Client/StdioClientTransport에서 기존 서버의 8개 도구와 engine_capture_frame을 사용했다. Computer Use의 native RPC 미구성과 Godot 새 문서의 브라우저 접근 거부는 유지되어 우회하지 않았다. GUI 키보드·마우스 전체 조작은 실행하지 못했으며 API 회귀와 실제 렌더 결과를 구분한다.

### 검증 결과

최종 확인은 아래 명령의 전체 Debug/Release 빌드·순차 CTest다. 샘플 hotreload가 소스 fixture를 잠깐 바꾸므로 두 구성을 동시에 검사하지 않았다. 데이터 실험은 새 build/object-validation 및 build/dev/test-data 경로에서 수행했다.

```powershell
cmake --build build/dev --config Debug
ctest --test-dir build/dev -C Debug --output-on-failure
cmake --build build/dev --config Release
ctest --test-dir build/dev -C Release --output-on-failure
```

검사 5개는 기존 프레임워크에 추가했다. 조작/충돌/트리거/Lua/offset, 동작 루트 Undo·저장 왕복/잘못된 enum·큰 float·중복 이름, 맵 이동/성장 유지/실패 보존, 2D/3D 투영 왕복, 잘못된 저장의 원본/dirty 보존을 확인한다. 빌드 중 실행 중인 Release MyEditor가 LNK1104를 일으켜 사용자에게 저장·종료를 요청하고 강제 종료하지 않았다. 프로세스 종료를 확인한 뒤 실행 파일 교체를 완료했다. 기존 sol2·nodiscard·getenv 경고는 이번 작업의 성능 개선으로 설명하지 않는다.

| 최종 Release 실제 MCP 캡처 | 확인 범위 |
|---|---|
| MyEditor-2026-10-01T10-12-59-998Z.png | 2D 개별 지형·집·소품·캐릭터, 30프레임, exit 0. docs/images/editor-meadow.png |
| MyEditor-2026-10-01T10-13-00-991Z.png | 창 있는 Hierarchy·동작 카드·컴포넌트 UI, 30프레임, exit 0. MCP의 1920×1080→960×540 축소본 그대로 docs/images/editor-objects.png |
| MyEditor-2026-10-01T10-13-01-847Z.png | 실제 3D 원근과 겹침 동률 수정 결과, 30프레임, exit 0. docs/images/editor-3d.png |
| MyEditor-2026-10-01T10-13-03-198Z.png | 검증 복사본의 Start→ChangeMap으로 실제 Play 전환·도착 캐릭터, 6000프레임, exit 0. docs/images/editor-cottage.png |

전환 실험은 build/object-validation/transition-project의 Village Sign에 Start→ChangeMap을 추가했고, 배포 원본을 변경하지 않았다. E 상호작용과 실패 이동은 API 검사에서 확인했으며 이 자동 전환 캡처를 사람의 E 입력 검증으로 확대하지 않는다. 프레임/실행 시간은 캡처 재현 정보이며 CPU/GPU 성능 기준선이나 개선 수치가 아니다. 상세 로그는 build/object-validation/{debug,release}-details.log, 캡처·앱 로그는 tools/mcp/.state에 있다.

### 다음 완료 조건

P1은 MyGame이 같은 오브젝트 시스템·GUID 맵·포털을 소비하는 실행 조합이다. P2는 기존 Tilemap 청크 저장/브러시·다층 이동/내비게이션, 네이티브 포커스/E·드롭·Undo·저장 사용자 흐름, 8방향/공격 아트다. 3D 메시 제작·기즈모·물리, 조건 노드와 인벤토리/퀘스트 맵 이전은 실제 요구·검증 조건을 먼저 정한다. 새 물리/스크립트/그래프 프레임워크·측정 없는 캐시/스레드는 추가하지 않았다. 관련 02/03/05/06/07/12/13/14/15/17 문서와 기본 에셋 안내를 구현 상태에 맞게 갱신했다.

### 오브젝트 아틀라스 최초 제작 지시

```text
Edit this original meadow village art into a game-ready transparent object sprite atlas. Preserve its crisp detailed pixel-art style, same upper-left lighting, same ivory timber cottages and terracotta roof design. Remove ALL terrain, sky, grass background and scenery connections. On a truly transparent background arrange EXACTLY SIX separated full objects in a TWO COLUMN by THREE ROW layout, each with generous empty transparent margin, no overlap. Top row: left large cottage with striped awning and door facing camera, right smaller cottage. Middle row: left roofed stone well, right one full round-canopy deciduous tree including trunk. Bottom row: left wooden fence segment with two posts, right wooden signpost with blue banner and small flower planter. Elevated orthographic three-quarter RPG view; ground/feet anchors at bottom center of each object. No labels, no borders, no text, no characters, no watermarks. Sharp square pixel clusters, no blur, no soft antialias. Buildings complete with every roof edge visible; actual alpha outside objects. This image is a reusable object atlas, not a full scene.
```

보정에서는 셀을 넘던 가장자리를 이유로 같은 여섯 개체를 2열×3행 각 512셀 중앙에 완전히 넣고, 주변 투명 여백과 최대 380픽셀 실루엣을 명시했다. 지형은 동일한 광원·색조의 풀/흙/물/돌 네 개 셀로 분리하도록 지정했다. 실제 사용 여부는 지시문이 아니라 alpha·셀 경계와 앱 렌더 결과로 결정했다.

최종 전체 Debug/Release 빌드는 모두 성공했다. 양쪽 CTest는 각각 13/13, 내부 검사는 각각 524/524 통과했다. 일반 float 필드의 초과 범위 차단까지 반영한 최종 상세 로그를 build/object-validation/{debug,release}-details.log에 보관했다. 문서 301개 로컬 링크 대상의 누락은 0개였고 git diff --check가 통과했다. 신규 PNG 두 개는 생성 원본과 SHA256이 같아 이미지 파일 보존을 확인했다. 네이티브 수동 UI 조작·성능 전후 측정은 실행한 검증으로 표시하지 않는다.

## 2026-10-01 · 도트 제작·모션 연결·중립 UI·제작 가이드

### 변경의 이유와 범위

상단 파일 상태의 긴 오류가 메뉴·툴바를 밀던 원인을 EditorApp 공통 셸에서 수정했다. 상태는 고정 하단 한 줄·툴팁·콘솔로 이동하고 도킹 호스트 높이에서 제외한다. 회색 표면·파란 선택 강조는 공용 ImGuiSkin에 모았다. 새 씬/저장/Play/Stop/Pause/Step·도트 파일 버튼은 공용 벡터 도형+텍스트를 사용한다. 타사 아이콘 팩·별도 글꼴을 복사/설치하지 않는다. Godot 공식 source/문서와 PixelOver 작업 흐름의 확인 URL·채택/제외 범위는 [18](18-editor-authoring.md)에 있다.

Inspector의 기본 이동 구성은 기존 트랜잭션·AddComponent·PropertyEdit로 SpriteRenderer/Collider2D/KinematicBody2D/CharacterController2D 누락만 추가한다. 기존 값과 Transform을 보존하며 새 충돌만 발밑 박스로 정한다. 최상위·한 명 규칙과 등록/WorldTransform을 mutation 전에 확인한다. Runtime의 이동 상태 진입 GUID를 transient로 기억하여 후속 모션을 매 틱 초기화하지 않는다.

이전 DotEditorPanel의 패널 전역 픽셀과 cwd 기준 PNG 저장을 제거했다. 이유는 프로젝트·문서·Undo·원본 보존과 연결되지 않았기 때문이다. 사용자가 저장한 기존 PNG는 삭제하지 않았다. DotDocument는 제작 데이터/버전·검증, Project는 문서 수명·저장, Panel은 조작·미리보기로 책임을 나눴다. 공유 I/O·TextureImporter·GUID/meta·AnimationAsset·ClipPlayback을 재사용한다. STB 이미지 쓰기 구현은 중복 없이 DotEditing.cpp 한곳으로 이동했다.

참조 PNG→픽셀/프레임→분리 영역의 부모 계층·피벗·기본 자세/포즈 키→모션/후속→PNG/.anim/.meta→DB→씬→Play 경로를 연결했다. 외부 파일은 크기를 먼저 확인하고 잘못된 버전·범위·키·계층·다음 이름을 거부한다. 새 GUID 폴더로 내보내며 실패 시 해당 호출이 만든 파일과 빈 폴더만 회수한다. 저장 실패는 원본과 dirty 상태를 보존한다. 확대 anchor 좌표·연속 선·채우기·클리핑된 가로 구간 그리기와 Undo 후 캐시 무효화를 적용했다.

첫 실제 Dot UI 캡처에서 도킹 초기 영역의 임시 크기를 한 번만 맞춤 처리해 이미지가 구석에서 너무 작게 나타났다. 맞춤 모드가 현재 영역 크기에 계속 대응하고 1배 미만도 fractional 비율을 사용하도록 공통 DotCanvasView::Fit에서 고쳤다. 사용자 줌/팬/슬라이더/1:1 조작은 맞춤을 해제한다. 작은 배율/영역 변경 검사와 재캡처로 확인했다.

원본 novice.png·씬·GUID는 유지하고 새 기본 프로젝트에 편집 가능한 novice.dot만 추가했다. 기존 첫 프레임을 제작 API로 nearest 축소/배치해 만든 128×288, 12프레임, 4파츠, idle/walk/greet 예제다. 완성된 8방향·고급 보행 아트로 표시하지 않는다. 스냅샷 Undo의 메모리 상한, 강체 파츠의 절단 경계, IK/메시/3D 리깅 미지원과 다른 앱 소비 조건은 18/14에 기록한다. 측정 없는 성능 향상 수치를 주장하지 않는다.

### 스킬·가이드·빌드

기존 ImGui/C++/clean-code/Lua/픽셀 스킬과 저장소 계약을 재사용한다. find-skills의 skills.sh 검색·원문 평가 후 Vercel web-design-guidelines만 추가 설치했다. 공식 출처·설치 명령·hash·기존 스킬을 유지/제외한 이유는 [15](15-skills-and-agents.md)와 skills-lock.json에 있다. 새 에이전트·웹 프레임워크·아이콘/폰트/영상 패키지를 추가하지 않았다.

가이드는 정적 HTML/CSS/JS로 결과→단계→이유/근거 순서, 검색·작업 목차·앵커·skip link·명시적 label/alt/크기·focus-visible·비디오 controls를 제공한다. 자동 재생하지 않고 GIF는 내려받기 링크로 둔다. 이미지 4종은 기존 공식 MCP SDK/서버의 실제 UI/2D Play/3D 캡처, MP4/GIF는 실제 제작 API의 walk 시트·타임라인 인코딩이다. MP4는 이미 설치된 OpenCV와 Windows Media Foundation H.264, GIF는 Pillow로 인코딩했다. 마우스 제작 녹화라고 설명하지 않는다.

MyEditorContent CMake 타깃은 실행 파일 빌드의 의존성으로 starter/docs를 복사한다. 소스 링크가 생략되는 문서 전용 변경에서도 가이드가 갱신되도록 이전 POST_BUILD를 대체했다. 도움말은 EnginePaths.engineDir/docs/guide/index.html을 존재 검사 후 ShellExecute로 연다. 실패는 하단 상태로 전달한다. 현재 한글 도움말 라벨의 다국어 번역은 남아 있다.

### 실제 검증

- 전체 Debug/Release 빌드 성공. 순차 CTest 각각 **13/13**, 내부 각각 **529/529**. 최소 회귀 5개는 기존 TestFramework/CMake에 추가했다. 최종 Play 편집 경계까지 포함한 Debug CTest 33.95초, Release CTest 28.44초였다. 헤더 주석·unused include·공백·콘텐츠 복사 갱신도 양쪽 빌드로 확인했다. 샘플 hotreload 때문에 두 CTest를 동시에 실행하지 않았다.
- 저장 실패 보존/한글·경로/버전·Undo, PNG 로드, 원본 왕복, 뼈 계층·각도/보간/맞춤, 프레임 삭제 키·구간, 반복/후속 GUID, 재내보내기 보존, 이동 구성의 idempotence/Undo/한 명/모션 상태, 제공 프로젝트 복사→도트 저장→export→씬 저장을 검사했다. 새 build/dev/test-data 경로만 사용했다.
- 공식 MCP SDK로 8개 도구를 확인하고 Debug/Release 각각 MyEditor UI·Dot UI·2D Play·3D frame40을 캡처했다. 원본 UI 1920×1080, MCP 결과 960×540, 모두 exit0. 근거 로그는 build/dot-{release-,}captures.log, 서버 로그/원본은 tools/mcp/.state에 있다.
- 후속 모션의 실제 Play 통합은 새 build/object-validation/successor-check 3프로젝트로 확인했다. 시작 대기, 짧은 greet→idle 연결, 연결을 제거한 greet 마지막 포즈를 각각 Release/headless/Play frame600 캡처했다. 대기와 연결 후 영상의 다른 픽셀 **0**, 연결 없는 마지막 포즈와 대기의 다른 픽셀 **424**. 포즈가 원래 같아서 우연히 통과하지 않도록 다른 마지막 포즈 대조를 사용했다. 검사 로그는 build/dot-successor-check.log와 해당 PNG다.
- ImGui scope 검사 변경 UI 파일 4개 PASS. 스킬 quality_suite semantic 30/30·공식 header 확인은 통과했지만 전체 결과는 g++/c++ 미발견으로 FAIL이다. 별도 MSVC 제품 빌드를 이 실패의 대체 성공으로 표시하지 않는다.
- node --check guide.js PASS. 정적 HTML의 로컬 참조·앵커·이미지 alt/실제 크기·video controls와 자동재생 부재 확인. H.264 MP4 768×576/24프레임/20fps, GIF 8프레임 확인. Markdown 코드 블록/인라인 코드를 제외한 로컬 링크 **587개 누락0**, git diff --check PASS. 이전 링크 검사에서 C++ lambda를 링크로 오인한 검증기 규칙을 고쳤으며 문서를 임의로 바꾸지 않았다.

### 미완료 검증과 한계

네이티브 Computer Use RPC 미구성으로 파일 선택/드롭/키보드/모션 키 입력/DPI 수동 흐름은 미검증이다. 가이드 브라우저 주소 http://127.0.0.1:8765 접근은 Browser Use 보안 정책이 사용자 권한 거부로 차단했다. 다른 surface/CDP/직접 입력으로 우회하지 않았다. 정적 파일·디코더 검증과 브라우저의 화면/키보드/비디오/오프라인/도움말 직접 클릭 검증은 구분한다. 테스트용 HTTP 서버는 종료했다. 자동 저장/크래시 복구·MyGame 통합·대규모 Undo 메모리 측정·고급 리깅과 P0 온라인 인증/영속성 조건은 별도 잔여다.

최종 공통 명령 경계 검토에서 Play 중 포커스된 제작 문서의 Undo를 선택할 수 있는 순서 문제를 고쳤다. ActiveStack은 Play 스택을 먼저 선택하고 SaveActive는 Stop 안내를 전달한다. Dot 문서 파일/저장/Undo 버튼도 Play 중 비활성화한다. 기존 이동 회귀에 Play 중 구성/모션 지정 거부 검사를 추가했고 전체 양쪽 13/13·529/529 통과를 재확인했다. 최종 Markdown 로컬 링크 589개 누락0으로 확인했다.

## 에디터 UI 스킬 재조사

2026-10-01: 사용자의 UI 스킬 조사 요청에 따라 설치된 find-skills·ImGui·game-ui-ux·웹 지침과 추가 후보 원문을 비교했다. skills.sh leaderboard 및 CLI의 `game editor ui`·`imgui`·`desktop ui ux accessibility`, MCP Market 게임 개발 목록, GitHub 원문·revision·stars를 확인했다. 판단·출처·설치 수·보류 이유·적용 순서는 [15](15-skills-and-agents.md#에디터-ui-개선용-스킬-재검토)에 기록했다.

기존 C++ Dear ImGui 스킬이 에디터 구현에 직접 맞아 주력으로 유지한다. game-ui-ux는 게임 HUD, 웹 지침은 HTML 가이드에 적용한다. ui-ux-pro-max는 데스크톱 스택도 지원하므로 이전의 웹 전용으로 읽힐 수 있는 분류를 정정했지만 Dear ImGui 전용 구현 지침은 없다. impeccable의 native audit도 모바일 프레임워크 대상임을 확인했다. Unity IMGUI는 C#의 다른 API이며 minimalist-ui는 밝은 웹 디자인을 강제해 제외했다. 인기도를 제품 적합성·검증 성공으로 취급하지 않았다.

문서만 변경했다. 기존 설치로 역할을 충족해 추가 설치·lock 변경·에이전트 실행은 하지 않았고 UI 코드·에셋·설정은 변경하지 않았다. 변경 문서의 로컬 링크 27개 누락0·새 절 앵커·git diff --check를 확인했다. 제품 빌드·CTest·실제 UI 조작을 이번 조사에서 다시 실행한 것으로 기록하지 않는다.

## 2026-10-01 · 작업대 전환·다섯 도크·씬 요소 선택

### 요청과 설계 근거

상단 왼쪽 메뉴·중앙 2D/3D/씬/도트메이커/Lua 텍스트·오른쪽 실행/일시정지/중지를 배치했다. 경계 없는 텍스트는 선택 색과 밑줄로 표시하고 키보드 포커스·클릭은 기존 ImGui Button이 처리한다. 주변 다섯 영역은 Hierarchy, Asset Browser, Console, Inspector, Animation 탭이다. 타일 팔레트/타일맵 편집도 기존 창 메뉴로 같은 도크에 추가할 수 있다. 좁은 창은 제어 행을 메뉴 아래에 배치한다. 버튼 크기·간격은 현재 ImGui 스타일·글꼴 크기에서 계산하며 별도 테마 설정을 만들지 않았다.

공식 Godot [First look at the editor](https://docs.godotengine.org/en/stable/getting_started/introduction/first_look_at_the_editor.html), [Nodes and Scenes](https://docs.godotengine.org/en/stable/getting_started/step_by_step/nodes_and_scenes.html), 4.5 태그의 [EditorNode](https://github.com/godotengine/godot/blob/4.5/editor/editor_node.cpp)·[CreateDialog](https://github.com/godotengine/godot/blob/4.5/editor/gui/create_dialog.cpp)를 2026-10-01 확인했다. 메뉴/작업대/실행의 위치, 2D/3D 시작 선택, 검색 포커스·결과 설명·확정 흐름을 채택했다. 코드·아이콘은 복사하지 않았다. 기존 ECS/GUID/World와 PlayWorld를 유지하며 전체 노드 상속·카탈로그·즐겨찾기는 현재 요구가 없어 추가하지 않았다. 이전에 보안 정책으로 차단된 다른 페이지는 우회하지 않았다.

### 코드 경계와 보존

- EditorApp/EditorWidgets가 공통 상단 동작과 네이티브 ImGui 버튼·벡터 도형을 제공한다. SelectWorkspace가 중앙 패널과 카메라 종류를 정한다. 중복 2D/3D 뷰포트 버튼·중앙 제목 탭·전역 문서 행을 제거한 이유는 동일 선택을 여러 곳에서 관리하고 공간을 차지했기 때문이다. 파일 생성/저장·Step은 기존 메뉴와 단축키로 유지한다. 확장 툴바 항목은 도구 메뉴에 노출한다.
- PanelManager는 새 도킹 루트에서 주변 다섯 탭을 처음 배치하고 저장된 분할은 유지한다. 기존 레이아웃 파일을 삭제하지 않았다. 열린 패널 복원에서 기존 인스턴스를 열어도 마지막 패널에 상태를 쓰던 원인을 인스턴스 ID 조회로 수정했다. 선택한 작업대는 기존 session.json에 보관한다. 프로젝트 전환 성공 후 문서 선택 ID와 작업대를 초기화하고 로컬 세션을 복원한다. 콘텐츠 문서 전체 재개를 구현한 것으로 설명하지 않는다.
- EditorWorkspace.cpp는 기존 IEditorPanelFactory와 문서/명령을 사용한다. 씬 생성/추가 요청의 임시 UI 상태와 두 중앙 패널을 분리했다. 2D/3D는 같은 씬의 평면/원근 시작 보기이며 새 메시·3D 물리 형식이 아니다. 오브젝트·스프라이트·캐릭터·충돌·트리거·상호작용·도착 지점·Lua 8개 조합만 표시한다. 검색 결과가 기존 선택을 숨기면 첫 결과로 변경해 잘못된 요소 생성을 막는다. 빈 결과·잘못된 부모·실행 상태·변경된 씬·중복 조작 캐릭터를 거부하고 오류를 표시한다.
- 생성 전에 등록 컴포넌트·직렬화 값을 확인하고 생성/이름/컴포넌트를 한 트랜잭션으로 기록한다. 캐릭터는 기존 SetupCharacterMovement를 재사용한다. CreateEntityCommand의 Redo가 다른 핸들을 생성해 뒤따르는 컴포넌트 명령이 옛 대상을 가리키던 공통 원인은 기존 World::CreateWithId로 수정했다. Undo는 먼저 부모 Children에서 분리한다. 명령 밖에서 엔티티 슬롯을 임의 재사용하는 작업은 같은 핸들 복구 계약을 보장하지 않는다.
- Lua 작업대와 Inspector는 DrawObjectLua를 공유하고 luaSource만 기존 PropertyEdit로 기록한다. 이벤트 연결의 전체 복사본을 확정한 뒤 Lua를 편집해 서로의 변경을 덮지 않는다. Play 중에는 읽기 전용이다. 메뉴 포커스에서도 도트 작업대의 저장 대상을 유지하며 명시적인 애니메이션 패널 포커스를 우선한다. CLI --workspace는 시작 화면/실제 캡처용이며 OS 입력을 주입하지 않는다.

사용자 에셋·기본 콘텐츠·GUID·씬 형식·렌더 좌표·물리 계약은 변경하지 않았다. 측정 없는 성능 수치·캐시·스레드·새 UI 프레임워크를 추가하지 않았다. 기존 imgui-ui-ux-engineering·clean-code 스킬을 적용했고 새 패키지/에이전트는 필요하지 않았다. 기준 문서 07/13/14/17/18 및 오프라인 가이드의 사용 순서를 갱신한다.

### 검증과 제한

최종 결과와 실제 캡처는 아래에 기록했다. Debug/Release 전체 빌드 로그는 build/workspace-build-{debug,release}.log, CTest와 상세 출력은 build/workspace-ctest-{debug,release}.log 및 build/workspace-{debug,release}-details.log다. Release 첫 시도는 사용자가 연 MyEditor.exe의 잠금으로 LNK1104가 발생했다. 저장·종료를 요청했고 강제 종료하지 않았다. 프로세스 종료를 확인한 뒤 기존 build/dev 트리의 실행 파일을 정상 교체했다. 잠금 당시 로그는 build/workspace-build-release-locked.log다.

기존 프레임워크에 3개 회귀를 추가했다. 작업대 전환/문서 보존/새 씬/실행 중 거부, 요소 추가/부모 Children/반복 Undo·Redo/저장 재열기/중복·잘못된 입력, 실제 ImGui 도킹/작업대/모달/검색·Enter/Lua 기본 코드·Undo/실행·일시정지·계속·중지/좁은 창/작업대 재열기를 검사한다. 기존 기본 프로젝트 회귀에는 도트 작업대를 유지한 상태의 애니메이션 저장 대상 검사를 추가했다. UI 검사에는 실제 ImGui 컨텍스트·OnFrame·ActivateItemByID·텍스트/키 이벤트를 사용하며 OS 마우스·키 입력 검사로 확대하지 않는다. 검색 포커스는 등장 프레임의 요청 다음 프레임에 적용되므로 테스트 입력도 그 순서를 따른다.

스킬 quality_suite는 구조/대비·정적 픽스처·의미 기준 30/30과 공식 v1.92.0/master 소스 가져오기를 통과했다. g++/c++만 탐색하는 스킬 컴파일 단계는 이 MSVC 환경에서 실패했으므로 전체 스킬 게이트 통과로 표시하지 않는다. 제품은 vendored Dear ImGui 1.92.9-WIP와 MSVC로 빌드한다. 정적 검사의 EditorApp/Panel/Inspector/EditorWidgets는 통과했고 팝업 파일은 검사기가 실제 API인 EndPopup 대신 존재하지 않는 EndPopupModal/EndPopupContextItem/EndPopupContextWindow를 요구해 실패했다. 실제 헤더·빌드·런타임 회귀로 짝을 확인했으며 스킬 검사를 임의 수정하지 않았다. 로그는 build/workspace-skill-quality.log, build/workspace-static.log다.

네이티브 CUA 입력 RPC는 미구성이므로 OS 파일 선택/마우스 클릭·여러 DPI·접근성 전체 검증은 미완료다. 정적 HTML/미디어 검사와 앱 프레임 캡처를 브라우저 직접 사용 검증으로 설명하지 않는다. 외부 Lua 파일 편집·구문 강조·디버거와 3D 메시/기즈모/물리 제작은 현재 범위에 포함하지 않았다.

최종 Debug/Release 전체 빌드는 모두 성공했고 CTest는 각각 13/13, 내부 검사는 각각 532/532 통과했다. 마지막 CTest 실행 시간은 Debug 33.31초/Release 28.06초이며 성능 개선 수치로 사용하지 않는다. 테스트용 World/프로젝트/저장은 새 build/ 하위 경로를 사용했다.

기존 MyEngine MCP 서버의 공식 SDK stdio Client에서 8개 도구를 확인하고 engine_capture_frame을 호출했다. 도구 서버나 패키지를 변경하지 않았으며 tools/mcp의 새 build/smoke 검증을 실행했다고 주장하지 않는다. 기본 템플릿을 build/workspace-validation/capture-*에 복사해 실제 Release 앱의 각 작업대를 40프레임 캡처했다. 모두 exit 0이고 MCP가 원본 1920×1080을 960×540으로 축소했다. 원본 화면을 임의로 재구성하지 않았다.

| 작업대 | 최종 MCP 캡처 |
|---|---|
| 2D | MyEditor-2026-10-01T14-06-23-614Z.png |
| 3D | MyEditor-2026-10-01T14-06-24-095Z.png |
| 씬 | MyEditor-2026-10-01T14-06-24-574Z.png |
| 도트메이커 | MyEditor-2026-10-01T14-06-25-110Z.png |
| Lua | MyEditor-2026-10-01T14-06-25-578Z.png |

MCP 원본은 tools/mcp/.state/captures/, 실행 기록은 build/workspace-validation/captures-release.log다. 2D 캡처를 그대로 docs/images/editor-workspace.png와 docs/guide/media/editor.png에 복사했다. 두 문서 이미지와 캡처의 SHA256 일치로 보존을 확인한다. Lua 캡처는 Player 선택/추가 진입 UI이며 코드 편집·Undo는 실제 ImGui 회귀로 구분해 확인했다. 웹 가이드는 실제 UI 스크린샷과 새 메뉴 순서를 반영했고 기존 GIF/영상 콘텐츠는 유지했다.

변경 문서의 코드 영역을 제외한 Markdown 로컬 링크 95개와 HTML 경로 12개는 누락 0개였다. git diff --check가 통과했다. 최종 문서·미디어는 기존 MyEditorContent 타깃으로 Debug/Release 실행 파일 옆에도 복사했다.
