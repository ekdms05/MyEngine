// mye/ui/UiDocument.h — UI 를 데이터로 정의하는 에셋 (docs/06 §1 UI=데이터+Lua)
//
// 소유: 06 (M5-A). 화면 하나 = UiDocument 에셋(위젯 트리 선언, 04 직렬화 프레임워크 포맷).
//   문서마다 Lua 컨트롤러 스크립트를 바인딩(바인딩 구현은 05, 표면 정의는 06). Instantiate 로
//   위젯 트리를 생성하고, find(name) 으로 위젯을 조회한다.
//
// 직렬화: refl/ser 로 UiNodeDesc 트리를 텍스트/바이너리 왕복. 위젯 타입은 typeName 문자열로
//   기록하고, WidgetFactory(UiSystem 등록)가 이름→인스턴스 생성(커스텀 위젯 플러그인 확장점).
#pragma once

#include "mye/ui/UiTypes.h"
#include "mye/ui/Widget.h"
#include "mye/core/Base.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mye::ui {

class WidgetFactory;

// 직렬화되는 위젯 노드 서술(트리). 04 리플렉션 등록 대상(MYE_REFLECT in UiDocument.cpp).
//   위젯별 프로퍼티는 MVP 에서 공통 필드 + 문자열 KV(properties) 로 표현(스키마 진화 유연).
//   후속에 위젯 타입별 강타입 프로퍼티(리플렉션)로 승격.
struct UiPropertyKV {
    std::string key;
    std::string value;    // 문자열 인코딩(색="#RRGGBB", 벡터="x,y" 등)
};

struct UiNodeDesc {
    std::string typeName;                 // "Panel", "Label", "Button" ... (WidgetFactory 키)
    std::string name;                     // find() 조회 키
    std::string styleClass;               // UiSkin 키
    AnchorRect  anchors{};
    std::vector<UiPropertyKV> properties; // 위젯별 속성(text, sprite vpath 등)
    std::vector<UiNodeDesc>   children;
};

// UiDocument 에셋. 루트 노드 서술 + 컨트롤러 스크립트 vpath.
struct UiDocument {
    UiNodeDesc  root;
    std::string controllerScript;         // Lua vpath(옵션) — 05 가 바인딩
    uint32_t    version = 1;

    // 위젯 트리 인스턴스화. factory 로 typeName→Widget 생성, 프로퍼티 적용, 앵커·스킨 세팅.
    //   실패(알 수 없는 타입 등) 시 Error. 성공 시 루트 위젯 소유 반환.
    Expected<WidgetPtr, Error> Instantiate(const WidgetFactory& factory) const;
};

// UiDocument ↔ JSON 텍스트 왕복(04 리플렉션/JsonArchive). 에셋 저장·로드·에디터 diff 용.
//   SaveDocumentJson: doc → UTF-8 JSON 문자열. LoadDocumentJson: JSON → doc.
Expected<std::string, Error> SaveDocumentJson(const UiDocument& doc);
Expected<UiDocument, Error>  LoadDocumentJson(std::string_view json);

// typeName → Widget 생성 팩토리. 내장 위젯 등록 + 커스텀 위젯 플러그인 등록점(docs/06 확장 §1).
class WidgetFactory {
public:
    using CreateFn = WidgetPtr (*)();

    WidgetFactory();                                   // 내장 위젯 등록(Panel/Label/...)
    void Register(std::string_view typeName, CreateFn fn);
    WidgetPtr Create(std::string_view typeName) const; // 미등록 시 nullptr

    // 프로퍼티 적용 훅: 위젯 타입별 KV → 필드 세팅(MVP 는 내장 위젯 스위치, 후속 리플렉션).
    bool ApplyProperties(Widget& w, const std::vector<UiPropertyKV>& props) const;

private:
    std::vector<std::pair<uint64_t, CreateFn>> m_factories;   // FNV(typeName) → fn
};

} // namespace mye::ui
