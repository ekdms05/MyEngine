# MyEngine 0.4.0

**2D 제작·애니메이션·로컬 게임 UI와 인증된 2D 이동을 Windows 배포본에 포함했습니다.** 기존 0.3.0은 그대로 보존합니다. 새 ZIP 전체를 별도 폴더에 풀고 `MyEditor.exe`를 실행하세요. 기존 프로젝트는 백업 후 여세요.

- 연속 상자/원 충돌을 로컬 Play·서버 권위·클라이언트 예측에서 공유합니다. 동일 정적 2D 장면의 두 인증 사용자, 원격 캐릭터 표시, 처리된 입력 확인, XY/층 저장·재접속을 연결했습니다.
- 씬에 저장하는 2D 카메라의 추종·경계·데드존·픽셀 스냅·줌·흔들림과 프로젝트 입력 액션 설정을 제공합니다. Lua에서 이름으로 눌림·유지·축을 읽습니다.
- 외부 PNG 시트에 8방향 클립·피벗·프레임 이벤트를 작성하고 행동 상태/조건/전이를 저장합니다. 현재 행동 조회, 남은 trigger 취소, 기본 2D 조작 잠금으로 프로젝트의 공격·피격·사망 흐름을 연결할 수 있습니다. 피해·HP·쿨다운은 게임 프로젝트가 소유합니다.
- 게임 창 하단의 상호작용/Message 표시와 `.ui` 문서 작성·Undo/Redo·저장·미리보기를 제공합니다. 로컬 Play/MyGame의 Lua HUD·버튼·포커스·모달·TextInput 제출을 연결했습니다.
- 에디터·플레이어·서버, 초원마을 템플릿, 오프라인 가이드/이미지/GIF, HUD 예제, 나눔스퀘어라운드와 필수 라이선스 고지를 함께 제공합니다. 픽셀 그림 제작·리깅은 외부 도구에서 진행합니다.

[온라인 2D 사용법](https://github.com/ekdms05/MyEngine/blob/v0.4.0/docs/23-2d-online-play.md), [모션과 행동](https://github.com/ekdms05/MyEngine/blob/v0.4.0/docs/26-2d-animation.md), [게임 UI와 Lua](https://github.com/ekdms05/MyEngine/blob/v0.4.0/docs/27-game-ui.md)를 확인하세요. ZIP의 `docs/guide/index.html`은 인터넷 없이 열 수 있습니다.

검증 범위: 기존 네이티브 테스트 **611개**의 Debug/Release 회귀, MCP build·smoke, 실제 MyServer/두 MyGame의 2D 인증·충돌·저장/재접속, 입력 액션, 방향 모션·연속 프레임·행동 중단·맵 수명, 로컬 UI 문서와 캡처를 검사합니다. Windows 배포 워크플로는 Release 테스트와 앱 검사를 DX11 WARP로 실행합니다. 소프트웨어 렌더 검사는 GPU 성능·실제 키보드/패드·IME 후보창·새 PC 검수를 대신하지 않습니다.

온라인 지원은 **동일 정적 장면의 인증된 루프백 개발 전송**입니다. 온라인 클라이언트는 프로젝트 Lua를 실행하지 않습니다. 온라인 전투/포털/HUD/채팅, 관심 영역, 암호화, 공개 서비스 운영, 게임 내보내기 설치 프로그램은 미완료입니다. 로컬 게임 UI 지원을 온라인 UI 지원으로 해석하지 마세요. 기존 정적 3D/XYZ 경로와 해당 라이선스 고지는 유지합니다.

Windows 10/11 x64·DirectX 11·[Microsoft Visual C++ v14 x64 런타임](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist)이 필요합니다. 파일 무결성은 ZIP의 SHA-256 파일과 `release-manifest.json`으로 확인하세요. 사용자 프로젝트·로컬 작업 기록·개발 도구 설정은 배포본에 포함하지 않습니다.
