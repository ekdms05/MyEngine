// mye/ui/UiDocument.cpp — UiDocument 인스턴스화 + WidgetFactory + refl/ser 등록
//
// 직렬화: UiDocument/UiNodeDesc/UiPropertyKV 를 리플렉션 등록(04 JsonArchive 왕복).
//   AnchorRect 는 core Vec2(비리플렉션)를 담으므로 CustomSerialize 훅으로 float 를 평탄화한다.
//   재귀 children(std::vector<UiNodeDesc>)은 GetType<UiNodeDesc>() 지연 해석으로 동작.
#include "mye/ui/UiDocument.h"
#include "mye/asset/AssetDatabase.h"
#include "mye/asset/AssetManager.h"
#include "mye/ui/Widgets.h"
#include "mye/core/Base.h"

#include "mye/refl/TypeBuilder.h"
#include "mye/ser/Serialize.h"
#include "mye/ser/Archive.h"
#include "mye/ser/JsonArchive.h"
#include "mye/core/Json.h"
#include "mye/asset/Texture.h"
#include "mye/text/RichText.h"
#include <charconv>
#include <cmath>
#include <unordered_set>

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
namespace {
Expected<void, Error> BindSprites(ui::Widget& node, asset::AssetDatabase& database,
    asset::AssetManager& assets, std::vector<asset::AssetHandle<asset::Texture>>& handles) {
    ui::UiSprite* sprite = nullptr;
    if (node.styleClass.IsValid()) return Error{node.name + ": GameUi does not load a skin; use inline properties", 1};
    if (auto* image = node.As<ui::Image>()) sprite = &image->sprite;
    else if (auto* window = node.As<ui::Window>()) sprite = &window->background;
    else if (auto* panel = node.As<ui::Panel>()) sprite = &panel->background;
    else if (auto* button = node.As<ui::Button>()) sprite = &button->normalSprite;
    if (sprite && sprite->assetRef.guid.IsValid()) {
        const auto path = database.PathFromGuid(sprite->assetRef.guid);
        if (!path.ends_with(".png")) return Error{node.name + ": UI texture GUID does not resolve to a project PNG", 1};
        auto loaded = assets.LoadSync<asset::Texture>(path);
        const auto* texture = loaded.Get();
        if (!texture || !texture->gpuTexture.IsValid()) return Error{node.name + ": cannot load UI texture " + path, 1};
        auto region = sprite->source;
        if (region.w == 0 && region.h == 0) region = {0, 0, static_cast<int32_t>(texture->width), static_cast<int32_t>(texture->height)};
        if (region.x < 0 || region.y < 0 || region.w <= 0 || region.h <= 0 ||
            uint64_t(region.x) + region.w > texture->width || uint64_t(region.y) + region.h > texture->height)
            return Error{node.name + ": UI source region exceeds its PNG", 1};
        sprite->texture = texture->gpuTexture;
        sprite->nativeSize = {region.w, region.h};
        sprite->uv = {float(region.x) / texture->width, float(region.y) / texture->height,
            float(region.w) / texture->width, float(region.h) / texture->height};
        handles.push_back(std::move(loaded));
    } else if (sprite && (sprite->source.w != 0 || sprite->source.h != 0)) {
        return Error{node.name + ": UI source region requires a texture GUID", 1};
    }
    for (const auto& child : node.children()) {
        auto bound = BindSprites(*child, database, assets, handles);
        if (!bound) return bound.GetError();
    }
    return {};
}
}

Expected<UiInstance, Error> InstantiateGameUi(const UiDocument& document,
    asset::AssetDatabase& database, asset::AssetManager& assets) {
    if (!document.controllerScript.empty())
        return Error{"controllerScript is not connected; use ObjectBehavior Lua callbacks for local UI updates", 1};
    auto root = document.Instantiate(WidgetFactory{});
    if (!root) return root.GetError();
    UiInstance instance;
    instance.root = std::move(root).Value();
    auto bound = BindSprites(*instance.root, database, assets, instance.textures);
    if (!bound) return bound.GetError();
    return instance;
}


// --- 내장 위젯 생성 함수 ---
template <typename T> static WidgetPtr MakeWidget() { return std::make_unique<T>(); }

WidgetFactory::WidgetFactory() {
    Register("Panel",       &MakeWidget<Panel>);
    Register("Label",       &MakeWidget<Label>);
    Register("Image",       &MakeWidget<Image>);
    Register("ProgressBar", &MakeWidget<ProgressBar>);
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

namespace {
Expected<float, Error> Number(std::string_view text) {
    float value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(value) || std::abs(value) > 32768)
        return Error{"UI number must be finite and within +/-32768", 1};
    return value;
}
Expected<Color, Error> Colour(std::string_view text) {
    if ((text.size() != 7 && text.size() != 9) || text.front() != '#') return Error{"UI colour requires #RRGGBB or #RRGGBBAA", 1};
    for (const char c : text.substr(1))
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return Error{"UI colour contains a non-hex digit", 1};
    return text::ParseHexColor(text);
}
Expected<RectInt, Error> Region(std::string_view text) {
    int32_t fields[4]{};
    for (int i = 0; i < 4; ++i) {
        const auto comma = text.find(',');
        const auto token = text.substr(0, comma);
        const auto parsed = std::from_chars(token.data(), token.data() + token.size(), fields[i]);
        if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() || fields[i] < 0 || fields[i] > 8192 ||
            (i < 3 && comma == std::string_view::npos) || (i == 3 && comma != std::string_view::npos))
            return Error{"UI source requires x,y,w,h integers in [0,8192]", 1};
        if (i < 3) text.remove_prefix(comma + 1);
    }
    if (fields[2] == 0 || fields[3] == 0) return Error{"UI source width/height must be positive", 1};
    return RectInt{fields[0], fields[1], fields[2], fields[3]};
}
}

Expected<void, Error> WidgetFactory::ApplyProperties(Widget& w, const std::vector<UiPropertyKV>& props) const {
    auto* label = w.As<Label>();
    auto* window = w.As<Window>();
    Panel* panel = window ? static_cast<Panel*>(window) : w.As<Panel>();
    auto* image = w.As<Image>();
    auto* progress = w.As<ProgressBar>();
    auto* button = w.As<Button>();
    std::unordered_set<std::string_view> keys;
    for (const auto& [key, value] : props) {
        if (!keys.insert(key).second) return Error{"Duplicate UI property: " + key, 1};
        if (key == "visible" || key == "interactive" || key == "clip" || key == "background" || key == "enabled" || key == "modal") {
            if (value != "true" && value != "false") return Error{"UI boolean requires true or false: " + key, 1};
            const bool enabled = value == "true";
            if (key == "visible") w.visibility = enabled ? Visibility::Visible : Visibility::Hidden;
            else if (key == "interactive") w.interactive = enabled;
            else if (key == "clip") w.clipChildren = enabled;
            else if (key == "modal" && panel) w.modal = enabled;
            else if (key == "background" && panel) panel->drawBackground = enabled;
            else if (key == "enabled" && button) button->state = enabled ? Button::State::Normal : Button::State::Disabled;
            else return Error{"UI property is unsupported by " + w.name + ": " + key, 1};
        } else if (key == "text" && label) label->setText(value);
        else if (key == "title" && window) window->title = value;
        else if (key == "fontSize" && label) {
            auto number = Number(value); if (!number) return number.GetError();
            if (number.Value() < 8 || number.Value() > 96 || std::floor(number.Value()) != number.Value())
                return Error{"UI fontSize requires an integer in [8,96]", 1};
            label->style.size = static_cast<uint16_t>(number.Value());
        } else if (key == "colour" && label) {
            auto colour = Colour(value); if (!colour) return colour.GetError(); label->style.color = colour.Value();
        } else if (key == "tint" && (panel || image)) {
            auto colour = Colour(value); if (!colour) return colour.GetError();
            if (panel) panel->tint = colour.Value(); else image->tint = colour.Value();
        } else if ((key == "fill" || key == "track") && progress) {
            auto colour = Colour(value); if (!colour) return colour.GetError();
            if (key == "fill") progress->fill = colour.Value(); else progress->background = colour.Value();
        } else if ((key == "value" || key == "maximum") && progress) {
            auto number = Number(value); if (!number) return number.GetError();
            if (key == "value") progress->value = number.Value(); else progress->maximum = number.Value();
        } else if ((key == "texture" || key == "source") && (image || panel || button)) {
            auto& sprite = image ? image->sprite : panel ? panel->background : button->normalSprite;
            if (key == "texture") {
                auto guid = asset::AssetGuid::FromString(value);
                if (!guid || !guid.Value().IsValid()) return Error{"UI texture requires a valid GUID", 1};
                sprite.assetRef = {guid.Value(), asset::Texture::kAssetTypeId};
            } else {
                auto region = Region(value); if (!region) return region.GetError(); sprite.source = region.Value();
            }
        } else if (key == "spacing" && (w.As<StackLayout>() || w.As<GridLayout>())) {
            auto number = Number(value); if (!number) return number.GetError();
            if (number.Value() < 0) return Error{"UI spacing cannot be negative", 1};
            if (auto* stack = w.As<StackLayout>()) stack->spacing = number.Value(); else w.As<GridLayout>()->spacing = number.Value();
        } else return Error{"UI property is unsupported by " + w.name + ": " + key, 1};
    }
    if (panel && panel->background.assetRef.guid.IsValid() && !keys.contains("background")) panel->drawBackground = true;
    if (button && button->state == Button::State::Disabled) button->interactive = false;
    if (progress) return progress->SetValue(progress->value, progress->maximum);
    return {};
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
    // Decorative nodes pass input through; explicit interactive properties still override this.
    w->interactive = w->As<Button>() || w->As<Window>();
    auto applied = factory.ApplyProperties(*w, node.properties);
    if (!applied) return Error{node.name + ": " + applied.GetError().message, 1};
    for (const UiNodeDesc& childDesc : node.children) {
        auto child = InstantiateNode(childDesc, factory);
        if (!child) return child.GetError();
        w->addChild(std::move(child).Value());
    }
    return w;
}

Expected<WidgetPtr, Error> UiDocument::Instantiate(const WidgetFactory& factory) const {
    auto valid = Validate(&factory); if (!valid) return valid.GetError();
    return InstantiateNode(root, factory);
}

Expected<void, Error> UiDocument::Validate(const WidgetFactory* customFactory) const {
    if (version != 1 || controllerScript.size() > 4096) return Error{"UI document requires version 1 and a bounded controller path", 1};
    std::unordered_set<std::string_view> names;
    WidgetFactory builtins;
    const auto& factory = customFactory ? *customFactory : builtins;
    size_t count = 0;
    auto check = [&](auto&& self, const UiNodeDesc& node, int depth) -> Expected<void, Error> {
        if (++count > 512 || depth > 24) return Error{"UI document exceeds 512 nodes or 24 levels", 1};
        if (node.typeName.empty() || node.typeName.size() > 64 || node.name.size() > 64 || node.styleClass.size() > 128 ||
            node.name.find('\0') != std::string::npos || (!node.name.empty() && !names.insert(node.name).second))
            return Error{"UI node has an invalid type/name/style or duplicate name", 1};
        const auto& a = node.anchors;
        for (const auto v : {a.anchorMin.x,a.anchorMin.y,a.anchorMax.x,a.anchorMax.y,a.pivot.x,a.pivot.y})
            if (!std::isfinite(v) || v < 0 || v > 1) return Error{"UI anchors and pivot must be in [0,1]", 1};
        if (a.anchorMin.x > a.anchorMax.x || a.anchorMin.y > a.anchorMax.y || a.sizeDelta.x < 0 || a.sizeDelta.y < 0)
            return Error{"UI anchors must be ordered and sizes nonnegative", 1};
        for (const auto v : {a.offsetMin.x,a.offsetMin.y,a.offsetMax.x,a.offsetMax.y,a.sizeDelta.x,a.sizeDelta.y})
            if (!std::isfinite(v) || std::abs(v) > 32768) return Error{"UI offsets/sizes must be finite within +/-32768", 1};
        if (node.properties.size() > 32) return Error{"UI node exceeds 32 properties", 1};
        auto probe = factory.Create(node.typeName);
        if (!probe) return Error{"Unknown UI widget: " + node.typeName, 1};
        for (const auto& property : node.properties)
            if (property.key.size() > 64 || property.value.size() > 4096) return Error{"UI property exceeds its text limit", 1};
        auto applied = factory.ApplyProperties(*probe, node.properties); if (!applied) return applied.GetError();
        for (const auto& child : node.children) { auto valid = self(self, child, depth + 1); if (!valid) return valid.GetError(); }
        return {};
    };
    return check(check, root, 1);
}

// Validate JSON types before the tolerant archive can default malformed data.
namespace {
bool VersionOne(const json::Value& value) {
    const auto* version = value.Find("__version");
    return version && version->IsInteger() && version->AsInt() == 1;
}
Expected<void, Error> CheckNodeJson(const json::Value& node, size_t& count, int depth) {
    if (!node.IsObject() || !VersionOne(node) || ++count > 512 || depth > 24) return Error{"UI JSON node/version/size/depth is invalid", 1};
    for (const char* key : {"typeName", "name", "styleClass"}) {
        const auto* value = node.Find(key); if (!value || !value->IsString()) return Error{std::string("UI JSON requires string ") + key, 1};
    }
    const auto* anchors = node.Find("anchors"), *properties = node.Find("properties"), *children = node.Find("children");
    if (!anchors || !anchors->IsObject() || !VersionOne(*anchors) || !properties || !properties->IsArray() || properties->AsArray().size() > 32 || !children || !children->IsArray())
        return Error{"UI JSON requires anchors object and bounded properties/children arrays", 1};
    for (const char* key : {"anchorMinX","anchorMinY","anchorMaxX","anchorMaxY","pivotX","pivotY","offMinX","offMinY","offMaxX","offMaxY","sizeX","sizeY"}) {
        const auto* value = anchors->Find(key); if (!value || !value->IsNumber() || !std::isfinite(value->AsDouble())) return Error{"UI JSON anchor must be a finite number", 1};
    }
    for (const auto& property : properties->AsArray()) {
        const auto* key = property.Find("key"), *value = property.Find("value");
        if (!property.IsObject() || !VersionOne(property) || !key || !key->IsString() || !value || !value->IsString()) return Error{"UI JSON property requires version 1 and string key/value", 1};
    }
    for (const auto& child : children->AsArray()) { auto valid = CheckNodeJson(child, count, depth + 1); if (!valid) return valid.GetError(); }
    return {};
}
}

// --- JSON 왕복 ---
Expected<std::string, Error> SaveDocumentJson(const UiDocument& doc) {
    auto valid = doc.Validate(); if (!valid) return valid.GetError();
    UiDocument copy = doc;   // Serialize 는 non-const 참조.
    auto wr = ser::JsonArchive::ForWrite();
    auto r = ser::Serialize(wr, copy);
    if (!r) return r.GetError();
    auto encoded = json::Stringify(wr.Root());
    if (encoded.size() > 4 * 1024 * 1024) return Error{"UI document exceeds 4 MiB", 1};
    return encoded;
}

Expected<UiDocument, Error> LoadDocumentJson(std::string_view jsonText) {
    if (jsonText.size() > 4 * 1024 * 1024) return Error{"UI document exceeds 4 MiB", 1};
    auto parsed = json::Parse(jsonText);
    if (!parsed) return parsed.GetError();
    const auto* version = parsed.Value().Find("version"), *root = parsed.Value().Find("root"), *controller = parsed.Value().Find("controllerScript");
    if (!parsed.Value().IsObject() || !VersionOne(parsed.Value()) || !version || !version->IsInteger() || version->AsInt() != 1 || !root || !controller || !controller->IsString())
        return Error{"UI JSON requires root, string controllerScript and version 1", 1};
    size_t count = 0; auto shape = CheckNodeJson(*root, count, 1); if (!shape) return shape.GetError();
    UiDocument doc;
    auto rd = ser::JsonArchive::ForRead(parsed.Value());
    auto r = ser::Serialize(rd, doc);
    if (!r) return r.GetError();
    auto valid = doc.Validate(); if (!valid) return valid.GetError();
    return doc;
}

} // namespace mye::ui
