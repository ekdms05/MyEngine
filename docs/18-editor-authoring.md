# 18. 도트·모션 제작과 에디터 UI

기준일: **2026-10-01**. [게임 제작 가이드](guide/index.html)는 결과→실행 순서→이유·근거 순서의 오프라인 페이지다. **도움말 → 게임 제작 가이드**에서 실행 파일 옆 `docs/guide/index.html`을 연다. 빌드에는 docs와 기본 프로젝트를 복사한다.

## 구현과 소비 경로

| 작업 | 실제 경로 | 경계 |
|---|---|---|
| 메뉴·상태 | EditorApp 호스트 높이에서 상태 영역 제외 → 별도 DrawStatusBar | 한 줄·툴팁·콘솔. 긴 오류가 메뉴 높이를 늘리지 않음 |
| 작업대·도킹 | 상단 텍스트 → SelectWorkspace → 중앙 패널, 주변 5영역 탭 → PanelManager | 2D/3D는 같은 씬의 카메라 전환. 문서·Undo 보존, session.json에 작업대 저장 |
| 씬 요소 추가 | 씬/Hierarchy → 검색 창 → CreateSceneElement → 기존 생성/컴포넌트/필드 트랜잭션 | 지원하는 8개 ECS 조합만 표시. Redo는 생성 핸들을 복구하고 부모 Children을 유지 |
| Lua 작업대 | 선택 오브젝트 → DrawObjectLua → luaSource PropertyEdit → 씬 저장/Play | Inspector와 같은 편집 경계. Play 중 읽기 전용 |
| 기본 이동 | Inspector → SetupCharacterMovement → 기존 AddComponent/PropertyEdit 트랜잭션 → ObjectSystem 고정 틱 | 최상위 오브젝트·활성 조작 캐릭터 1명. 기존 값 보존, 새 Collider만 발밑 박스 설정 |
| 도트 원본 | Panel → DotDocument → 문서 CommandStack → SaveDot → 공용 WriteJsonFile | assets 안의 .dot, 실패 시 원본·dirty 보존, 다른 기존 파일 덮어쓰기 확인 |
| 참조 이미지 | PNG 선택/에셋 드롭 → 크기 사전 검사 → 기존 TextureImporter::DecodePng(false) | straight RGBA, 불변 참조를 원본에 포함. 한글 경로 지원 |
| 캔버스 | DotCanvasView → anchor zoom/pan/fit → 클리핑된 동일 색 가로 구간 그리기 | 0.25–64배, 1:1·그리드·이전 프레임. 맞춤은 도킹 크기 변화에 대응 |
| 픽셀 편집 | 브러시·지우개·스포이트·연속 선·채우기·영역 선택 → 획 단위 Undo | 뼈 생성 뒤 캔버스 크기 변경 제한 |
| 파츠 리깅 | 선택 영역 → AddDotBone → 부모·피벗·기본 자세 → EvaluateDotPose/DotWorldPoses | 강체 위치·회전, 최단 각도 선형 보간. 겹치지 않는 영역 권장, 마지막 뼈부터 삭제 |
| 모션 | 프레임 생성/복제/삭제·시간 → 구간/방향/반복/다음 모션 → 기존 ClipPlayback | 삭제 시 키·구간 재배치. 다음 지정 시 이전 모션은 한 번 재생 |
| 내보내기 | RenderDotFrame → PNG sheet + 기존 .anim + .meta → DB 새로고침 → 씬 Animator/Controller | 새 GUID 디렉터리로 기존 파일 보존. 실패 시 해당 출력의 생성 파일만 회수 |
| Play 연결 | .anim.nextAnimation → EditorModule resolver → 종료 커서 → 다음 GUID 바인딩 | 이동 상태 변화 때만 진입 모션 지정하여 후속 모션을 매 틱 재시작하지 않음 |

프로젝트가 문서, 문서가 픽셀·키·Undo를 소유한다. 패널에는 도구·배율·선택·재생·파생 미리보기만 둔다. 참조는 불변이므로 Undo 사이에 공유한다. 원본 변경은 미리보기·내보내기를 무효화한다. 모션의 씬 지정은 씬 Undo, 픽셀·키 변경은 도트 Undo에 기록한다. 기존 `.anim`은 다음 GUID 필드가 없어도 읽는다.

## 근거 있는 제한

- 버전 1 JSON의 RGBA 8자리 hex를 사용하고 파싱 전에 크기·수·범위를 제한한다. 기존 64 MiB JSON I/O를 재사용한다. 프레임 전체 4M, 참조 포함 6M 픽셀 제한으로 직렬화 공간을 남긴다.
- 캔버스 1–512, 프레임 1–64, 참조 한 변 ≤4096/총 ≤4M, 뼈 ≤64, 모션 ≤32, 시간 0.01–10초. 부모는 앞의 뼈만 참조한다. 중복 키·범위·비유한 수·없는 다음 모션은 거부한다.
- Undo는 최근 24개 문서 스냅샷이다. 최대 프레임 예산에서는 before/after 이력이 약 768 MiB까지 커질 수 있다. 참조는 복사하지 않는다. 긴 편집에서 메모리·응답 시간이 병목으로 측정되면 픽셀 diff로 바꾼다. 측정 없이 풀·스레드를 추가하지 않았다.
- 파츠는 역변환한 픽셀 중심의 nearest 샘플링과 straight alpha 합성이다. 메시 변형·IK·3D 리깅·GIF 입력·다중 레이어는 미구현이다. 단일 이미지에서 부위를 자르면 회전 경계가 드러날 수 있으므로 가려진 픽셀을 보완한다.
- 다음 모션 자원 해석은 **MyEditor Play** 조합에 연결됐다. MyGame이 같은 파일을 자동 소비한다고 표시하지 않는다. 다른 앱의 콘텐츠 조합은 별도 완료 조건이다.
- 새 기본 프로젝트의 `assets/sprites/novice.dot`은 기존 novice.png 첫 프레임을 제작 API로 nearest 축소·배치한 편집 예제다. 기존 PNG·씬·GUID를 교체하지 않는다. 몸통·머리·다리 4영역과 idle/walk/greet 모션이 있으며 8방향/완성 보행 아트는 아니다.

## 공식 벤치마킹

2026-10-01 확인. 타사 게임·에셋·아이콘·코드를 복사하지 않고 작업 구조를 현재 계약에 맞췄다.

| 근거 | 채택과 제외 이유 |
|---|---|
| [Godot theme source](https://github.com/godotengine/godot/blob/084a2caa05119b625a99b6b51d44b459a26362de/editor/themes/editor_theme_manager.cpp) | 중립 어두운 표면·선택 강조·역할별 대비. ImGuiSkin 한곳에 회색/파랑 정의. 전체 테마 엔진 대신 직접 그린 벡터 아이콘+텍스트로 외부 폰트/아이콘 의존성 제거 |
| [Godot AnimationPlayer editor](https://github.com/godotengine/godot/blob/084a2caa05119b625a99b6b51d44b459a26362de/editor/animation/animation_player_editor_plugin.cpp) | 모션 추가/삭제/이름/다음 모션의 Undo. Godot 타입을 도입하지 않고 현재 CommandStack 재사용 |
| [Godot 4.7 문서 입구](https://docs.godotengine.org/en/4.7/index.html) | 작업별 탐색·검색·오프라인 제공. 서버·검색 SaaS 없이 정적 HTML/CSS/JS로 현재 기능만 설명 |
| [PixelOver 뼈 제작](https://docs.pixelover.io/ko/tutorials/bones_animation/) | 영역 분리·부모 계층·기본 자세/포즈·키·재생·내보내기. IK/메시에는 별도 계약·검증이 필요하므로 강체 파츠 범위 유지 |
| [PixelOver 그리기](https://docs.pixelover.io/ko/tutorials/drawing/), [애니메이션](https://docs.pixelover.io/ko/manual/animation/) | 픽셀 도구·선택·모션/시간/키 분리. 전 기능 복제·전용 형식은 도입하지 않음 |

Godot HEAD 확인값 `084a2caa05119b625a99b6b51d44b459a26362de`의 theme 파일은 고정 URL로 재확인했다. 과거 권한 거부된 Godot 스프라이트 애니메이션 페이지는 우회하지 않았다.

## 자료와 검증의 범위

guide/media의 editor/dot/play/3d PNG는 공식 MCP SDK→기존 engine_capture_frame의 실제 MyEditor 화면이다. UI 원본 1920×1080은 기존 MCP가 960×540으로 축소한다. 걷기 MP4/GIF는 제작 API가 내보낸 motion-1.anim과 PNG 셀을 프레임 순서로 인코딩했다. 8프레임×0.15초=1.2초, nearest 2배·중립 배경·음성 없음. 마우스 제작 과정을 녹화한 영상으로 표시하지 않는다. 네이티브 video controls·포스터·GIF 다운로드를 제공하고 자동 재생은 하지 않는다.

기존 C++ 테스트의 저장 실패/경로/버전/Undo, 리깅/각도/부모, 맞춤 배율, 프레임 재배치, 내보내기/후속 GUID, 이동 구성/상태를 검증한다. 라이브러리 검증과 앱 캡처를 구분한다. 네이티브 입력 RPC는 미구성이다. 브라우저 보안 정책이 로컬 가이드 접근을 사용자 권한 거부로 차단하여 화면·키보드·브라우저 비디오 호환성 검증을 우회하지 않았다. 파일/링크/미디어 검사는 브라우저 동작 증거를 대신하지 않는다.

실행한 검증과 잔여 조건은 [작업 기록](16-foundation-worklog.md), [우선순위](14-development-priorities.md)에 있다.
