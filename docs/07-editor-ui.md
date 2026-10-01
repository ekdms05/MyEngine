# 07. MyEditor 사용 구조와 편집 계약

MyEditor는 공용 코어·씬·렌더 위에 `mye_editor`와 ImGui를 조합한 Windows 실행 파일이다. Release 위치는 `build/dev/apps/editor/Release/MyEditor.exe`, Debug 위치는 `build/dev/apps/editor/Debug/MyEditor.exe`다. 저장소 루트에서 실행한다.

## 화면과 작업 흐름

| 기능 | 현재 역할 | 소스 |
|---|---|---|
| EditorApp·EditorModule | 도킹 셸·메뉴·단축키·수명 | [EditorApp.cpp](../engine/editor/src/EditorApp.cpp), [EditorModule.cpp](../engine/editor/src/EditorModule.cpp) |
| Hierarchy·Selection | 엔티티 목록·선택 | [Selection.h](../engine/editor/include/mye/editor/Selection.h), [BuiltinPanels.h](../engine/editor/include/mye/editor/BuiltinPanels.h) |
| Inspector | 리플렉션 기반 필드 편집 | [Inspector.cpp](../engine/editor/src/Inspector.cpp) |
| Viewport | 씬 표시·좌표 변환·조작 경계 | [Viewport.h](../engine/editor/include/mye/editor/Viewport.h) |
| Asset Browser·Console | 에셋 탐색·실행 진단 | [BuiltinPanels.h](../engine/editor/include/mye/editor/BuiltinPanels.h) |
| TileEditing·AnimEditing | 타일·높이/경사·애니 클립/이벤트 편집 | [TileEditing.h](../engine/editor/include/mye/editor/TileEditing.h), [AnimEditing.h](../engine/editor/include/mye/editor/AnimEditing.h) |

현재 셸은 어두운 바탕과 주황 강조를 사용한다. 엔티티·파일·확장 패널 이름이 중복되어도 ImGui ID 스코프가 충돌하지 않게 한다. 해상도·DPI·전체 상호작용의 검증 범위는 실제 실행 결과로 기록한다.

## 편집 명령과 문서

[CommandStack](../engine/editor/include/mye/editor/CommandStack.h)과 [Command](../engine/editor/include/mye/editor/Command.h)은 Undo/Redo·구조 변경·필드 변경을 관리한다. 드래그·브러시 같은 연속 입력은 사용자 작업 단위로 묶고, UI 프레임마다 독립 변경을 쌓지 않는다.

[ProjectContext·Document](../engine/editor/include/mye/editor/Project.h)는 프로젝트·문서·dirty 상태를 소유하고 공용 씬 직렬화를 사용한다. 라이브러리의 씬 저장 왕복과 앱 메뉴의 실제 파일 열기는 별개다. 현재 `ProjectContext::Open`·OpenScene에는 실제 프로젝트 해석/로드를 연결할 작업이 남아 있다. 완성된 파일 열기·최근 프로젝트·자동 복구 워크플로로 설명하지 않는다.

저장 실패 시 기존 문서·dirty 상태·사용자 데이터를 보존하고 성공처럼 진행하지 않는다. 실제 완료 시나리오는 열기 → 편집 → 저장 → 재열기 → 플레이이며 [개선 목록](14-development-priorities.md)에서 관리한다.

## PlayWorld

[PlayMode](../engine/editor/include/mye/editor/PlayMode.h)는 편집 World의 스냅샷으로 별도 PlayWorld를 만든다. Play 종료로 임시 게임 상태가 편집 문서를 덮어쓰지 않게 한다. 월드별 이벤트·선택·참조 수명을 함께 정리한다.

현재 UI는 플레이 중 NewScene·Save를 제한하고 공통 NewScene 경계에서도 편집 보호를 적용한다. PlayWorld가 있다는 사실을 MyGame과 동일한 물리·Lua·게임플레이 조합 완료로 해석하지 않는다. 동일 플레이 시스템 연결은 별도 제품 통합 항목이다.

## 확장과 게임 UI의 경계

[ExtensionRegistry](../engine/editor/include/mye/editor/ExtensionRegistry.h)는 패널·메뉴·툴바·Inspector 확장 등록을 제공한다. 기즈모·오버레이·뷰포트 툴·설정 페이지는 실제 구현·소비 범위를 확인하고 등록 시그니처만으로 동작 완료를 선언하지 않는다.

게임 UI는 `engine/ui`의 Widget·UiSystem·텍스트 경로로 처리한다. 에디터의 ImGui 레이아웃을 게임 HUD·채팅 화면으로 그대로 쓰지 않는다. MCP 개발 도구는 [08](08-mcp.md)에 있으며 에디터 원격 명령·Lua REPL 제어는 현재 연결된 지원 기능이 아니다.

## 검증

에디터 직렬화·선택·커맨드·Undo·PlayWorld·타일/애니 편집은 기존 자체 테스트로 검사한다. 뷰포트는 MyEditor headless 캡처로 확인할 수 있으나 전체 셸·다양한 DPI·파일 대화상자·마우스 워크플로의 검증을 대신하지 않는다. 현재 앱 연결 범위는 [13](13-architecture-and-features.md), 완료 조건은 [14](14-development-priorities.md)에 있다.
