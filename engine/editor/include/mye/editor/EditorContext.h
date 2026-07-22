// mye/editor/EditorContext.h — 패널·커맨드·확장이 받는 서비스 게이트웨이 (docs/07)
//
// 07 API 스케치: onGui(EditorContext&), execute(EditorContext&) 등이 받는 컨텍스트.
// 에디터의 모든 하위 서비스(선택·커맨드·플레이모드·확장·현재 World)로 가는 유일한 통로다.
// 패널이 다른 패널을 직접 참조하지 않고 이 컨텍스트를 경유하도록 결합을 낮춘다.
//
// 수명: EditorApp이 소유. EditorContext는 서비스 포인터 묶음(값 취급, 프레임마다 재구성 가능).
#pragma once

#include "mye/editor/EditorTypes.h"

namespace mye::editor {

// 커맨드·패널·확장이 받는 서비스 접근점. 비소유 포인터 묶음.
struct EditorContext {
    EditorApp*               app = nullptr;
    EngineContext*           engine = nullptr;   // 01 서비스 게이트웨이(이벤트·잡·시간)
    EventBus*                events = nullptr;    // 에디터 전역 이벤트 버스(선택 변경 등)

    SelectionManager*        selection = nullptr;
    PlayModeController*       playMode = nullptr;
    EditorExtensionRegistry* extensions = nullptr;
    ProjectContext*          project = nullptr;

    // 포커스 문서 — Ctrl+Z/저장/커맨드 발행 대상.
    Document*                activeDocument = nullptr;
    CommandStack*            commands = nullptr;  // activeDocument의 스택으로 위임(null 가능)

    // 패널이 표시·편집 대상으로 삼는 World. 07 §3: PlayModeController::activeWorld()가 정본.
    //   에디트 모드=편집 World, 플레이 모드=Play World. 패널은 이 경로로만 World를 얻는다.
    ecs::World* activeWorld() const;
};

} // namespace mye::editor
