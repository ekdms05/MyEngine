# XYZ 플레이와 온라인 연결

캐릭터는 XZ로 이동하고 Y로 점프·낙하한다. 에디터 Play와 MyGame, MyServer 권위 계산과 MyGame 예측은 같은 `PhysicsWorld3D::Step`을 사용한다. 기존 XY 장면은 그대로 2D 물리를 사용한다.

## 장면 만들기

1. 하이어라키 **+ → 캐릭터 3D**를 추가한다. 최상위, 회전 없음, 양수 XYZ 스케일을 유지한다. Collider3D의 로컬 half·offset에 이 스케일이 적용된다.
2. BillboardRenderer에 PNG 또는 `.anim`을 지정한다. 바디 크기는 이미지 크기와 별개다. Collider3D의 `half=(.3,.8,.3)`, `offset=(0,.8,0)`은 발 위치를 원점으로 하는 기본 상자다.
3. **충돌 3D**로 바닥·벽·천장을 만든다. `half`는 반크기다. 바닥 위에 캐릭터의 발을 배치한다. MeshRenderer만 추가한 물체는 충돌하지 않는다.
4. **경사 3D**는 -Z의 낮은 면에서 +Z의 높은 면으로 올라간다. 양수 XYZ 크기만 사용한다. 임의 회전·음수 스케일은 검증 오류다.
5. **게임 카메라 3D**의 `current`를 하나만 켠다. `followTarget`에 캐릭터의 고유 ObjectName을 지정하고 `target=(0,1,0)`, `orbitEnabled=true`로 설정한다.
6. 저장하고 재생한다. WASD/방향키는 XZ 이동, Space는 접지 중 점프, E는 로컬 상호작용, Q/R과 오른쪽 마우스 드래그는 카메라 회전이다. MyGame은 왼쪽 스틱 이동·A 점프·오른쪽 스틱 회전도 받는다.

Camera3D의 `yawDegrees`는 360도 순환, `pitchDegrees`는 고정 높이각, `distance`는 거리다. `rotationSpeed`는 도/초, `mouseSensitivity`는 도/픽셀이다. 카메라는 상자 캐스트로 장애물 앞까지 거리를 줄인다. 추종 보간·휠 줌·동적 강체는 제공하지 않는다. 카메라를 돌려도 캐릭터의 월드 진행 방향은 바뀌지 않는다. cameraRelative 이동은 orbit의 yaw 또는 고정 카메라의 수평 시선을 기준으로 한다. 현재 `.anim` 파일은 단일 클립이며 카메라 상대 8방향 에셋 매핑은 후속 항목이다.

KinematicBody3D.settings에서 speed·gravity·jumpSpeed·floorSnap·floorMaxAngle·stepHeight·skin·maxSlides를 설정한다. 중력과 점프는 월드 단위/초² 및 단위/초다. 걷는 경사는 floorMaxAngle보다 완만해야 한다. 접지 상태의 낮은 턱만 stepHeight 범위에서 오른다. 공중에서는 턱을 자동으로 오르지 않는다. 깊게 박힌 시작 위치는 오류로 거부한다.

트리거 3D는 진입·이탈 연결과 Lua 콜백을 사용한다. ScenePortal의 scenePath·spawnName으로 로컬 맵을 연결한다. 목적지에도 3D 조작 캐릭터가 필요하며 XYZ 스폰·Progression을 유지한다. MoveTo의 z는 3D 순간 이동에 사용한다. 일반 이동과 순간 이동을 구분한다.

## 두 계정 접속

배포본에는 MyServer.exe가 포함된다. 인증 개발 전송은 **127.0.0.1에만 바인딩**한다. 공개 인터넷 전송·완성 MMORPG 서버로 설명하지 않는다.

```powershell
MyServer.exe --data "private/server" --register tester "local-test-password"
MyServer.exe --data "private/server" --make-char tester Hero
MyServer.exe --data "private/server" --project "game/project.myeproj" --port 27015
MyGame.exe --project "game/project.myeproj" --connect 127.0.0.1:27015 --credentials "private/login.json" --character 1
```

login.json은 `{"username":"tester","password":"local-test-password"}` 형식이다. 실제 자격 증명과 서버 데이터는 배포 에셋·Git에 넣지 않는다. 위 계정 관리 명령은 합성 로컬 계정용이며 비밀번호 인자를 기록하는 도구에 전달하지 않는다. 캐릭터 ID를 생략하면 해당 계정의 첫 캐릭터를 선택한다. 다른 계정의 캐릭터, 이미 접속한 캐릭터, 다른 장면/좌표 계약의 저장 위치는 거부한다.

두 번째 계정을 등록하고 별도 MyGame을 실행하면 같은 장면의 다른 플레이어를 표시한다. 각 창의 카메라는 독립적이다. 원격 표현은 장면의 로컬 캐릭터 비주얼을 복제하고 조작·Lua·바디를 추가하지 않는다. 캐릭터별 외형 선택·원격 보간은 후속 작업이다.

서버와 게임에 같은 프로젝트/scene을 제공한다. `--scene assets/scenes/area.scene`으로 선택할 수 있다. 장면 JSON의 FNV 호환 지문이 다르면 접속을 거부한다. 지문은 인증 증명이 아니며 서버가 충돌 데이터를 소유한다. 서버는 60 Hz를 요구한다. legacy XY 서버와 봇은 `--project` 없는 별도 경로로 유지한다.

입력은 정규화한 XZ 방향·점프·순서를 전송하며 위치는 보내지 않는다. 서버가 한 고정 틱에 입력 하나를 계산한 뒤 확인 번호를 보낸다. 클라이언트는 권위 상태에서 미확인 입력을 동일 물리로 재실행한다. 최근 미확인 입력 최대 8개를 재전송하고 큐는 240개로 제한한다. 연결 nonce·무작위 세션 토큰·송신 주소·버전·길이·수치 범위·중복 ID·순서를 검사한다. 3D 상태는 float32로 보내 충돌 skin보다 거친 기존 XY 위치 양자화를 적용하지 않는다.

한 스냅샷은 최대 24개 엔티티와 1400 bytes 이하로 제한한다. 이 값은 실측 동접 성능이 아니다. 좌표는 각 축 ±100,000, 속도 길이는 200 이하이며 입장과 송수신에 같은 범위를 적용한다. 5초 수신 중단 또는 확인 큐 초과는 연결 실패다. 종료/타임아웃 후 XYZ·방향·sceneId·3D 계약을 기존 CharacterStore에 기록한다. 구 저장의 누락 posZ/facing/world3D는 0/0/false로 읽는다. 구 XY 위치를 자동으로 3D로 해석하지 않으며 legacy XY 서버도 기존 3D 캐릭터의 입장을 거부한다. 개발 소스에서는 ack가 이미 확인한 값보다 낮거나 보낸 입력보다 크면 스냅샷을 거부한다. 이 단조 검사와 세션 경계는 새 [2D 인증 라이브러리](03-scene-world.md#인증된-2d-이동의-라이브러리-연결)에서도 사용한다. main 소스의 앱은 같은 CLI에서 활성 2D/3D 캐릭터로 차원을 선택한다. [2D 플레이](23-2d-online-play.md)는 기존 0.3.0 배포본과 구분한다.

현재 온라인 경로는 **동일 정적 장면의 인증·캐릭터 입장·XYZ 이동/점프·충돌·예측/보정·복제·재접속**이다. 에디터 Play는 로컬 실행이다. 온라인 Lua/포털/보상/NPC/채팅/HUD/맵 전환·AOI·암호화·게임 내보내기는 연결하지 않았다. 클라이언트 Lua를 권위 게임 규칙으로 실행하지 않는다. 공개 운영 조건은 [개발 우선순위](14-development-priorities.md)에 남긴다.

## 구현 근거

2026-10-02, Godot revision `12c17c187e88efa23e6bbd6689630e256eca6523`의 공식 소스를 확인했다.

| 근거 | 적용 | MyEngine 차이 |
|---|---|---|
| [CharacterBody3D](https://github.com/godotengine/godot/blob/12c17c187e88efa23e6bbd6689630e256eca6523/scene/3d/physics/character_body_3d.cpp), [Vector3](https://github.com/godotengine/godot/blob/12c17c187e88efa23e6bbd6689630e256eca6523/core/math/vector3.h) | 잔여 이동의 법선 투영, floor/wall/ceiling, snap, 경사 정지 | 투영 식을 조정해 사용; 축 정렬 상자·닫힌 경사 볼록체의 연속 SAT, 기존 LH/ECS/고정 틱. 임의 삼각형/강체/플랫폼 없음 |
| [SpringArm3D](https://github.com/godotengine/godot/blob/12c17c187e88efa23e6bbd6689630e256eca6523/scene/3d/physics/spring_arm_3d.cpp) | 캐스트 결과와 안전 여백으로 카메라 거리 축소 | 기존 Camera3D에 입력·거리 필드 추가; 노드/PhysicsServer 의존성 없음 |
| [SceneReplicationInterface](https://github.com/godotengine/godot/blob/12c17c187e88efa23e6bbd6689630e256eca6523/modules/multiplayer/scene_replication_interface.cpp) | 권위 검증과 입장/상태 복제 경계 | 기존 UDP·계정·CharacterStore 유지; Godot RPC/ENet/객체 모델을 복사하지 않음 |

MIT 저작권·허가문은 `third_party/godot` 및 배포본 licenses에 보존한다. Godot 엔진 전체나 새로운 외부 물리/네트워크 라이브러리를 추가하지 않았다. 공유 계산 타깃 `mye_physics3d`는 ECS/렌더링을 참조하지 않으며 net은 이 타깃만 사용한다.
