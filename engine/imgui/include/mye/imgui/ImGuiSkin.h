// mye/imgui/ImGuiSkin.h — devTool ImGui 스킨 이식 (다크 + 주황 액센트 + 둥근 모서리)
//
// 원본(E:/monad/.../devTool)은 커스텀 패치 ImGui의 ImGuiCol_Scheme(주황 액센트)와
// StyleColorsDark 위에 라운딩값을 얹어 스킨을 구성한다. MyEngine의 스톡 ImGui에는
// ImGuiCol_Scheme 항목이 없으므로, 여기서는 동일한 라운딩값을 적용하고 주황 액센트를
// 표준 상호작용 색(헤더/버튼/탭/슬라이더/체크마크/스크롤바 등)에 매핑해 같은 시각
// 아이덴티티(주황-다크-라운드)를 재현한다.
#pragma once

namespace mye::imgui {

// 현재 ImGui 컨텍스트에 devTool 스킨(다크 베이스 + 주황 액센트 + 라운딩)을 적용한다.
// ImGui::CreateContext() 이후, 백엔드 Init 전(혹은 언제든 스타일 갱신 시)에 호출.
void ApplySkin();

} // namespace mye::imgui
