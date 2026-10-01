// ImGuiFonts.h — bundled NanumSquareRound UI font with system CJK merging.
// Windows language fonts are loaded in place and are not redistributed.
#pragma once

namespace mye::imgui {

// 현재 ImGui 컨텍스트의 폰트 아틀라스에 CJK 지원 폰트를 로드한다.
// 백엔드 Init 전에 호출(아틀라스는 첫 NewFrame에 빌드됨). 폰트 파일을 못 찾으면
// 조용히 기본 폰트를 유지한다(호출부는 실패해도 계속 진행). 로드 성공 시 true.
bool LoadEditorFonts(float sizePx = 18.0f);

} // namespace mye::imgui
