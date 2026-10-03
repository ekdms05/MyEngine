# 13. 구조와 기능

기준: 2026-10-03, Windows/MSVC. MyEngine은 가벼운 온라인 2D 픽셀 MMORPG 제작을 목표로 하는 C++20·DirectX 11 엔진이다. 작은 프로젝트 창에서 시작하며 프로젝트를 선택한 뒤 에디터를 표시한다. 외부 아트 도구의 PNG와 엔진의 `.anim`을 기존 GUID/VFS 경로로 연결한다. 현재 지원과 최종 목표는 구분하며 [작은 작업과 완료 조건](22-2d-mmorpg-roadmap.md)에 따라 2D부터 진행한다.

## 책임과 실행 조합

| 영역 | 책임과 실제 소비자 |
|---|---|
| `core`, `reflect`, `ddc`, `plugin` | 플랫폼·고정 틱·입력·JSON·타입 메타데이터·동적 컴포넌트·플러그인 기반 |
| `rhi`, `render`, `scene` | DX11, 픽셀 타깃, 공통 깊이, ECS·변환·충돌·애니메이션·렌더 추출 |
| `asset` | VFS·pak·AssetManager·AssetDatabase·임포터·`.meta` GUID |
| `script` | Lua 5.4 C API 직접 바인딩, VM·레지스트리 참조·오류 격리·코루틴·핫 리로드 |
| `runtime`, `audio`, `ui` | 오브젝트 동작·맵 전환·세이브·대화·NPC·오디오·게임 위젯·텍스트 |
| `imgui`, `editor` | 프로젝트·문서·Undo·도킹·Inspector·에셋·모션 편집·PlayWorld·별도 게임 창 |
| `gameplay`, `net`, `persist`, `liveops`, `gameserver` | 게임·서버 기반. 개별 라이브러리 검증과 앱 통합은 구분 |
| `game/social`, `game/mmo` | 게임 고유 소셜·경제·직업·사냥 규칙. 엔진이 게임을 역참조하지 않음 |

| 실행 파일 | 현재 조합 |
|---|---|
| `MyEditor` | 프로젝트 창 → 문서 World → 에셋 GUID 해석 → 씬/모션/컴포넌트 편집 → PlayWorld 고정 틱 → 게임 창 |
| `MyGame` | 프로젝트 로컬 플레이·인증 루프백 2D/XYZ 플레이. 저장 게임 카메라와 공용 물리 사용 |
| `MyServer` | 계정·세션·권위 이동·영속·운영 루프. 공개 운영 전 신뢰 경계 완료 조건은 [14](14-development-priorities.md) |
| `paktool` | pak 생성·검사 CLI. 프로젝트별 완성 게임 배포 UI는 후속 작업 |

최근 제작한 기본 콘텐츠는 `game/starter/meadow_village`의 초원마을·집 내부 맵·레벨 1 캐릭터다. 구 `samples/` 프로젝트와 도트 제작 원본은 제품에서 제거했다. 사용자가 만든 `.dot` 파일은 새 에디터에서 열 수 없으며 삭제하거나 변환하지 않는다.

## 데이터 흐름

```text
프로젝트 .myeproj → assets/scenes/*.scene → 편집 World·문서 Undo
assets/ + .meta → AssetDatabase → assets:// VFS → AssetManager → 렌더/모션
실행 → 편집 World 복제 → Lua·조작·충돌·이벤트 → 애니메이션·변환
→ RenderExtract → HybridRenderer → 960×540 픽셀 타깃 → 별도 게임 창
```

입력은 게임 창이 활성일 때만 PlayWorld에 전달한다. 창을 닫거나 Stop을 누르면 PlayWorld를 폐기한다. Pause는 시뮬레이션만 멈추고 표시를 유지한다. 플레이는 같은 프로세스의 별도 Win32 창이며 장애 격리는 제공하지 않는다.

main 소스는 .myeproj의 inputMap을 ProjectContext와 MyGame이 같은 파서로 읽는다. core InputActions가 이름/바인딩/세기와 프레임→고정 틱 소비를 담당하고 runtime GameInputBuffer가 기본 이동/상호작용/카메라/종료를 조합한다. 파일 메뉴에서 기본/사용자 조작을 작성·제거·별도 저장하며 기존 필드 없는 프로젝트는 기본값을 사용한다. InputBindingModule을 재사용해 로컬 ObjectSystem의 Lua에 이름 기반 조회를 등록했다. GameInput의 비소유 액션 상태는 Tick 동안만 연결하고 반환/종료 전에 해제한다. 조회는 소비하지 않으며 raw InputState는 이 경로에 연결하지 않는다. Play 창은 다섯 마우스 버튼·휠과 XInput 폴링을 연결했다. 폴링은 원시 GamepadSample을 보관하고 액션은 RawGamepadAxis에서 저장 데드존을 한 번 적용한다. 기존 저수준 필터는 조회 시 유지한다. 저장 실패·짧은 탭/중복 소비·캡처 취소·합성 Win32/패드 샘플·저장 사용자 액션의 Play/로컬 Lua·앱의 등록/오류 종료를 검증한다. 입력 작성 창은 Enter 추가·포커스별 Escape와 폭에 따른 줄바꿈/스크롤·하단 버튼을 검증했다. 직접 장치/전체 키보드 탐색/실제 모니터 DPI 검수는 [25](25-input-actions.md)에 남았다.

프로젝트 생성·열기는 후보 파일과 경로를 검증한 후 기존 문서를 바꾼다. 저장 실패는 `Expected<T, Error>`로 전달한다. 에셋 가져오기는 내용을 검사하고 중복 파일을 덮어쓰지 않는다. 삭제는 사용 중인 참조를 검사하고 파일·메타를 휴지통으로 보낸다. 편집 레이아웃은 `.myeditor/`에 저장하며 버전관리에서 제외한다.

main 소스의 2D 조작은 UpdateCharacterAnimation2D로 로컬/예측/원격의 대기·걷기 요청과 방향을 조합한다. 입력/권위 facing은 방향, 실제 이동량은 모션을 결정하며 마지막 방향과 후속 클립을 유지한다. 런타임 요청 GUID는 애니메이터에 보관하고 저장하지 않는다. BindAnimations의 후속 연결은 고정 틱에서만 허용하며 표시에는 SampleAnimator를 사용해 트리거·커서·첫 이벤트를 소비하지 않는다. AnimationAsset의 같은 시트/기본·선택적 8방향 값 데이터를 GUID로 바인딩한다. 기본 전용 파일은 버전 1, 방향별 파일은 버전 2이며 직접 클립→선택적 좌측 반전→기본 순서다. 애니메이션 로드/검증 실패는 Expected로 전달해 에디터 Play를 중단하고 자동 실행을 실패시킨다. [26](26-2d-animation.md)의 저장·Undo·앱 연결과 시간 비율 보정·반전 발 피벗·연속 로컬/온라인 표현 검사와 남은 행동 전환을 구분한다.

행동 상태의 `.animstate`는 기존 bool/float/trigger·조건/전이 값을 asset 계층에서 소유하고 runtime은 호환 alias로 소비한다. 이름 기반 상태/초기 상태·GUID 참조와 전이별 진행률 정책을 검증/저장한다. AssetDatabase/VFS 로드는 각 `.anim` 파일 형식을 확인하고 삭제 검사는 행동 파일의 참조를 보호한다. SpriteAnimator.stateMachine은 상태별 시트/방향·독립 재생을 Play/MyGame에 바인딩하고 로컬 Lua 초기화 전에 기본값을 준비한다. bool moving은 공통 2D 이동량을 소비한다. 전용 행동 문서 편집/Undo는 다음이며 [26](26-2d-animation.md)에 데이터와 앱 완료 범위를 구분한다.

## 공통 계약과 실제 한계

씬의 공통 등록에는 SpriteRenderer·BillboardRenderer·MeshRenderer·Camera2D·Camera3D가 포함된다. 두 공식 앱이 같은 등록·직렬화 계약을 사용한다. GLB/임베디드 glTF는 기존 MeshImporter와 GUID resolver로 연결하며 노드 변환·glTF 재질·리깅·외부 .bin은 지원하지 않는다. billboard의 Full/YAxis/None, 발 피벗·반전과 기존 SpriteAnimator를 렌더에서 소비한다.

표준 GLB 앞면은 Z/인덱스 변환 후 DX11 clockwise 컬링으로 표시한다. 0.2.3은 반대 면을 승인한 기존 렌더 검사를 여섯 축의 앞/뒤 검사로 교체했다. 공통 렌더 경계가 지정된 메시/PNG의 실패를 반환하며 MyGame과 자동 실행 에디터는 exit 1로 종료한다. 대화형 에디터는 Play를 중단하고 하단 오류/콘솔을 통해 편집을 계속할 수 있다.

저장한 Camera3D의 위치·target·FOV·near/far·고유 이름 추종과 orbit 설정을 Play/MyGame이 사용한다. 에디터 작업 카메라는 별개이며 current가 없으면 기존 2D 카메라로 표시한다. 3D 선택/기즈모는 아직 없다. [컴포넌트 사용법](20-components.md)에 설정·검증·제한을 정리했다.

개발 main의 Camera2D는 고유 이름/부모 추종·월드 데드존/경계·픽셀 스냅·줌을 저장한다. Lua 줌/렌더 전용 흔들림·논리 좌표 변환은 로컬 Play/MyGame에 등록되고 인증 2D는 저장 설정·고정 틱/휠 입력을 소비한다. 선택 카메라의 행렬·뷰 크기·스냅 잔차를 공통으로 전달한다. 두 앱의 실제 정적 프레임 일치와 두 온라인 사용자별 2배 추종/원격 표시를 검증했다. 카메라 없는 이전 씬의 추종도 같은 도우미를 쓴다. [2D 카메라](24-2d-camera.md)에 작성 경로와 장치 입력 미검증을 기록했다.

좌표·깊이의 정본은 [02](02-rendering.md): 왼손, +Y up, PPU 48, 내부 960×540, Y/높이 기반 깊이·alpha cutout. 시뮬레이션은 고정 틱, UI·렌더는 표현 단계다.

에디터 오브젝트 Lua는 Math·ECS·log·co와 이름 기반 입력 액션이 기본 연결된다. 원시 입력 장치/audio/events/reflect/DDC/대화·저장·NPC 서비스는 별도 앱 등록이 필요하다. 라이브러리 함수가 있다는 사실을 에디터 제공 API로 설명하지 않는다. [19](19-lua-api.md)에 각 API의 범위가 있다.

네트워크·영속·게임 UI 등의 라이브러리는 유지하지만 완성 MMORPG 제작·출시 통합을 뜻하지 않는다. 게임 UI 시각 편집, 스크립트 자동 완성·중단점, 온라인 맵 전환·게임 규칙, 파일 감시 자동 반영, 범용 게임 내보내기는 남아 있다. DX11 외 백엔드와 범용 GPU timestamp/readback에는 미구현 경계가 있다.

`render`는 `scene` 공개 타입을 PRIVATE include로 소비하는 실제 결합이 남아 있다. CMake의 링크 그래프만으로 이를 해결했다고 주장하지 않는다. 성능 개선은 동일 Release 장면과 하드웨어에서 측정한 병목을 기준으로 한다.

Sol2·도트 제작·구 데모를 제거했다. 실제 사용하는 Lua VM·ImGui·디코더·FreeType는 유지한다. VM·UI·디코더 자체 재작성은 경량화 근거가 충분할 때만 검토한다.

값 타입 기반 `mye_physics3d`를 로컬 Play·서버 권위·클라이언트 예측에서 함께 사용한다. Camera3D orbit/Q·R/마우스 입력과 정적 충돌에 따른 거리 축소를 연결했다. [XYZ 플레이와 온라인](21-3d-play-and-online.md)에 동일 장면의 인증·소유권·입력 순서·예측/보정·XYZ 복제·저장 및 남은 한계를 정리했다.

2D 로컬 이동은 `ObjectSystem → PhysicsWorld2D → MoveAndSlide2D → CastMotion2D`를 사용한다. 값 타입의 `mye_physics2d`는 core만 링크하며 ECS/렌더와 분리했다. 연속 상자/원 캐스트·초기 겹침 회복·복수 벽 슬라이드를 사용한다. 모든 이동 계산과 공간 해시 검증이 성공한 뒤 물리 위치·결과·트리거 이력을 반영하고, 실패는 기존 Expected 경로로 전달한다. [씬/물리 계약](03-scene-world.md)에 오프셋·층/레이어·범위·큰 영역 검사와 정적 형상/float 정밀도 한계를 기록했다. MyGame의 --ticks는 고정 틱 한도이며 최종 틱의 렌더/캡처까지 확인할 수 있다. 이 변경은 개발 소스이며 기존 0.3.0 배포본은 그대로다.

legacy XY 서버/예측은 아직 별도 속도 적분을 사용한다. 새 인증 2D 경로는 StepMotion2D/MoveAndSlide2D를 서버·예측·재실행에 공유한다. 개발 소스의 프로젝트 온라인 앱은 활성 캐릭터로 2D/XYZ를 선택하며 2D 이동·원격 스프라이트·저장 재접속을 연결했다. 계산 경계·온라인 앱 연결·공개 운영은 각각 검증해야 한다. 기존 3D 타입/직렬화/실행 지원은 보존한다.

runtime의 LoadOnlineScene2D는 공용 장면 검증과 GatherCollisionBodies2D로 단일 캐릭터 원형·정적 충돌·속도/층·원점 스폰을 추출한다. ValidateSpawn2D는 잘못된 스폰을 수정 없이 거부한다. NetGameServer::Configure2D의 소유권/장면/층 admission·실제 입력 ack·예측과 저장/재접속은 라이브러리에서 검증했다. mye_net의 PUBLIC 의존성은 core와 값 타입 physics2d/physics3d이며 ECS/runtime/UI를 링크하지 않는다. LoadOnlineScene은 공용 로더에서 차원을 선택하고 MyServer/MyGame이 각각 권위/표현을 소비한다. 온라인 ObjectSystem/Lua를 실행하지 않으며 원격 비주얼만 생성·제거한다. tools/verify-online2d.ps1이 두 앱·픽셀·벽 충돌/ack·저장/재접속을 검사한다. [2D 플레이](23-2d-online-play.md)는 main 소스 전용이며 배포본/장치 입력과 구분한다.
