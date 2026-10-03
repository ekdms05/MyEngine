# 07. 에디터

프로젝트 창에서 이름·폴더·템플릿을 선택하거나 `.myeproj`를 열면 에디터가 표시된다. 시작 인자를 주면 프로젝트 창을 건너뛴다. 잘못된 프로젝트는 오류를 표시하고 프로젝트 창에 남는다.

## 작업대와 배치

왼쪽 상단 메뉴, 중앙 상단 **2D·3D·Lua**, 오른쪽 상단 **재생·일시정지·중지**를 사용한다. 작업대는 경계 없는 텍스트 선택이며 활성 항목만 강조한다. 좁은 창에서는 조작 버튼을 다음 행에 표시한다.

주변 탭 영역은 왼쪽 위 하이어라키, 왼쪽 아래 에셋, 중앙 아래 콘솔, 오른쪽 위 인스펙터, 오른쪽 아래 애니메이션이다. 씬 만들기는 하이어라키와 파일 메뉴에 있다. 중복 씬 작업대와 도트 제작 작업대는 제거했다.

하이어라키의 **씬 만들기**에서 2D/3D 시작 보기를 선택한다. 이는 같은 ECS 씬의 카메라 시작 모드이며 다른 직렬화 형식이 아니다. **+ 추가**는 검색·넉넉한 행 간격·선택 설명·추가/취소 순서로 구성한다. 캐릭터는 최상위에 만들고 한 씬에 활성 조작 캐릭터 하나만 허용한다.

Inspector는 컴포넌트 설명과 필드별 툴팁을 제공한다. 기본 위치·이미지·조작은 열어 두고 다른 컴포넌트는 펼쳐 확인한다. 헤더 우클릭으로 제거하고 Undo로 복원한다. [컴포넌트 사용법](20-components.md)을 참고한다.

에디터의 기본 글꼴은 **나눔스퀘어라운드 Regular**다. 한글 11,172자를 지원하는 공식 TTF와 SIL OFL 1.1 전문을 함께 제공한다. 폰트가 없는 개발 실행에서는 설치된 맑은 고딕을 사용하며, 일본어·중국어는 설치된 Windows 언어 폰트로 보완한다. 게임 프로젝트의 런타임 텍스트 폰트 선택과는 별개다.

## 파일과 문서

씬·모션 문서는 별도의 데이터·Undo 스택을 소유한다. Ctrl+S는 포커스한 문서, 프로젝트 저장은 이름이 정해진 모든 문서와 메타를 저장한다. 미저장 문서를 가진 프로젝트를 바꾸려면 명시적으로 폐기를 확인한다. 실패한 후보 열기는 기존 프로젝트를 유지한다.

에셋 브라우저의 폴더/파일 또는 빈 영역을 우클릭하여 가져오기·폴더 생성·탐색기 열기·휴지통 이동을 사용한다. PNG·`.anim`·Lua·WAV 내용과 프로젝트 내부 경로를 검사한다. 중복 파일은 덮어쓰지 않는다. 참조 중인 파일은 제거를 거부한다. 원본 이동으로 GUID를 바꾸는 기능은 제공하지 않는다.

도트·리깅은 외부 제작 도구를 사용한다. 애니메이션 패널은 **미리보기·클립·시트·이벤트** 탭으로 구성한다. 미리보기에서 재생·속도·타임라인 시간/순서를, 클립에서 저장 경로·반복·방향·선택 오브젝트 지정·후속 모션을, 시트에서 PNG·그리드·영역·피벗을, 이벤트에서 이름·인자를 편집한다. 긴 필드는 입력 위에 라벨을 두고 패널 너비에 맞춘다. `.anim`으로 저장한다. 현재 공식 2D 경로는 외부 본 모션을 PNG 프레임으로 베이크하여 연결하며 런타임 본/가중치/IK 임포트는 지원하지 않는다. [베이크·발 피벗·이동 검수 절차](26-2d-animation.md)를 따른다.

## 플레이와 수명

main 소스의 **파일 → 입력 설정…**에서 조작 이름을 추가/제거하고 키·마우스·패드 바인딩을 편집·저장한다. 사용자 액션은 Lua에서 이름으로 조회한다. Play를 중지한 상태에서만 저장하며 창 닫기/취소는 저장 전 편집을 버린다. 씬 저장/Undo와는 별개 설정이다. Play와 MyGame의 고정 틱은 같은 GameInputBuffer를 사용한다. [입력 설정](25-input-actions.md)에 기본값·Lua 조회/수명·장치 입력의 남은 범위를 설명한다.

재생은 현재 편집 World를 복제하고 **별도 게임 창**에서 표시한다. 조작·Lua·충돌·맵 이동·모션은 같은 고정 틱을 사용한다. 게임 창에 포커스가 있을 때 프로젝트 입력 설정을 받으며 기본값은 WASD/방향키/E다. 에디터 포커스·텍스트 편집 입력과 섞지 않는다. Pause는 시뮬레이션을 멈추고, Stop 또는 게임 창 닫기는 PlayWorld를 버린다. 실행 중 변경을 편집 씬으로 저장하지 않는다.

별도 창은 에디터와 같은 프로세스와 DX11 디바이스를 공유한다. 게임 창은 자체 EventBus·입력 상태·스왑체인·픽셀 타깃을 소유한다. Raw Input은 에디터 등록을 유지하고 게임 창은 물리 키 매핑을 재사용한다. 종료 시 게임 창의 GPU 자원과 메시지 훅을 디바이스보다 먼저 해제한다.

## 개발 CLI

```powershell
build/dev/apps/editor/Release/MyEditor.exe --project game/starter/meadow_village/project.myeproj
build/dev/apps/editor/Release/MyEditor.exe --project <manifest> --workspace lua --select novice
build/dev/apps/editor/Release/MyEditor.exe --project <manifest> --headless --frames 3 --dump <capture.bmp>
build/dev/apps/editor/Release/MyEditor.exe --project <manifest> --import-asset <source.png> --asset-destination environment/tree.png --headless --frames 1
```

`--workspace`는 `2d`, `3d`, `lua`다. `--play`는 플레이로 시작한다. 창이 있는 플레이 캡처는 게임 창, 그 외 GUI 캡처는 에디터, headless는 월드 타깃을 담는다. `--add-element-dialog`는 추가 창 캡처용이다. headless 렌더도 DX11 장치가 필요하다.

main의 Play 타깃에는 근처 상호작용 안내와 Message도 합성된다. 별도 Play 창·에디터 Play 뷰포트·headless Play는 같은 텍스트 경로를 사용한다. 기존 ImGui 전용 Message 출력은 제거했다. 메뉴/게임 입력을 선점하지 않는 읽기 전용 안내이며 [글꼴·줄바꿈·수명·미완료 HUD 범위](06-runtime-systems.md#게임-창의-안내와-message--main-소스)를 확인한다. 기존 설치 0.3.0에는 이 게임 창 연결이 없다.

## 공식 벤치마킹 근거

확인: 2026-10-02. 공식 문서의 동작을 참고했으며 Godot 코드를 복사하지 않았다.

| 공식 자료 | 채택 | 차이·보류 |
|---|---|---|
| [First look at the editor](https://docs.godotengine.org/en/stable/getting_started/introduction/first_look_at_the_editor.html) | 프로젝트 선택 후 에디터, 중앙 작업대·도크·사용 설명 | 필요한 3개 작업대와 현재 ECS 유지; 온라인 라이브러리·최근 목록 미제공 |
| [Create Dialog source](https://github.com/godotengine/godot/blob/4.5/editor/gui/create_dialog.cpp) | 검색 → 선택 → 설명 → 생성/취소 | 현재 등록된 요소만 표시; 노드 객체 모델을 복제하지 않음 |
| [Game embedding](https://docs.godotengine.org/en/latest/tutorials/editor/game_embedding.html) | 별도 표시 창과 일시정지 중 렌더 유지 | latest 문서는 불안정 버전. Godot의 별도 프로세스 대신 기존 PlayWorld를 쓰는 동일 프로세스 창; 장애 격리 보류 |
| [File paths](https://docs.godotengine.org/en/stable/tutorials/io/data_paths.html) | 프로젝트 데이터와 로컬 편집 상태 구분 | 기존 GUID/assets:// 유지 |
| [File system](https://docs.godotengine.org/en/stable/tutorials/scripting/filesystem.html), [FileSystem dock source](https://github.com/godotengine/godot/blob/master/editor/docks/filesystem_dock.cpp) | 폴더·파일 우클릭과 삭제 의존성 확인 | 프로젝트 GUID와 메타 유지; 폴더 이동·이름 변경 보류 |
| [Editor fonts source](https://github.com/godotengine/godot/blob/12c17c187e88efa23e6bbd6689630e256eca6523/editor/themes/editor_fonts.cpp) | 기본 UI 글꼴과 문자권별 보완 폰트 구분 | Godot의 Inter·Noto/Droid 조합 대신 사용자 요청의 나눔스퀘어라운드와 기존 ImGui 시스템 폰트 병합을 사용. 별도 폰트 엔진을 추가하지 않음 |
| [Camera3D](https://docs.godotengine.org/en/stable/classes/class_camera3d.html), [SpriteBase3D](https://docs.godotengine.org/en/stable/classes/class_spritebase3d.html) | 저장 카메라의 current/FOV/near/far, 빌보드 모드·cutout | 2026-10-02 확인. 기존 ECS·LH·PPU48·depth 경로를 재사용. 카메라는 target/이름 추종 오프셋, current 중복 오류. Godot API·자동 카메라 선택·물리 구현을 복제하지 않음 |
| [BaseMaterial3D CullMode](https://docs.godotengine.org/en/stable/classes/class_basematerial3d.html#enum-basematerial3d-cullmode) | 기본 백페이스 컬링으로 모델 외부를 표시 | 2026-10-02 확인. RH glTF→LH/DX11 투영의 앞면을 수정하고 여섯 축 앞/뒤를 검사. Godot의 RH 객체 모델·양면 재질 옵션은 추가하지 않음 |

0.2.3의 메시 수정 근거는 [glTF instantiation의 정면 규약](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#instantiation)과 [DX11의 렌더 타깃 앞면 설정](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_rasterizer_desc)이다. 표준 위향 면을 위에서 표시·아래에서 컬링하며 원본은 변경하지 않는다. 지정한 메시/PNG의 오류는 하단 상태/콘솔에 씬·오브젝트·GUID를 안내하고 Play를 중단한다. 자동 실행에서는 exit 1을 전달한다.

Windows 삭제는 [IFileOperation](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-ifileoperation)의 STA·휴지통·Undo·취소 결과를 사용한다. 무조건 재귀 삭제하는 경로를 만들지 않았다.

게임 UI 캔버스/타입별 편집 확장·스크립트 자동 완성/디버깅·복구 정책·프로세스 격리·게임 배포 UI는 [개발 우선순위](14-development-priorities.md)에 남긴다. 현재 화면은 [제작 가이드](guide/index.html)에서 확인한다.

요소 추가에는 캐릭터 3D·충돌 3D·경사 3D·트리거 3D가 있다. Play의 Space 점프·Q/R 회전·오른쪽 마우스 드래그는 활성 게임 창에만 전달한다. 첫 누름과 마우스 델타는 한 고정 틱에서만 소비한다. 에디터 Play는 로컬이며 온라인 연결은 별도 MyGame CLI에서 설정한다.

## UI 문서 작성 · main 소스

창 → 게임 UI에서 새 `.ui`를 만들거나 에셋 더블 클릭/우클릭으로 연다. 위젯 트리·앵커 프리셋·속성 값을 편집 → 적용 → Undo/Redo → 프로젝트 assets에 저장 → GameUi.document 지정 → Stop/재실행 순서다. 적용 전 속성은 문서에 임시 보관하며 프로젝트 미저장 확인·저장 실패·패널 재생성에서 유지한다. 기존 파일/GUID는 새 문서의 저장으로 덮어쓰지 않는다. 미리보기는 Play와 같은 UI/PNG/한글 렌더 경로를 사용하며 프로젝트 Lua를 실행하지 않는다. CLI `--ui assets/ui/hud.ui`와 MCP engine_run의 args로 같은 패널을 열 수 있다.

[UI 문서 가이드](27-game-ui.md)에 허용 속성·저장/닫기·오류/지원 범위를 기록한다. 로컬 버튼 클릭·키보드 포커스/모달 차단은 연결했다. TextInput도 추가/저장하며 문자/조합·제출을 로컬 두 앱에 연결했다. 캔버스 드래그/재부모화·전용 타입별 속성 UX·실제 IME 확정/후보창·패드 UI·온라인·설치 0.3.0은 별도다. Release 합성 네이티브 입력6건은 통과했다. 라이브러리/ImGui 이벤트 검사, 실제 앱 프레임과 직접 장치/DPI 검수를 구분한다.
