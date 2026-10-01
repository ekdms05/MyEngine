# 02. 렌더링과 좌표·깊이 계약

현재 렌더 경로는 `mye_rhi`의 DX11 백엔드와 `mye_render`의 `HybridRenderer`·`SpriteBatch`·`PixelPerfectTarget`이다. 이 문서는 공용 좌표·픽셀·깊이 계약을 소유한다. API의 실제 정의는 [Rhi.h](../engine/rhi/include/mye/rhi/Rhi.h)와 [render 헤더](../engine/render/include/mye/render)에 있다.

## 좌표계·단위

| 항목 | 계약 |
|---|---|
| 공간 | 왼손 좌표계, 월드 +Y 위, 3D 카메라 앞 +Z |
| 단위 | 기본 PPU 48; 48px 타일은 월드 1 unit |
| 내부 픽셀 타깃 | 960×540, 16:9 |
| 화면 UI/Text | 좌상단 원점, +Y 아래 |
| NDC 깊이 | 0은 near, 1은 far; 작은 깊이가 앞 |
| 스프라이트 피벗 | 캐릭터는 발밑 기준으로 위치·정렬을 해석 |

[Camera2D](../engine/render/include/mye/render/Camera2D.h)가 화면/월드 변환·카메라 픽셀 스냅을 담당한다. 표현 상태에 보간을 적용할 경우 보간 후 픽셀 스냅한다. 월드의 +Y 위와 UI의 +Y 아래를 혼용하지 않는다.

[SpriteCorners](../engine/scene/include/mye/scene/SpriteGeometry.h)는 소스 영역 크기·발밑 피벗·PPU와 전체 WorldTransform으로 쿼드 좌표를 만든다. HybridRenderer와 에디터 선택 경계가 같은 함수를 사용하므로 scale·rotation을 렌더에서 누락하지 않는다. SceneSerializer는 로드한 LocalTransform에 파생 WorldTransform을 복구한다. 좌표·PPU·깊이·cutout 수치는 유지한다.

MyEditor PNG·뷰포트는 UNORM 색을 그대로 사용한다. 편집기 스왑체인도 BGRA8Unorm으로 맞춰 ImGui 출력에서 sRGB 인코딩이 색을 다시 밝히지 않게 한다. 월드 텍스처의 point 샘플링과 ImGui 이미지 미리보기의 필터는 구분한다.

## 현재 프레임 경로

```text
Scene RenderExtract → RenderProxyList → HybridRenderer
→ 불투명 3D 메시 → 타일/스프라이트 cutout → 픽셀 타깃 확대
→ 앱의 게임 UI·텍스트 또는 에디터 ImGui → Present
```

[HybridRenderer::Render](../engine/render/src/HybridRenderer.cpp)는 메시를 먼저 그리고 타일·스프라이트를 같은 깊이 버퍼에 그린다. CPU 제출 순서 대신 깊이 인코딩과 alpha cutout으로 가림을 결정한다. `render`가 `scene` 헤더를 PRIVATE include로 읽는 실제 결합은 [구조 문서](13-architecture-and-features.md)에 기록되어 있다.

RHI는 GPU 핸들·버퍼·텍스처·파이프라인·BindGroup·스왑체인·커맨드 컨텍스트를 제공한다. 구현 백엔드는 DX11이고 셰이더는 HLSL/FXC를 사용한다. DX12/Vulkan·범용 RenderPass 플러그인·프레임 그래프·포스트 체인을 현재 기능으로 나열하지 않는다.

## 알파와 단일 깊이

월드 스프라이트·타일은 cutoff 미만의 알파를 discard하고 남은 픽셀에 depth test/write를 적용한다. 따라서 투명 영역이 뒤의 캐릭터·3D 오브젝트를 가리지 않는다. 일반 반투명 효과를 cutout 경로에 그대로 넣으면 동일한 정렬 결과를 보장할 수 없다.

스프라이트의 쿼드 전체는 발밑 `sortKeyY`에 따른 같은 깊이를 사용한다. 점프 같은 시각적 오프셋과 논리 지면 정렬을 구분한다. 경사 타일은 아래/위 정렬 값을 보간하는 ramp depth를 사용한다.

## Screen2D 깊이 인코딩

정본은 [DepthEncoder.cpp](../engine/render/src/DepthEncoder.cpp)의 `EncodeDepth`다. 실제 구현의 수식은 다음과 같다.

```text
range = max(viewRangeY, 0.0001)
viewBottomY = viewTopY - range
t = saturate((sortKeyY - viewBottomY) / range)
orderCap = band.width * 0.5
orderBias = clamp(orderInLayer * 0.00001, -orderCap, orderCap)
depth = clamp(band.base + t * band.width - orderBias,
              band.base, max(band.base, band.base + band.width - 0.00001))
```

`sortKeyY`가 클수록 화면 뒤쪽이므로 깊이가 커진다. 같은 지면 Y에서 `orderInLayer`가 클수록 앞으로 당긴다. 미세 바이어스와 최종 깊이를 밴드 안에 제한하여 인접 층 침범·공유 경계의 z-fighting을 막는다.

| sortLayer | 깊이 구간 | 용도 |
|---|---|---|
| 0 BackgroundFar | 0.95~1.00 | 원경 |
| 1 BackgroundNear | 0.90~0.95 | 근경·패럴랙스 |
| 2 Ground | 0.85~0.90 | 지면 |
| 100+k World | 0.20~0.85의 8개 밴드 | 캐릭터·오브젝트·다리층 |
| 3 OverheadFX | 0.10~0.20 | 머리 위 표현용 예약 밴드 |

World의 기본 밴드 수는 8이고 높은 층일수록 base가 작다. 현재 값은 [DepthEncoder.h](../engine/render/include/mye/render/DepthEncoder.h)의 컴파일 상수이며 런타임 프로젝트 설정으로 조절되는 기능은 아니다. 렌더의 층 변환은 0~7로 제한한다. 씬 추출의 층 매핑과 렌더 변환이 서로 다른 상한을 갖는 점은 별도 정합 검토 대상이다.

다리 상판과 위층 캐릭터는 상위 World 밴드, 아래 길·캐릭터는 하위 밴드를 사용한다. 층 변경의 원인은 월드·충돌 로직이며 렌더러는 전달받은 층과 정렬 값을 소비한다. MyGame의 물리·층 전이는 아직 전체 연결되지 않았으므로 `bridge_demo`의 검증을 모든 게임 이동 경로의 완성으로 해석하지 않는다.

## 삽입 3D 메시

| 모드 | 현재 의미 |
|---|---|
| AnchorFlat | 메시 전체를 앵커의 깊이로 평탄화 |
| AnchorBiased | 앵커 정렬에 메시 내부의 정규화된 기하 깊이를 작게 더함 |
| Geometry | 카메라의 실제 기하 깊이 사용 |

AnchorBiased의 셰이더는 메시의 view-space Z 범위를 정규화한 `n`을 사용한다.

```text
biasEps = band.width * 0.10
depth = anchorDepth + (0.5 - n) * biasEps
```

편차는 앵커 주변으로 제한된다. 월드 Z 두께를 그대로 곱하는 방식과 다르며, 메시 자체의 앞뒤를 유지하면서 2D 발밑 정렬에 참여한다. 3D 조명은 현재 방향광·ambient 기반 경로다. 다중 조명·그림자·노멀맵 2D 조명·완성된 Billboard3D 제작 워크플로는 확장 요구에 해당한다.

## 픽셀 타깃과 UI

[PixelPerfectTarget](../engine/render/src/PixelPerfectTarget.cpp)은 내부 타깃을 정수 배율로 확대하고 남는 화면 영역을 여백으로 처리한다. 1920×1080은 기본 타깃의 2배다. sharp-bilinear 전체 화면 옵션은 현재 구현으로 표시하지 않는다.

게임의 클릭 좌표는 `ComputeLayout`의 destRect·정수 배율과 동일하게 변환한다. UI 패널·텍스트는 월드 깊이 대신 제출 순서를 유지한다. [SpriteBatch](../engine/render/include/mye/render/SpriteBatch.h)의 `yDown`은 UI 방향, `alphaMask`는 R8 글리프 coverage를 나타낸다. R8을 일반 RGBA 텍스처로 읽지 않고 coverage로 premultiplied 색·알파를 만든다.

## 캡처·검증·남은 범위

프레임 캡처는 DX11의 `CaptureBackbuffer` 경로를 쓴다. 일반 GPU readback의 `CopyTextureToBuffer`·`EnqueueReadback`·`TryGetReadback`은 아직 stub이므로 완성된 ID 버퍼 픽킹·썸네일 비동기 경로로 설명하지 않는다. `WriteTimestamp`·`ResolveTimestamps`도 현재 DX11 stub이며 GPU 측정 완료 기능으로 설명하지 않는다.

검증은 `bridge_demo`의 다리 위/아래·3D 가림, `sprite_demo`의 픽셀 출력과 `mye_tests`의 깊이·카메라·R8 글리프 출력 검사로 수행한다. 조명·파티클 계산 데이터의 존재는 GPU 합성 완료가 아니다. 실루엣·오버헤드 페이드·라이트맵·포스트·다른 그래픽 API는 [개선 목록](14-development-priorities.md)에 따라 실제 수요와 장면 측정으로 결정한다.

문서의 깊이 수식은 현재 코드 호출 경로에 맞춰 정정했다. 이번 문서 정리는 좌표·PPU·밴드 상수·실행 알고리즘을 변경하지 않았다.

## 에디터 3D 보기

ViewportCamera는 같은 씬을 2D 또는 LH 원근 카메라로 표시한다. `BuildViewportView`와 투영/역투영을 렌더·그리드·팬·선택에서 공유하며 기준 편집 평면은 Z=0이다. SpriteCorners3D는 full TRS의 world XYZ를 유지한다. HybridViewInfo.geometryDepth가 켜지면 쿼드 VS는 clip Z/W, 메시 VS는 Geometry 깊이를 쓴다. 2D의 인코딩 깊이 경로는 그대로다.

같은 XY 평면의 지형·소품은 원근 깊이가 같아 raster 정밀도로 부분 가림이 발생할 수 있다. 3D 쿼드에만 기존 EncodeDepth 결과 × `1e-5` NDC 바이어스를 더해 동률 순서를 정한다. 원근 XY/W와 물리 Transform을 바꾸지 않는다. 실제 3D 캡처에서 이 문제를 발견하고 수정 전후를 확인했다. 좌표·PPU48·960×540·alpha cutout은 유지하며 3D 뷰에서 물리가 3D로 바뀌지는 않는다.
