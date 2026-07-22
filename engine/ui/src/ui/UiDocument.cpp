// mye/ui/UiDocument.cpp — UiDocument 인스턴스화 + WidgetFactory + refl/ser 등록
//
// 직렬화: UiDocument/UiNodeDesc/UiPropertyKV 를 리플렉션 등록(04 JsonArchive 왕복).
//   AnchorRect 는 core Vec2(비리플렉션)를 담으므로 CustomSerialize 훅으로 float 를 평탄화한다.
//   재귀 children(std::vector<UiNodeDesc>)은 GetType<UiNodeDesc>() 지연 해석으로 동작.
#include "mye/ui/UiDocument.h"
#include "mye/ui/Widgets.h"
#include "mye/core/Base.h"

#include "mye/refl/TypeBuilder.h"
#include "mye/ser/Serialize.h"
#include "mye/ser/Archive.h"
#include "mye/ser/JsonArchive.h"
#include "mye/core/Json.h"

// -----------------------------------------------------------------------------
// 리플렉션 등록 — 전역 스코프.
// -----------------------------------------------------------------------------
MYE_REFLECT(mye::ui::UiPropertyKV);
MYE_REFLECT(mye::ui::AnchorRect);
MYE_REFLECT(mye::ui::UiNodeDesc);
MYE_REFLECT(mye::ui::UiDocument);

namespace {
// AnchorRect 커스텀 직렬화: Vec2 6개 + sizeDelta 를 평탄한 f64 키로 왕복(core Vec2 비리플렉션 회피).
void SerializeAnchorRect(mye::ser::IArchive& ar, void* instance) {
    auto& a = *static_cast<mye::ui::AnchorRect*>(instance);
    auto rw = [&](const char* key, float& v) {
        ar.Key(key);
        double d = static_cast<double>(v);
        ar.Value(d);
        if (ar.IsReading()) v = static_cast<float>(d);
    };
    rw("anchorMinX", a.anchorMin.x); rw("anchorMinY", a.anchorMin.y);
    rw("anchorMaxX", a.anchorMax.x); rw("anchorMaxY", a.anchorMax.y);
    rw("pivotX", a.pivot.x);         rw("pivotY", a.pivot.y);
    rw("offMinX", a.offsetMin.x);    rw("offMinY", a.offsetMin.y);
    rw("offMaxX", a.offsetMax.x);    rw("offMaxY", a.offsetMax.y);
    rw("sizeX", a.sizeDelta.x);      rw("sizeY", a.sizeDelta.y);
}

// UiNodeDesc 커스텀 직렬화 — children(std::vector<UiNodeDesc>)의 자기참조 필드를 리플렉션
//   등록 시점에 GetType<UiNodeDesc>() 로 재진입시키면 함수-로컬 static 재귀 초기화 데드락이
//   난다. 그래서 노드는 필드 등록 대신 커스텀 훅으로 수동 재귀 직렬화한다(등록 시점 재진입 없음).
void SerializeNodeDesc(mye::ser::IArchive& ar, void* instance);   // 전방(재귀).

void SerializeNodeVector(mye::ser::IArchive& ar, std::vector<mye::ui::UiNodeDesc>& v) {
    std::size_t count = ar.IsReading() ? 0 : v.size();
    ar.BeginArray(count);
    if (ar.IsReading()) v.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        std::uint32_t ver = 1;
        ar.BeginObject("mye::ui::UiNodeDesc", ver);
        SerializeNodeDesc(ar, &v[i]);
        ar.EndObject();
    }
    ar.EndArray();
}

void SerializeNodeDesc(mye::ser::IArchive& ar, void* instance) {
    auto& n = *static_cast<mye::ui::UiNodeDesc*>(instance);
    ar.Key("typeName");   ar.Value(n.typeName);
    ar.Key("name");       ar.Value(n.name);
    ar.Key("styleClass"); ar.Value(n.styleClass);
    // anchors(리플렉션 struct — CustomSerialize 훅 보유)를 SerializeDynamic 으로 위임.
    ar.Key("anchors");
    if (const auto* at = mye::refl::GetType<mye::ui::AnchorRect>())
        (void)mye::ser::SerializeDynamic(ar, *at, &n.anchors);
    // properties(std::vector<UiPropertyKV> — 비재귀, 리플렉션 등록 안전).
    ar.Key("properties");
    if (const auto* pt = mye::refl::GetType<std::vector<mye::ui::UiPropertyKV>>())
        (void)mye::ser::SerializeDynamic(ar, *pt, &n.properties);
    // children(자기참조) — 수동 재귀.
    ar.Key("children");
    SerializeNodeVector(ar, n.children);
}
} // namespace

template <> void mye::refl::Reflect(TypeBuilder<mye::ui::UiPropertyKV>& b) {
    b.Version(1)
     .Field("key", &mye::ui::UiPropertyKV::key)
     .Field("value", &mye::ui::UiPropertyKV::value);
}
template <> void mye::refl::Reflect(TypeBuilder<mye::ui::AnchorRect>& b) {
    b.Version(1).CustomSerialize(&SerializeAnchorRect);
}
template <> void mye::refl::Reflect(TypeBuilder<mye::ui::UiNodeDesc>& b) {
    // 자기참조 children 때문에 필드 등록 대신 커스텀 훅으로 수동 재귀(등록 시 재진입 회피).
    b.Version(1).CustomSerialize(&SerializeNodeDesc);
}
template <> void mye::refl::Reflect(TypeBuilder<mye::ui::UiDocument>& b) {
    b.Version(1)
     .Field("root", &mye::ui::UiDocument::root)
     .Field("controllerScript", &mye::ui::UiDocument::controllerScript)
     .Field("version", &mye::ui::UiDocument::version);
}

namespace mye::ui {

// --- 내장 위젯 생성 함수 ---
template <typename T> static WidgetPtr MakeWidget() { return std::make_unique<T>(); }

WidgetFactory::WidgetFactory() {
    Register("Panel",       &MakeWidget<Panel>);
    Register("Label",       &MakeWidget<Label>);
    Register("Image",       &MakeWidget<Image>);
    Register("Button",      &MakeWidget<Button>);
    Register("StackLayout", &MakeWidget<StackLayout>);
    Register("GridLayout",  &MakeWidget<GridLayout>);
    Register("Window",      &MakeWidget<Window>);
}

void WidgetFactory::Register(std::string_view typeName, CreateFn fn) {
    m_factories.emplace_back(HashFnv1a64(typeName), fn);
}

WidgetPtr WidgetFactory::Create(std::string_view typeName) const {
    const uint64_t h = HashFnv1a64(typeName);
    for (const auto& [key, fn] : m_factories)
        if (key == h) return fn();
    return nullptr;
}

bool WidgetFactory::ApplyProperties(Widget& w, const std::vector<UiPropertyKV>& props) const {
    // TODO(impl): 위젯 타입별 KV 적용(Label.text, Image.sprite vpath, Button.caption 등).
    //   MVP: Label.text / Window.title 정도만. 후속 리플렉션 기반 일반화.
    if (Label* lbl = w.As<Label>()) {
        for (const auto& kv : props)
            if (kv.key == "text") lbl->setText(kv.value);
    } else if (Window* win = w.As<Window>()) {
        for (const auto& kv : props)
            if (kv.key == "title") win->title = kv.value;
    }
    return true;
}

// --- 트리 인스턴스화 ---
static Expected<WidgetPtr, Error> InstantiateNode(const UiNodeDesc& node,
                                                  const WidgetFactory& factory) {
    WidgetPtr w = factory.Create(node.typeName);
    if (!w)
        return Error{"UiDocument: unknown widget type '" + node.typeName + "'", 1};
    w->name = node.name;
    w->anchors = node.anchors;
    w->styleClass = MakeStyleClass(node.styleClass);
    factory.ApplyProperties(*w, node.properties);
    for (const UiNodeDesc& childDesc : node.children) {
        auto child = InstantiateNode(childDesc, factory);
        if (!child) return child.GetError();
        w->addChild(std::move(child).Value());
    }
    return w;
}

Expected<WidgetPtr, Error> UiDocument::Instantiate(const WidgetFactory& factory) const {
    return InstantiateNode(root, factory);
}

// --- JSON 왕복 ---
Expected<std::string, Error> SaveDocumentJson(const UiDocument& doc) {
    UiDocument copy = doc;   // Serialize 는 non-const 참조.
    auto wr = ser::JsonArchive::ForWrite();
    auto r = ser::Serialize(wr, copy);
    if (!r) return r.GetError();
    return json::Stringify(wr.Root());
}

Expected<UiDocument, Error> LoadDocumentJson(std::string_view jsonText) {
    auto parsed = json::Parse(jsonText);
    if (!parsed) return parsed.GetError();
    UiDocument doc;
    auto rd = ser::JsonArchive::ForRead(parsed.Value());
    auto r = ser::Serialize(rd, doc);
    if (!r) return r.GetError();
    return doc;
}

} // namespace mye::ui
