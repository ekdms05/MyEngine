// mye/editor/Viewport.h — 씬 뷰포트 렌더러 인터페이스 + 뷰포트 패널 (docs/07 씬 뷰포트·기즈모)
//
// 07: 씬 뷰포트는 활성 World를 오프스크린 RT(HybridRenderer로 RenderExtract 렌더)에 그리고,
//   ViewportPanel이 ImGui::Image(GetImGuiTextureID)로 표시한다. 팬/줌 2D 카메라·픽셀 그리드·
//   CPU 레이/AABB 픽킹(엔티티 선택)·이동 기즈모(Transform 드래그 → PropertyEditCommand)를 제공한다.
//
// 렌더 소유 분리: 실제 오프스크린 RT·HybridRenderer·카메라 상태는 EditorModule(디바이스/스왑체인
//   소유자)이 IEditorViewport 구현으로 제공한다. 패널은 이 인터페이스로만 렌더 자원에 접근한다
//   (패널이 RHI/렌더러를 직접 만지지 않도록 결합을 낮춘다). EditorApp이 구현을 배선한다.
#pragma once

#include "mye/editor/EditorTypes.h"
#include "mye/editor/Panel.h"
#include "mye/core/Math.h"

#include <cstdint>
#include <memory>

namespace mye::editor {

// 뷰포트 카메라 상태(패널이 팬/줌으로 갱신, EditorModule이 렌더에 소비).
struct ViewportCamera {
    Vec2  center{0.0f, 0.0f};   // 월드 중심(unit)
    float zoom = 1.0f;          // 1=1x, 2=확대
};

// 뷰포트 렌더 자원 게이트웨이. EditorModule(디바이스 소유자)이 구현한다.
//   패널은 매 프레임 뷰포트 픽셀 크기·카메라를 통지하고, 그 결과 오프스크린 RT의 ImGui
//   텍스처 ID와 좌표 변환(스크린↔월드)을 얻는다. 렌더 자체는 EditorModule이 프레임 말미에 수행.
class IEditorViewport {
public:
    virtual ~IEditorViewport() = default;

    // 이번 프레임 뷰포트가 요구하는 픽셀 크기·카메라를 통지(다음 렌더가 이 값을 사용).
    virtual void SetViewportSize(uint32_t widthPx, uint32_t heightPx) = 0;
    virtual void SetCamera(const ViewportCamera& cam) = 0;
    virtual ViewportCamera Camera() const = 0;

    // 직전에 렌더된 오프스크린 RT의 ImGui 텍스처 ID(ImGui::Image 인자). null 가능(미초기화).
    virtual void* ColorTextureId() const = 0;
    // 렌더 대상 픽셀 크기(ColorTextureId의 이미지 크기). ImGui::Image UV/좌표 매핑에 사용.
    virtual uint32_t RenderWidth() const = 0;
    virtual uint32_t RenderHeight() const = 0;

    // 좌표 변환(뷰포트 이미지 로컬 픽셀 ↔ 월드). localPx: 이미지 좌상단(0,0)~(RenderW,RenderH),
    //   +Y 아래(ImGui 화면 좌표). 픽킹·기즈모·그리드가 소비.
    virtual Vec2 ScreenToWorld(Vec2 localPx) const = 0;
    virtual Vec2 WorldToScreen(Vec2 world) const = 0;
};

// 씬 뷰포트 패널 팩토리(내장 패널 — 다른 패널과 동일 경로 등록).
std::unique_ptr<IEditorPanelFactory> MakeViewportPanelFactory();

} // namespace mye::editor
