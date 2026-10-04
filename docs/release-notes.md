# MyEngine 0.5.0

인증된 2D 기본 공격·상호작용 포털과 게임 폴더 내보내기 CLI를 제공합니다. 기존 버전은 보존합니다. ZIP 전체를 새 폴더에 풀고 MyEditor.exe를 실행하세요.

- MyServer가 플레이어 공격의 거리·벽·층·HP·쿨다운을 검증합니다. HP를 같은 맵의 플레이어에게 복제하며 중복 요청은 한 번만 판정합니다. 사망한 플레이어는 이동/공격할 수 없고 재접속으로 부활하지 않습니다.
- ScenePortal과 InteractionTarget으로 맵을 연결합니다. 클라이언트는 도착 씬·에셋을 먼저 준비하고, 서버가 고유 named spawn과 충돌을 확인해 전환합니다. 맵별 복제와 epoch가 이전 맵의 지연 입력을 차단합니다. 저장 맵 재접속은 --scene으로 명시합니다.
- MyEditor --export-game으로 같은 버전의 Release MyGame·프로젝트 에셋·GUID·고지를 새 폴더에 내보냅니다. 기존 출력은 보존하며 실패한 임시 결과를 게시하지 않습니다. 에디터 export 메뉴와 서버/설치 프로그램 내보내기는 다음 작업입니다.
- 기존 로컬 Lua·게임 UI·방향 모션/행동 상태·2D 카메라/입력과 XYZ 경로를 유지합니다. 제작 그림·리깅은 외부 도구를 사용합니다.

서버 데이터를 먼저 백업하세요. 구형 캐릭터 저장은 읽지만 새 저장은 version 2/records와 명시적인 사망 상태를 사용합니다. 구버전 서버는 새 형식을 거부하므로 다운그레이드에 같은 데이터를 재사용하지 마세요. 기존 프로젝트 에셋과 GUID를 변경하지 않습니다.

[온라인 전투·맵 가이드](https://github.com/ekdms05/MyEngine/blob/v0.5.0/docs/28-2d-online-world.md), [게임 내보내기](https://github.com/ekdms05/MyEngine/blob/v0.5.0/docs/29-game-export.md), [전체 제작 가이드](https://github.com/ekdms05/MyEngine/blob/v0.5.0/docs/README.md)를 확인하세요. ZIP의 docs/guide/index.html은 오프라인 안내입니다.

검증: Debug/Release 619개 회귀, MCP build·45개 smoke, 실제 두 클라이언트의 공격/HP·포털 왕복·도착 맵 저장/재시작, 독립 게임 폴더 실행과 원본/출력 픽셀 비교. 배포 워크플로는 기존 충돌·입력·모션/맵 수명·게임 UI 검사도 유지하고 DX11 WARP를 사용합니다. WARP는 GPU 성능이나 실제 입력·클린 PC 검수를 대신하지 않습니다.

현재 전송은 인증된 loopback 개발용 UDP입니다. 온라인 클라이언트는 프로젝트 Lua를 실행하지 않습니다. NPC/스킬/보상/퀘스트·온라인 HUD/채팅·표현용 VFX·부활·암호화/공개 운영·export GUI/설치/서버 배포는 아직 미완료입니다. 기본 공격은 타격 모션 마커와 연결되지 않습니다.

Windows 10/11 x64·DirectX 11·Microsoft Visual C++ v14 x64 런타임이 필요합니다. ZIP SHA256과 release-manifest.json으로 무결성을 확인하세요. 사용자 데이터·개인 로그인·로컬 설정·진단 로그는 엔진 배포물에 넣지 않습니다.
