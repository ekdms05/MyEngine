# 10. 제품 기능과 개선 안내

기준일: **2026-10-01**. 현재 기능은 소스·앱 실행 경로로 설명한다. 기능 상태의 상세 정본은 [13. 현재 구조와 기능](13-architecture-and-features.md), 개선 순서와 완료 조건의 정본은 [14. 개발 우선순위](14-development-priorities.md)다.

## 현재 사용 가능한 경로

| 영역 | 현재 기능 | 사용·검증 경로 |
|---|---|---|
| 픽셀·하이브리드 렌더 | DX11, 단일 깊이, 타일/스프라이트/3D, 정수 확대 | MyGame, sprite/bridge/character/village 샘플 |
| 씬·콘텐츠 기반 | sparse-set ECS, 트랜스폼, 타일, 물리, 애니, Lua, 텍스트·오디오·대화/NPC | 라이브러리 테스트와 통합 샘플 |
| 에디터 | 도킹 셸, 선택·Inspector·Undo, PlayWorld, 타일/애니 편집 | MyEditor, 에디터 자체 테스트 |
| 게임 앱 | 타이틀·설정·씬 로딩·로컬 이동·BGM, 네트워크 예측·보간 | MyGame 기본 samples/mmo_demo 프로젝트 |
| 로컬 서버 | 인증·권위 이동·활성 세션 저장·백업·운영 메트릭 | MyServer, verify-foundation.ps1 |
| RPG·소셜 기반 | 전투·아이템·퀘스트·성장, 친구·채팅·파티·길드·거래·사냥 데이터 | engine/gameplay, game/social, game/mmo 테스트; 앱 통합은 제한적 |
| 에셋·확장·개발 도구 | VFS·임포트·pak, 리플렉션·DDC·DLL 호스트, MCP 8개 도구 | asset_smoke·자체 테스트·MCP smoke |

## 우선 개선할 사용자 흐름

1. 외부 접속 전에 보호되는 인증 전송·세션 패킷 인증·로그인 제한·데이터 내구성을 검증한다. 현재 서버는 루프백을 유지한다.
2. 로컬/서버 고정 틱 이동·충돌·층 판정과 접속 해제/timeout 경계를 연결한다.
3. 에디터 실제 프로젝트·씬 열기 → 편집 → 저장 → 재열기 → 플레이를 완성한다.
4. 기존 Lua/runtime/gameplay/social/mmo 모듈을 MyGame/MyServer의 실제 콘텐츠·명령·UI에 연결한다.
5. 배포 에셋·글꼴·pak·콘텐츠 검사와 동일 Release 장면의 CPU/GPU/네트워크 측정을 보완한다.

각 작업의 ID·선행 조건·실패 시나리오는 [14](14-development-priorities.md)에서 관리한다. 모듈을 다시 만들거나 모든 기능을 완료로 재분류하지 않는다. 수용량·운영 안전성은 별도 검증이 필요하다.

## 확인 방법

```powershell
cmake --build build/dev --config Debug
ctest --test-dir build/dev -C Debug --output-on-failure
powershell -File tools/verify-foundation.ps1 -Configuration Debug
```

Release는 같은 build/dev의 Release 구성으로 확인한다. 화면·실제 오디오·외부 운영은 각각 필요한 장면/장치/장애 검증을 수행한다. 실행한 검증과 남은 한계는 [작업 기록](16-foundation-worklog.md)에 남긴다.

과거 단계별 완료표·서로 다른 시점의 중복 기능표는 제거했다. 구 빌드 디렉터리의 실제 경로는 삭제 판단에 필요한 증거이므로 작업 기록에 보존한다. 이 문서의 파일명은 기존 링크 호환을 위해 유지한다.
