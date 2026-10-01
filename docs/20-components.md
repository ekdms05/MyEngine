# 20. 컴포넌트 사용법

하이어라키에서 오브젝트를 선택하고 인스펙터에서 필요한 컴포넌트를 펼친다. 설명은 펼친 헤더 아래, 필드의 단위·제약은 마우스를 올렸을 때 표시된다. 헤더 우클릭으로 제거하고 Ctrl+Z로 복원한다. 실행 중 편집은 PlayWorld의 임시 상태다.

| 컴포넌트 | 사용법 | 예시·제약 |
|---|---|---|
| ObjectName | 오브젝트 이름 | 이벤트 대상·포털 도착 이름은 씬에서 유일해야 한다 |
| LocalTransform | 부모 기준 position/rotation/scale | +Y 위; 1 unit=48px. 단위 회전 쿼터니언 0,0,0,1 |
| WorldTransform / Parent / Children | 계층에서 파생되는 변환·연결 | 위치는 LocalTransform, 계층은 하이어라키에서 편집 |
| SpriteRenderer | sprite에 PNG 드래그, srcUV로 영역 선택 | 전체 UV 0,0,1,1; pivotPx는 영역 좌상단 기준 픽셀. tint 흰색은 원본 색 |
| FloorLevel | 높이 층 level | 지면 0; 렌더·충돌 층을 함께 확인 |
| Collider2D | shape/half/offset·그룹/층 마스크 | 48px 전체 폭은 반폭 0.5. isTrigger면 이동을 막지 않고 이벤트 발생 |
| KinematicBody2D | 충돌하며 이동하는 본체 | skin=충돌 간격, maxSlideIters=1~16. 바닥 스냅에는 바닥 서비스 필요 |
| CharacterController2D | enabled/speed·idleAnimation/walkAnimation | 최상위 활성 1명, 속도 0~100 unit/s. 3이면 144px/s. 조작을 끄면 정지 |
| SpriteAnimator | animation에 .anim 드래그, speed/playing 설정 | speed=재생 배율. 이동 속도와 별개. 이미지 크기·에셋 참조 일치 필요 |
| InteractionTarget | enabled/radius/prompt | radius 안에서 E. 행동은 ObjectBehavior로 연결. prompt 자동 HUD는 미연결 |
| ScenePortal | scenePath/spawnName/onInteract | assets/scenes/*.scene과 목적지 ObjectName. E면 InteractionTarget, 자동 진입이면 trigger Collider2D |
| ObjectBehavior | 이벤트→행동 연결 또는 Lua return 테이블 | Start/Interact/TriggerEnter/TriggerExit. 연결 순서대로 실행, 최대64개. Lua64KiB |
| Progression | level/xp/maxLevel | 현재 레벨 내 XP. 성장·보상 규칙은 게임에서 정의 |

## 기본 조작 캐릭터

1. 최상위 오브젝트를 선택하고 **캐릭터 기본 이동 구성**을 누른다. 또는 **+ 추가 → 캐릭터**를 선택한다.
2. SpriteRenderer.sprite에 PNG를 지정하고 발밑 피벗과 충돌 박스를 맞춘다.
3. CharacterController2D.speed와 대기·걷기 모션을 지정한다.
4. 재생하여 게임 창에서 WASD/방향키로 걷고 E로 상호작용한다. Pause/Stop은 에디터에 있다.

구성 버튼은 기존 값을 보존하며 누락된 스프라이트·충돌·본체·조작을 한 Undo 작업으로 추가한다. Lua로 별도 속도를 지정하려면 CharacterController2D를 제거하고 본체·충돌을 유지한다. 조작 컴포넌트가 있으면 Lua 이후 본체 속도를 다시 설정한다.

## 코드 없이 연결

ObjectBehavior의 연결은 이벤트, 행동, 대상, 매개변수 순서로 설정한다. 대상이 비어 있으면 현재 오브젝트다. Message는 메시지, SetVisible은 표시 전환, MoveTo는 즉시 위치 변경, ChangeMap은 씬 경로+도착 이름, LuaCallback은 return 테이블의 함수 이름을 사용한다. MoveTo는 경로 탐색 이동이 아니다.

Lua 콜백·함수·변수는 [Lua API](19-lua-api.md), 맵/저장 실패 흐름은 [오브젝트 작업](17-object-workflow.md)을 참고한다. 표시나 메시지 기반만으로 게임 HUD·퀘스트·온라인 동기화가 자동 완성되지는 않는다.
