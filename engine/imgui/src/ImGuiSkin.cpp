// mye/imgui/ImGuiSkin.cpp — devTool ImGui 스킨 이식 구현 (ImGuiSkin.h 참조)
#include "mye/imgui/ImGuiSkin.h"

#include "imgui.h"

namespace mye::imgui {

namespace {
// devTool 액센트(ImGuiCol_Scheme) — 주황.  main.hpp: ImVec4(0.96f, 0.52f, 0.12f, 1.f)
constexpr ImVec4 kAccent   {0.96f, 0.52f, 0.12f, 1.00f};
constexpr ImVec4 kAccentHi {1.00f, 0.60f, 0.20f, 1.00f};  // hover(밝게)
constexpr ImVec4 kAccentLo {0.78f, 0.40f, 0.08f, 1.00f};  // active(어둡게)

inline ImVec4 WithAlpha(const ImVec4& c, float a) { return ImVec4(c.x, c.y, c.z, a); }
} // namespace

void ApplySkin() {
    // 1) 다크 베이스 — 원본과 동일하게 StyleColorsDark 위에 커스터마이즈.
    ImGui::StyleColorsDark();

    ImGuiStyle& st = ImGui::GetStyle();

    // 2) 짧은 라운딩과 일정한 간격으로 밀도 높은 편집 패널을 구분한다.
    st.WindowRounding    = 3.0f;
    st.ChildRounding     = 3.0f;
    st.FrameRounding     = 3.0f;
    st.PopupRounding     = 3.0f;
    st.GrabRounding      = 2.0f;
    st.WindowPadding    = ImVec2(10, 10);
    st.FramePadding     = ImVec2(7, 4);
    st.ItemSpacing      = ImVec2(8, 6);
    st.ScrollbarSize     = 16.0f;   // 로그/목록 스크롤바를 굵게 — 빠르게 드래그
    st.ScrollbarRounding = 8.0f;
    st.TabRounding       = 6.0f;

    // 3) 주황 액센트 매핑 — 스톡 ImGui엔 ImGuiCol_Scheme이 없으므로 표준 상호작용
    //    색에 액센트를 입혀 동일한 시각 아이덴티티를 재현한다.
    ImVec4* c = st.Colors;
    c[ImGuiCol_Text] = ImVec4(0.90f, 0.91f, 0.94f, 1.00f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.58f, 0.64f, 1.00f);
    c[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.09f, 0.12f, 1.00f);
    c[ImGuiCol_ChildBg] = ImVec4(0.07f, 0.08f, 0.10f, 1.00f);
    c[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.11f, 0.14f, 1.00f);
    c[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.16f, 0.20f, 1.00f);

    // 상호작용 프레임(체크박스/슬라이더/입력창 배경) — 눌림/hover 시 주황.
    c[ImGuiCol_FrameBgHovered]      = WithAlpha(kAccent, 0.35f);
    c[ImGuiCol_FrameBgActive]       = WithAlpha(kAccent, 0.55f);

    // 슬라이더/그랩·체크마크 — 액센트.
    c[ImGuiCol_CheckMark]           = kAccent;
    c[ImGuiCol_SliderGrab]          = kAccent;
    c[ImGuiCol_SliderGrabActive]    = kAccentHi;

    // 버튼 — 기본은 중립색, hover/active는 액센트로 강조한다.
    c[ImGuiCol_Button]              = ImVec4(0.16f, 0.18f, 0.23f, 1.00f);
    c[ImGuiCol_ButtonHovered]       = WithAlpha(kAccent, 0.70f);
    c[ImGuiCol_ButtonActive]        = kAccentLo;

    // 헤더(트리/셀렉션/메뉴) — 액센트 계열.
    c[ImGuiCol_Header]              = WithAlpha(kAccent, 0.35f);
    c[ImGuiCol_HeaderHovered]       = WithAlpha(kAccent, 0.60f);
    c[ImGuiCol_HeaderActive]        = WithAlpha(kAccent, 0.80f);

    // 탭 — 활성 탭을 주황으로.
    c[ImGuiCol_Tab]                 = WithAlpha(kAccent, 0.30f);
    c[ImGuiCol_TabHovered]          = WithAlpha(kAccent, 0.70f);
    c[ImGuiCol_TabActive]           = WithAlpha(kAccentLo, 0.90f);
    c[ImGuiCol_TabUnfocused]        = WithAlpha(kAccent, 0.10f);
    c[ImGuiCol_TabUnfocusedActive]  = WithAlpha(kAccentLo, 0.55f);

    // 타이틀바 활성 — 살짝 주황 틴트.
    c[ImGuiCol_TitleBgActive]       = ImVec4(0.24f, 0.13f, 0.04f, 1.00f);

    // 리사이즈 그립·분리선·플롯 하이라이트·텍스트 셀렉션.
    c[ImGuiCol_ResizeGrip]          = WithAlpha(kAccent, 0.30f);
    c[ImGuiCol_ResizeGripHovered]   = WithAlpha(kAccent, 0.65f);
    c[ImGuiCol_ResizeGripActive]    = WithAlpha(kAccentHi, 0.90f);
    c[ImGuiCol_SeparatorHovered]    = WithAlpha(kAccent, 0.65f);
    c[ImGuiCol_SeparatorActive]     = kAccentHi;
    c[ImGuiCol_PlotHistogramHovered]= kAccentHi;
    c[ImGuiCol_TextSelectedBg]      = WithAlpha(kAccent, 0.35f);
    c[ImGuiCol_NavHighlight]        = kAccent;
    c[ImGuiCol_DragDropTarget]      = kAccentHi;

    // 스크롤바 그랩 — 액센트 계열로 눈에 띄게.
    c[ImGuiCol_ScrollbarGrabHovered]= WithAlpha(kAccent, 0.55f);
    c[ImGuiCol_ScrollbarGrabActive] = WithAlpha(kAccent, 0.80f);

    // 도킹 프리뷰 — 액센트.
    c[ImGuiCol_DockingPreview]      = WithAlpha(kAccent, 0.55f);
}

} // namespace mye::imgui
