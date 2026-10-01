# 13. 구조와 기능

기준: 2026-10-02, Windows/MSVC. MyEngine은 C++20·DirectX 11 기반 2.5D 픽셀아트 엔진이다. 작은 프로젝트 창에서 시작하며 프로젝트를 선택한 뒤 에디터를 표시한다. 외부 아트 도구의 PNG와 엔진의 `.anim`을 기존 GUID/VFS 경로로 연결한다.

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
| `MyGame` | 명시한 프로젝트를 불러오는 로컬 플레이어. 구 타이틀 데모와 온라인 표현 조합은 제거 |
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

프로젝트 생성·열기는 후보 파일과 경로를 검증한 후 기존 문서를 바꾼다. 저장 실패는 `Expected<T, Error>`로 전달한다. 에셋 가져오기는 내용을 검사하고 중복 파일을 덮어쓰지 않는다. 삭제는 사용 중인 참조를 검사하고 파일·메타를 휴지통으로 보낸다. 편집 레이아웃은 `.myeditor/`에 저장하며 버전관리에서 제외한다.

## 공통 계약과 실제 한계

씬의 공통 등록에는 SpriteRenderer·BillboardRenderer·MeshRenderer·Camera3D가 포함된다. 두 공식 앱이 같은 등록·직렬화 계약을 사용한다. GLB/임베디드 glTF는 기존 MeshImporter와 GUID resolver로 연결하며 노드 변환·glTF 재질·리깅·외부 .bin은 지원하지 않는다. billboard의 Full/YAxis/None, 발 피벗·반전과 기존 SpriteAnimator를 렌더에서 소비한다.

표준 GLB 앞면은 Z/인덱스 변환 후 DX11 clockwise 컬링으로 표시한다. 0.2.3은 반대 면을 승인한 기존 렌더 검사를 여섯 축의 앞/뒤 검사로 교체했다. 공통 렌더 경계가 지정된 메시/PNG의 실패를 반환하며 MyGame과 자동 실행 에디터는 exit 1로 종료한다. 대화형 에디터는 Play를 중단하고 하단 오류/콘솔을 통해 편집을 계속할 수 있다.

저장한 Camera3D의 위치·target·FOV·near/far·고유 이름 추종 오프셋을 Play/MyGame이 사용한다. 에디터 작업 카메라는 별개이며 current가 없으면 기존 2D 카메라로 표시한다. [컴포넌트 사용법](20-components.md)에 설정·검증·제한을 정리했다. 3D 선택/기즈모와 XZ 지면·Y 높이의 3D 물리는 아직 없다.

좌표·깊이의 정본은 [02](02-rendering.md): 왼손, +Y up, PPU 48, 내부 960×540, Y/높이 기반 깊이·alpha cutout. 시뮬레이션은 고정 틱, UI·렌더는 표현 단계다.

에디터 오브젝트 Lua는 Math·ECS·log·co만 기본 연결된다. audio/input/events/reflect/DDC/대화·저장·NPC 모듈은 별도 앱 등록이 필요하다. 라이브러리 함수가 있다는 사실을 에디터 제공 API로 설명하지 않는다. [19](19-lua-api.md)에 각 API의 범위가 있다.

네트워크·영속·게임 UI 등의 라이브러리는 유지하지만 완성 MMORPG 제작·출시 통합을 뜻하지 않는다. 게임 UI 시각 편집, 스크립트 자동 완성·중단점, 서버와 클라이언트의 공통 충돌·맵 계약, 파일 감시 자동 반영, 범용 게임 내보내기는 남아 있다. DX11 외 백엔드와 범용 GPU timestamp/readback에는 미구현 경계가 있다.

`render`는 `scene` 공개 타입을 PRIVATE include로 소비하는 실제 결합이 남아 있다. CMake의 링크 그래프만으로 이를 해결했다고 주장하지 않는다. 성능 개선은 동일 Release 장면과 하드웨어에서 측정한 병목을 기준으로 한다.

Sol2·도트 제작·구 데모를 제거했다. 실제 사용하는 Lua VM·ImGui·디코더·FreeType는 유지한다. VM·UI·디코더 자체 재작성은 경량화 근거가 충분할 때만 검토한다.
