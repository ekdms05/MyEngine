# MyEngine 0.3.0

- XZ 이동·Y 점프/중력, 상자·경사·벽·천장·낮은 턱·트리거를 처리하는 공통 3D 캐릭터 물리를 추가했습니다. 로컬 Play·서버 권위·클라이언트 예측이 같은 고정 틱 계산을 사용합니다.
- 요소 추가에 캐릭터/충돌/경사/트리거 3D를 추가하고 Inspector·Undo·저장/재로드·로컬 XYZ 포털과 Lua 위치/속도/접지 API를 연결했습니다.
- 저장 가능한 게임 orbit 카메라에 Q/R·오른쪽 드래그·카메라 상대 이동·정적 장애물에 따른 거리 축소를 연결했습니다. 활성 게임 창의 입력만 받으며 첫 누름/마우스 델타는 한 틱에서 소비합니다.
- MyGame과 MyServer에 같은 정적 장면의 인증 온라인 접속을 연결했습니다. 소유권·버전·길이·세션·순서·수치를 검증하고 XYZ 예측/보정·상태 복제·저장/재접속을 처리합니다. 기존 XY 장면/저장 계약은 유지합니다.
- ZIP에 MyServer, XYZ/온라인 오프라인 가이드와 실제 화면/GIF, Godot MIT 고지를 포함합니다. 기존 개발 MCP 9개 도구에 온라인 문서 조회와 서버 틱 한도를 반영했습니다.

Godot 공식 소스의 벡터 투영을 조정해 사용하고 CharacterBody3D·SpringArm3D·복제 권위 경계를 참고했습니다. Godot 런타임·새 외부 물리/네트워크 라이브러리는 추가하지 않았습니다. [구현 근거와 사용법](https://github.com/ekdms05/MyEngine/blob/v0.3.0/docs/21-3d-play-and-online.md)을 확인하세요.

검증: 기존 네이티브 테스트 **551개**의 Debug·Release 회귀, 기존 서버/프로젝트 검사, MCP build·smoke, 실제 MyServer와 두 MyGame 프로세스의 인증/복제/저장 및 MyEditor 로컬 Play 캡처. 라이브러리 검사는 점프·경사·벽·예측/소유권·재접속을 포함합니다. 저장 시점 GIF·headless 실행은 실제 키/마우스/패드·IME 또는 새 PC 검증을 대신하지 않습니다.

지원 범위는 정적 축 정렬 상자/+Z 경사와 동일 장면의 **루프백 개발 전송**입니다. 임의 메시/회전 물리·동적 강체·NPC 내비게이션·카메라 상대 8방향 에셋 자동 매핑·온라인 포털/게임 규칙/HUD/채팅·AOI·암호화·게임 내보내기는 미지원입니다. 스냅샷 24개는 실측 동접 보장이 아닙니다. 공개 인터넷 MMORPG 운영 완료를 뜻하지 않습니다.

Windows 10/11 x64·DirectX 11·[Microsoft Visual C++ v14 x64 런타임](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist)이 필요합니다. ZIP 전체를 새 폴더에 풀고 MyEditor.exe를 실행하세요. 기존 프로젝트는 백업 후 열고 사용자 원본/GUID는 유지하세요. 나눔스퀘어라운드 OFL 및 실제 사용 의존성 고지는 licenses에 포함됩니다.
