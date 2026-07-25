// mye/imgui/ImGuiFonts.h — ImGui 폰트 로딩 (한글/CJK). devTool HaFont(haruna) 이식.
//
// 스톡 ImGui 기본 폰트는 ASCII 전용이라 한글이 □ 로 나온다. 여기서 CJK 글리프를 담은
// HaFont(haruna.ttf, 상업 배포 가능)를 로드해 에디터/오버레이 UI에 한글을 렌더링한다.
// i18n(ko/en/ja/zh)에서 일본어/중국어 글리프 폰트를 이 지점에서 병합 확장한다.
#pragma once

namespace mye::imgui {

// 현재 ImGui 컨텍스트의 폰트 아틀라스에 CJK 지원 폰트를 로드한다.
// 백엔드 Init 전에 호출(아틀라스는 첫 NewFrame에 빌드됨). 폰트 파일을 못 찾으면
// 조용히 기본 폰트를 유지한다(호출부는 실패해도 계속 진행). 로드 성공 시 true.
bool LoadEditorFonts(float sizePx = 18.0f);

} // namespace mye::imgui
