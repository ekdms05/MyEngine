# 초원마을 기본 프로젝트

개별 타일·건물·우물·나무·울타리·안내판과 레벨 1 모험가로 구성한다. 마을 255개, 집 내부 57개 엔티티가 각 `.scene`에 저장되며 지형은 `Terrain` 그룹 아래에 있다. 기존 사용자 프로젝트는 자동 덮어쓰지 않는다. 새 프로젝트에서 기본 에셋 포함을 선택해 최신 템플릿을 사용한다.

| 에셋 | 용도 | 크기·규약 |
|---|---|---|
| `environment/terrain_tiles.png` | 풀·흙·물·돌 4개 지형 | 1254×1254, 2×2 영역, 타일 월드 크기 1×1 |
| `environment/village_objects.png` | 두 집·우물·나무·울타리·안내판 | 1024×1536 RGBA, 6개 독립 영역·발밑 피벗 |
| `environment/meadow_village.png` | 원래 마을 구상의 참고 아트 | 1672×941, 기본 씬에서는 사용하지 않음. 비교·재구성 근거로 보존 |
| `characters/novice.png` | 모험가 시트 | 2170×725 RGBA, 8개 영역 |
| `animations/novice_idle.anim` | 대기 | 프레임 0–3, 각 0.30초, 반복 |
| `animations/novice_walk.anim` | 걷기 | 프레임 4–7, 각 0.14초, 반복·footstep 이벤트 |
| `scenes/meadow_village.scene` | 마을 | 시작 위치 `(0,-0.9,0)`, `Progression.level=1`, 배율 0.15 |
| `scenes/cottage.scene` | 집 내부 | 돌 타일·경계 콜라이더·복귀 포털·이름 있는 도착 지점 |

Play를 누르고 뷰포트를 클릭하면 WASD/방향키로 이동한다. 우물·안내판 근처에서 E로 상호작용한다. 서쪽 집의 `Cottage Door`에서 E를 누르면 `Cottage Spawn`으로 이동한다. 내부 남쪽 `Cottage Exit`에서 E를 누르면 마을 `Village Spawn`으로 돌아온다. 레벨·경험치는 맵 이동에서 유지되고 Stop은 편집 월드를 보존한다.

Hierarchy 이름으로 선택하거나 뷰포트에서 선택해 위치·배율·UV·충돌·동작을 수정한다. 지형도 일반 스프라이트 엔티티로 저장한다. 물 타일은 아틀라스에 준비돼 있으며 기본 배치에는 사용하지 않았다. 대규모 맵의 청크·브러시·자동 타일은 기존 Tilemap/TileEditing에 별도 저장 계약을 연결할 작업이다. 8방향·장비·공격 아트는 이 기본 시트의 범위에 포함하지 않는다.

건물·우물·나무의 `Collider2D`는 세계 좌표 단위의 발밑 AABB다. 아트의 투명 영역과 물리 경계를 구분하며 뷰포트 **충돌 영역**에서 확인한다. 캐릭터는 `CharacterController2D`·`KinematicBody2D`·`Collider2D`로 움직이고 실제 이동 결과에 따라 대기/걷기 `.anim`을 선택한다.

동작 편집과 Lua 예제는 [제작 안내](../../../docs/17-object-workflow.md), 아트·설계·검증 근거는 [작업 기록](../../../docs/16-foundation-worklog.md)에 있다. PNG와 `.meta`를 함께 옮겨 GUID를 보존한다. 원본 시트는 자르거나 리샘플링하지 않고 UV로 영역을 사용한다. 타사 게임·캐릭터·로고·외부 에셋 팩을 사용하지 않았다.

## 편집 가능한 도트·모션 예제

새 기본 프로젝트의 assets/sprites/novice.dot을 에셋 브라우저에서 더블클릭해 연다. 기존 novice.png의 첫 영역을 제작 API로 nearest 축소한 128×288/12프레임 원본이며 불변 참조, 몸통/머리/다리 4개 파츠, idle/walk/greet 모션을 제공한다. 기존 PNG·GUID·씬·기본 모션을 교체하지 않는다. 회전으로 드러나는 절단 경계·가려진 픽셀은 실제 아트 제작에서 보완한다. 8방향·완성된 보행 아트를 제공하는 것은 아니다. 제작 순서는 [도트·모션 안내](../../../docs/18-editor-authoring.md)와 [가이드 페이지](../../../docs/guide/index.html)에 있다.
