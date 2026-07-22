// Inspector.cpp — 리플렉션 자동 인스펙터 + 커스텀 드로어 훅 (docs/07 인스펙터)
//
// 07 인스펙터: 선택 엔티티의 컴포넌트를 열거→refl::TypeInfo 필드 순회→타입별 ImGui 위젯.
//   값 변경 시 PropertyEditCommand(경로 + before/after 값 blob)를 발행한다 — 패널은 값을 직접
//   쓰지 않는다(모든 편집은 IEditorCommand 경유). 커스텀 드로어(타입 단위)·컴포넌트 에디터가
//   EditorExtensionRegistry에 등록돼 있으면 우선 사용한다.
//
// [값 blob] 07 §4·04 규약: 리프 값 하나를 refl::ReadValue(→JsonArchive) 로 직렬화해
//   ValueBlob{json}로 만든다. Undo는 이 before/after 를 refl::WriteValue로 되돌린다(M4-B).
//
// [편집 흐름] 위젯은 매 프레임 인스턴스의 라이브 값을 읽어 초기화된다(인스턴스가 진실).
//   ImGui가 편집 활성화 시작을 알리면 그 시점 값을 before 로 스냅샷하고, 편집 종료(비활성화)
//   시점에 after 로 PropertyEditCommand를 push한다. 드래그 도중 프레임마다 다시 push하지 않아
//   중복 커맨드를 피한다(연속 드래그 → 편집 종료 시 1개). CommandStack::TryMerge와도 정합.
//
// [헤더 동결] Inspector.h 공개 표면(DrawEntity/DrawReflected/SetDebugMode)만 노출한다.
//   모든 위젯·재귀·커맨드 로직은 Impl 내부/파일 로컬 함수로 은닉한다.
#include "mye/editor/Inspector.h"

#include "mye/editor/Command.h"
#include "mye/editor/CommandStack.h"
#include "mye/editor/EditorContext.h"
#include "mye/editor/ExtensionRegistry.h"

#include "mye/asset/AssetGuid.h"
#include "mye/core/Json.h"
#include "mye/core/Math.h"
#include "mye/ecs/ComponentType.h"
#include "mye/ecs/World.h"
#include "mye/refl/TypeId.h"
#include "mye/refl/TypeInfo.h"
#include "mye/refl/TypeRegistry.h"
#include "mye/ser/JsonArchive.h"
#include "mye/ser/Serialize.h"

#include "imgui.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace mye::editor {

namespace {

ecs::ComponentTypeId ComponentIdOf(const refl::TypeInfo& t) {
    return static_cast<ecs::ComponentTypeId>(refl::TypeIdFromName(t.Name()));
}

const refl::Attribute* FindAttr(const refl::FieldInfo* f, refl::AttributeKind k) {
    return f ? f->FindAttribute(k) : nullptr;
}
bool HasAttr(const refl::FieldInfo* f, refl::AttributeKind k) {
    return FindAttr(f, k) != nullptr;
}

// 리플렉션 이름이 접미사(Vec2/Vec3/Color 등)로 끝나는가(네임스페이스 무관).
bool IsNamed(const refl::TypeInfo& t, std::string_view suffix) {
    const std::string_view n = t.Name();
    if (n == suffix) return true;
    if (n.size() > suffix.size() && n.substr(n.size() - suffix.size()) == suffix &&
        n[n.size() - suffix.size() - 1] == ':')
        return true;
    return false;
}

// 리프 값 하나(컴포넌트 루트 + 경로) → ValueBlob(JSON 텍스트). 실패 시 빈 blob.
ValueBlob ReadLeafBlob(const refl::TypeInfo& rootType, const void* rootComp,
                       const refl::PropertyPath& path) {
    if (!rootComp) return ValueBlob{};
    auto wr = ser::JsonArchive::ForWrite();
    auto r = refl::ReadValue(rootType, rootComp, path, wr);
    if (!r) return ValueBlob{};
    return ValueBlob{json::Stringify(wr.Root(), 0)};
}

// ---- 위젯: 값 포인터 직접 편집(반환 = 이번 프레임 변경 여부) ----

bool WidgetPrimitive(const refl::TypeInfo& type, const refl::FieldInfo* field, void* ptr,
                     const std::string& label) {
    const std::string_view n = type.Name();
    const std::string id = "##" + label;

    float rmin = 0.0f, rmax = 0.0f, rstep = 0.0f;
    bool hasRange = false;
    if (const refl::Attribute* ra = FindAttr(field, refl::AttributeKind::Range)) {
        rmin = static_cast<float>(ra->rangeMin);
        rmax = static_cast<float>(ra->rangeMax);
        rstep = static_cast<float>(ra->rangeStep);
        hasRange = true;
    }

    ImGui::TextUnformatted(label.c_str());
    ImGui::SameLine();

    if (n == "bool") return ImGui::Checkbox(id.c_str(), static_cast<bool*>(ptr));
    if (n == "f32") {
        return ImGui::DragFloat(id.c_str(), static_cast<float*>(ptr),
                                hasRange && rstep > 0 ? rstep : 0.1f,
                                hasRange ? rmin : 0.0f, hasRange ? rmax : 0.0f);
    }
    if (n == "f64") {
        return ImGui::DragScalar(id.c_str(), ImGuiDataType_Double, ptr, 0.1f);
    }
    if (n == "string") {
        auto* s = static_cast<std::string*>(ptr);
        char buf[512];
        std::snprintf(buf, sizeof(buf), "%s", s->c_str());
        bool ch;
        if (HasAttr(field, refl::AttributeKind::Multiline))
            ch = ImGui::InputTextMultiline(id.c_str(), buf, sizeof(buf));
        else
            ch = ImGui::InputText(id.c_str(), buf, sizeof(buf));
        if (ch) *s = buf;
        return ch;
    }

    // 정수 계열.
    int iv = 0;
    const bool isU = (!n.empty() && n[0] == 'u');
    auto loadInt = [&](auto* p) { iv = static_cast<int>(*p); };
    if      (n == "i8")  loadInt(static_cast<std::int8_t*>(ptr));
    else if (n == "i16") loadInt(static_cast<std::int16_t*>(ptr));
    else if (n == "i32") loadInt(static_cast<std::int32_t*>(ptr));
    else if (n == "i64") loadInt(static_cast<std::int64_t*>(ptr));
    else if (n == "u8")  loadInt(static_cast<std::uint8_t*>(ptr));
    else if (n == "u16") loadInt(static_cast<std::uint16_t*>(ptr));
    else if (n == "u32") loadInt(static_cast<std::uint32_t*>(ptr));
    else if (n == "u64") loadInt(static_cast<std::uint64_t*>(ptr));
    else { ImGui::TextUnformatted("(?)"); return false; }

    bool ch;
    if (hasRange)
        ch = ImGui::DragInt(id.c_str(), &iv, rstep > 0 ? rstep : 1.0f,
                            static_cast<int>(rmin), static_cast<int>(rmax));
    else
        ch = ImGui::DragInt(id.c_str(), &iv);
    if (ch) {
        if (isU && iv < 0) iv = 0;
        auto store = [&](auto* p) { *p = static_cast<std::remove_reference_t<decltype(*p)>>(iv); };
        if      (n == "i8")  store(static_cast<std::int8_t*>(ptr));
        else if (n == "i16") store(static_cast<std::int16_t*>(ptr));
        else if (n == "i32") store(static_cast<std::int32_t*>(ptr));
        else if (n == "i64") store(static_cast<std::int64_t*>(ptr));
        else if (n == "u8")  store(static_cast<std::uint8_t*>(ptr));
        else if (n == "u16") store(static_cast<std::uint16_t*>(ptr));
        else if (n == "u32") store(static_cast<std::uint32_t*>(ptr));
        else if (n == "u64") store(static_cast<std::uint64_t*>(ptr));
    }
    return ch;
}

bool WidgetEnum(const refl::TypeInfo& type, void* ptr, const std::string& label) {
    const refl::EnumInfo* e = type.AsEnum();
    if (!e) return false;

    std::int64_t cur = 0;
    std::memcpy(&cur, ptr, type.Size() <= sizeof(cur) ? type.Size() : sizeof(cur));

    std::vector<std::string> names;
    int curIdx = 0, i = 0;
    for (const auto& c : e->Constants()) {
        names.emplace_back(c.name);
        if (c.value == cur) curIdx = i;
        ++i;
    }
    std::vector<const char*> cstrs;
    cstrs.reserve(names.size());
    for (auto& s : names) cstrs.push_back(s.c_str());

    ImGui::TextUnformatted(label.c_str());
    ImGui::SameLine();
    const std::string id = "##" + label;
    int sel = curIdx;
    bool ch = ImGui::Combo(id.c_str(), &sel, cstrs.data(), static_cast<int>(cstrs.size()));
    if (ch && sel >= 0 && sel < static_cast<int>(e->Constants().size())) {
        std::int64_t v = e->Constants()[sel].value;
        std::memcpy(ptr, &v, type.Size() <= sizeof(v) ? type.Size() : sizeof(v));
    }
    return ch;
}

bool WidgetAssetRef(void* ptr, const std::string& label) {
    auto* ref = static_cast<asset::AssetRef*>(ptr);
    ImGui::TextUnformatted(label.c_str());
    ImGui::SameLine();
    const std::string s = ref->guid.IsValid() ? ref->guid.ToString() : std::string("(none)");
    ImGui::TextUnformatted(s.c_str());
    return false;   // 참조 선택 UI(에셋 브라우저 드롭)은 M4-B.
}

// Vec/Color/Quat 특수 위젯. 처리하면 handled=true, 변경 여부는 반환.
bool WidgetSpecialStruct(const refl::TypeInfo& type, void* ptr, const std::string& label,
                         bool& handled) {
    handled = true;
    const std::string id = "##" + label;
    ImGui::TextUnformatted(label.c_str());
    ImGui::SameLine();
    if (IsNamed(type, "Vec2")) return ImGui::DragFloat2(id.c_str(), &static_cast<Vec2*>(ptr)->x, 0.1f);
    if (IsNamed(type, "Vec3")) return ImGui::DragFloat3(id.c_str(), &static_cast<Vec3*>(ptr)->x, 0.1f);
    if (IsNamed(type, "Vec4")) return ImGui::DragFloat4(id.c_str(), &static_cast<Vec4*>(ptr)->x, 0.1f);
    if (IsNamed(type, "Quat")) return ImGui::DragFloat4(id.c_str(), &static_cast<Quat*>(ptr)->x, 0.01f);
    if (IsNamed(type, "Color")) return ImGui::ColorEdit4(id.c_str(), &static_cast<Color*>(ptr)->r);
    // 미처리 — 특수 위젯 아님.
    ImGui::NewLine();   // SameLine 취소(레이아웃 정리)
    handled = false;
    return false;
}

} // namespace

// -----------------------------------------------------------------------------
// Impl — 편집 세션 추적 + 재귀 필드 드로잉. 헤더 비노출.
// -----------------------------------------------------------------------------
struct InspectorRenderer::Impl {
    bool debugMode = false;

    // 편집 세션 추적: ImGui 활성 아이템 ID + before blob.
    ImGuiID   editingId = 0;
    ValueBlob editingBefore;
    std::string editingPath;

    // 한 필드(리프/중첩/벡터)를 그린다. rootType/rootComp = 컴포넌트 루트(값 blob·커맨드 기준).
    void DrawField(EditorContext& ctx, const ObjectRef& target,
                   const refl::TypeInfo& rootType, void* rootComp,
                   const refl::PropertyPath& path, const refl::TypeInfo& type,
                   const refl::FieldInfo* field, void* ptr, const std::string& label) {
        const bool readOnly = !debugMode && HasAttr(field, refl::AttributeKind::ReadOnly);
        if (readOnly) ImGui::BeginDisabled(true);

        bool changed = false;
        bool isLeaf = true;

        // 리프 후보는 위젯 호출 전에 현재(편집 이전) 값을 스냅샷한다 —
        //   단발 변경(Checkbox/Combo)·드래그 시작의 before 를 정확히 얻기 위함.
        const bool leafKind = (type.GetKind() == refl::Kind::Primitive ||
                               type.GetKind() == refl::Kind::Enum);
        ValueBlob beforeFrame;
        if ((leafKind || type.GetKind() == refl::Kind::Struct) && rootComp)
            beforeFrame = ReadLeafBlob(rootType, rootComp, path);

        // 커스텀 타입 드로어 우선.
        IPropertyDrawer* drawer =
            ctx.extensions ? ctx.extensions->FindPropertyDrawer(type) : nullptr;
        if (drawer) {
            PropertyDrawContext pc;
            pc.editor = &ctx; pc.target = target; pc.path = path;
            pc.type = &type; pc.field = field; pc.instance = ptr; pc.label = label;
            changed = drawer->OnGui(pc);
        } else {
            switch (type.GetKind()) {
            case refl::Kind::Primitive: changed = WidgetPrimitive(type, field, ptr, label); break;
            case refl::Kind::Enum:      changed = WidgetEnum(type, ptr, label); break;
            case refl::Kind::AssetRef:  changed = WidgetAssetRef(ptr, label); break;
            case refl::Kind::Struct: {
                bool handled = false;
                changed = WidgetSpecialStruct(type, ptr, label, handled);
                if (!handled) {
                    isLeaf = false;
                    if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
                        for (const auto& f : type.Fields()) {
                            if (!debugMode && HasAttr(&f, refl::AttributeKind::HideInInspector))
                                continue;
                            refl::PropertyPath p = path;
                            p.segments.push_back(refl::PathSegment{std::string(f.Name())});
                            DrawField(ctx, target, rootType, rootComp, p, f.Type(), &f,
                                      f.GetPtr(ptr), std::string(f.Name()));
                        }
                        ImGui::TreePop();
                    }
                }
                break;
            }
            case refl::Kind::Vector: {
                isLeaf = false;
                const refl::TypeInfo* elem = type.ElementType();
                const std::size_t n = elem ? type.VectorSize(ptr) : 0;
                std::string hdr = label + " [" + std::to_string(n) + "]";
                if (elem && ImGui::TreeNodeEx(hdr.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
                    for (std::size_t i = 0; i < n; ++i) {
                        refl::PropertyPath ip = path;
                        ip.segments.push_back(refl::PathSegment{i});
                        DrawField(ctx, target, rootType, rootComp, ip, *elem, nullptr,
                                  type.VectorElementAt(ptr, i), "[" + std::to_string(i) + "]");
                    }
                    ImGui::TreePop();
                }
                break;
            }
            }
        }

        // 툴팁.
        if (const refl::Attribute* tip = FindAttr(field, refl::AttributeKind::Tooltip)) {
            if (!tip->text.empty() && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tip->text.c_str());
        }

        if (readOnly) { ImGui::EndDisabled(); return; }

        // 리프 값 편집 커밋(before/after blob). beforeFrame = 위젯 호출 직전 값.
        if (isLeaf)
            TrackAndCommit(ctx, target, rootType, rootComp, path, label, changed, beforeFrame);
    }

    // ImGui 활성 상태로 편집 세션을 추적하고, 세션 종료 시 PropertyEditCommand를 발행한다.
    //   beforeFrame = 이번 프레임 위젯 호출 직전(편집 이전) 값 blob.
    void TrackAndCommit(EditorContext& ctx, const ObjectRef& target,
                        const refl::TypeInfo& rootType, void* rootComp,
                        const refl::PropertyPath& path, const std::string& label,
                        bool changedNow, const ValueBlob& beforeFrame) {
        if (!rootComp || !ctx.commands) return;
        const ImGuiID itemId = ImGui::GetItemID();

        const bool activated = ImGui::IsItemActivated();
        const bool deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();

        // 드래그/키입력 세션 시작: 편집 이전 값을 before로 고정.
        if (activated) {
            editingId = itemId;
            editingPath = path.ToString();
            editingBefore = beforeFrame;
        }

        // 세션 종료(드래그/입력 확정).
        if (deactivatedAfterEdit && editingId == itemId) {
            ValueBlob after = ReadLeafBlob(rootType, rootComp, path);
            if (editingBefore.json != after.json) {
                ctx.commands->Push(std::make_unique<PropertyEditCommand>(
                    target, path, editingBefore, after, std::string("Edit ") + label));
            }
            editingId = 0; editingBefore = ValueBlob{}; editingPath.clear();
            return;
        }

        // 단발 변경(Checkbox/Combo 등 — 활성 추적 없이 이번 프레임에 확정).
        if (changedNow && !ImGui::IsItemActive() && editingId != itemId) {
            ValueBlob after = ReadLeafBlob(rootType, rootComp, path);
            if (beforeFrame.json != after.json) {
                ctx.commands->Push(std::make_unique<PropertyEditCommand>(
                    target, path, beforeFrame, after, std::string("Edit ") + label));
            }
        }
    }
};

InspectorRenderer::InspectorRenderer() : m_impl(std::make_unique<Impl>()) {}
InspectorRenderer::~InspectorRenderer() = default;

void InspectorRenderer::DrawEntity(EditorContext& ctx, ecs::Entity entity) {
    m_impl->debugMode = m_debugMode;
    ecs::World* world = ctx.activeWorld();
    if (!world || !world->Valid(entity)) {
        ImGui::TextUnformatted("(선택 없음)");
        return;
    }

    for (const refl::TypeInfo* t : refl::TypeRegistry::Get().All()) {
        if (!t || t->GetKind() != refl::Kind::Struct) continue;
        const ecs::ComponentTypeId cid = ComponentIdOf(*t);
        if (!world->IsRegistered(cid)) continue;
        void* comp = world->TryGetDynamic(entity, cid);
        if (!comp) continue;

        const std::string header(t->Name());
        if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushID(header.c_str());
            IComponentEditor* editor =
                ctx.extensions ? ctx.extensions->FindComponentEditor(*t) : nullptr;
            if (editor) {
                ComponentEditContext cec;
                cec.editor = &ctx; cec.entity = entity;
                cec.componentType = t; cec.component = comp;
                editor->OnGui(cec);
            } else {
                DrawReflected(ctx, ObjectRef::Component(entity, *t), *t, comp,
                              refl::PropertyPath{});
            }
            ImGui::PopID();
        }
    }
}

void InspectorRenderer::DrawReflected(EditorContext& ctx, const ObjectRef& target,
                                      const refl::TypeInfo& type, void* instance,
                                      const refl::PropertyPath& basePath) {
    if (type.GetKind() != refl::Kind::Struct || !instance) return;
    m_impl->debugMode = m_debugMode;

    // 값 blob·커맨드 기준 = 컴포넌트 루트. target.componentType가 정본, 없으면 type 자체.
    const refl::TypeInfo& rootType = target.componentType ? *target.componentType : type;
    void* rootComp = instance;   // basePath가 빈 최초 호출에서 instance == 컴포넌트 루트.

    for (const auto& f : type.Fields()) {
        if (!m_debugMode && HasAttr(&f, refl::AttributeKind::HideInInspector)) continue;
        refl::PropertyPath p = basePath;
        p.segments.push_back(refl::PathSegment{std::string(f.Name())});
        m_impl->DrawField(ctx, target, rootType, rootComp, p, f.Type(), &f,
                          f.GetPtr(instance), std::string(f.Name()));
    }
}

} // namespace mye::editor
