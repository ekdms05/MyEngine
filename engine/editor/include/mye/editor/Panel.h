// mye/editor/Panel.h — 패널 인터페이스 + 매니저 + 도킹 (docs/07 §7, 패널 공통 규약)
//
// 07: 모든 패널은 IEditorPanel 구현체로 PanelManager에 등록(내장·플러그인 동일 경로).
//   고유 문자열 ID, 표시 이름, 다중 인스턴스 허용 여부, 직렬화 가능한 자체 상태. Window 메뉴는
//   등록된 패널 목록에서 자동 생성. buildDockspaceAndDrawAll이 도킹 스페이스 + 전체 드로우.
//
// 레이아웃 저장: ImGui ini(<project>/.myeditor/layout.ini) + 자체 패널 목록/상태(session.json).
//   씬 파일에는 절대 저장 금지(07 §6).
#pragma once

#include "mye/editor/EditorTypes.h"
#include "mye/core/Json.h"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mye::editor {

// 패널 기본 도킹 위치 힌트(Default 프리셋 자동 배치, 07 §7).
enum class DockSlot : std::uint8_t {
    Center,   // 씬 뷰포트
    Left,     // 하이어라키
    Right,    // 인스펙터
    Bottom,   // 에셋·콘솔 탭 그룹
    Floating, // 초기 미도킹
};

// 패널 종류 서술자 — 팩토리가 반환. Window 메뉴·도킹 배치 근거.
struct PanelDesc {
    PanelTypeId id;                     // "mye.hierarchy" 등 고유 문자열
    std::string title;                  // 표시 이름(한국어)
    bool        allowMultiple = false;  // 다중 인스턴스 허용(뷰포트·인스펙터 등)
    DockSlot    defaultDock = DockSlot::Floating;
};

// 에디터 이벤트(패널 onEvent 수신) — 01 이벤트 버스와 별개의 패널 로컬 통지 채널.
//   선택 변경·플레이 상태 변경 등을 얇게 전달(상세는 컨텍스트에서 조회).
struct EditorEvent {
    enum class Kind : std::uint8_t { SelectionChanged, PlayStateChanged, DocumentChanged };
    Kind kind = Kind::SelectionChanged;
};

// 패널 인스턴스 인터페이스(07 API 스케치).
class IEditorPanel {
public:
    virtual ~IEditorPanel() = default;

    virtual const PanelDesc& Desc() const = 0;
    virtual void OnGui(EditorContext& ctx) = 0;             // 매 프레임 ImGui 빌드
    virtual void OnEvent(const EditorEvent& /*ev*/) {}      // 선택·플레이 상태 변경 등
    virtual void SerializeState(json::Value& /*out*/) const {}  // 레이아웃 저장용 자체 상태
    virtual void DeserializeState(const json::Value& /*in*/) {}
};

// 패널 팩토리 — 등록 단위(내장·플러그인 공용). 다중 인스턴스는 Create를 반복 호출.
class IEditorPanelFactory {
public:
    virtual ~IEditorPanelFactory() = default;
    virtual const PanelDesc& Desc() const = 0;
    virtual std::unique_ptr<IEditorPanel> Create() = 0;
};

// 패널 매니저 — 등록·오픈·도킹 스페이스 빌드·Window 메뉴 자동 생성(07 §7).
class PanelManager {
public:
    PanelManager();
    ~PanelManager();
    PanelManager(const PanelManager&) = delete;
    PanelManager& operator=(const PanelManager&) = delete;

    // 팩토리 등록(내장·플러그인 공용). 등록 순서가 Window 메뉴 순서.
    void RegisterFactory(std::unique_ptr<IEditorPanelFactory> factory);

    // 패널 인스턴스 오픈. 다중 불가 패널이 이미 열려 있으면 기존 인스턴스로 포커스.
    PanelInstanceId Open(std::string_view panelId);
    void Close(PanelInstanceId id);
    bool IsOpen(std::string_view panelId) const;

    // 메인 도킹 스페이스 구성 + Window 메뉴 자동 항목 + 전체 패널 OnGui 호출(07 §7).
    void BuildDockspaceAndDrawAll(EditorContext& ctx);

    // 열린 패널·자체 상태를 json으로(session.json). 레이아웃 복원 시 역.
    void SerializeLayout(json::Value& out) const;
    void DeserializeLayout(const json::Value& in);

    // 모든 등록 팩토리(Window 메뉴·플러그인 매니저 집계).
    std::span<const PanelDesc> RegisteredPanels() const;

    void BroadcastEvent(const EditorEvent& ev);   // 전 패널 OnEvent 전달

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mye::editor
