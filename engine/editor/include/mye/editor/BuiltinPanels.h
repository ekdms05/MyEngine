// mye/editor/BuiltinPanels.h — 내장 패널 팩토리·드롭 배선 진입점 (docs/07)
//
// M4-B: 내장 패널(하이어라키·인스펙터·에셋 브라우저·콘솔)의 팩토리 생성 함수와, 씬 뷰포트가
//   에셋 드롭을 커맨드로 인스턴스화할 때 호출하는 공개 헬퍼를 모은다. 내장 = 1급 플러그인 —
//   각 팩토리는 PanelManager::RegisterFactory 로 등록된다(EditorApp::RegisterBuiltinPanels).
//
// 뷰포트 에이전트 소비 계약: "MYE_ASSET" 드래그드롭 페이로드(널 종단 vpath 문자열)를 받으면
//   InstantiateAssetToWorld(ctx, vpath, worldPos) 를 호출한다 — 종류별(스프라이트/.prefab/.lua)
//   생성 커맨드를 활성 스택에 발행(Undo 가능)한다.
#pragma once

#include "mye/core/Math.h"   // Vec2

#include <memory>
#include <string_view>

namespace mye::editor {

class IEditorPanelFactory;
struct EditorContext;

// ---- 내장 패널 팩토리(EditorApp 이 등록) ----
std::unique_ptr<IEditorPanelFactory> MakeHierarchyPanelFactory();
std::unique_ptr<IEditorPanelFactory> MakeInspectorPanelFactory();
std::unique_ptr<IEditorPanelFactory> MakeAssetBrowserPanelFactory();
std::unique_ptr<IEditorPanelFactory> MakeConsolePanelFactory();

// ---- M5-B 콘텐츠 제작 도구 패널(타일맵·팔레트·애니메이션 에디터) ----
std::unique_ptr<IEditorPanelFactory> MakeTilemapEditorPanelFactory();
std::unique_ptr<IEditorPanelFactory> MakeTilePalettePanelFactory();
std::unique_ptr<IEditorPanelFactory> MakeAnimationEditorPanelFactory();

// ---- 도트(픽셀아트) 에디터 패널 ----
std::unique_ptr<IEditorPanelFactory> MakeDotEditorPanelFactory();

// 콘솔 로그 싱크를 프로세스 전역 Log 에 1회 장착(EditorApp 초기화가 호출).
void InstallConsoleLogSink();

// 씬 뷰포트가 "MYE_ASSET" 드롭 수신 시 호출 — vpath 종류별 생성 커맨드 발행(Undo 가능).
//   worldPos = 드롭 지점의 월드 좌표(뷰포트가 픽 좌표를 넘긴다).
void InstantiateAssetToWorld(EditorContext& ctx, std::string_view vpath, Vec2 worldPos);

} // namespace mye::editor
