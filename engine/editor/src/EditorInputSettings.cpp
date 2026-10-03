#include "mye/editor/EditorApp.h"
#include "mye/runtime/GameInput.h"
#include "imgui.h"
#include <algorithm>
#include <array>
#include <cfloat>

namespace mye::editor {
namespace {
void DrawBinding(InputBinding& binding) {
    static constexpr std::array<const char*, 5> devices{"키보드", "마우스 버튼", "패드 버튼", "패드 축", "마우스 휠"};
    const auto device = static_cast<int>(binding.device);
    if (!ImGui::BeginTable("Binding", 2, ImGuiTableFlags_SizingStretchSame)) return;
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("장치");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##Device", devices[device])) {
        for (int i = 0; i < static_cast<int>(devices.size()); ++i) if (ImGui::Selectable(devices[i], i == device)) {
            binding = {static_cast<InputDevice>(i), i == 0 ? static_cast<int>(KeyCode::Space) : 0};
        }
        ImGui::EndCombo();
    }
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("입력");
    ImGui::SetNextItemWidth(-FLT_MIN);
    const auto codeName = InputCodeName(binding.device, binding.code);
    if (ImGui::BeginCombo("##Code", codeName.data())) {
        for (int code = 0; code < static_cast<int>(KeyCode::Count); ++code) {
            const auto name = InputCodeName(binding.device, code);
            if (!name.empty() && ImGui::Selectable(name.data(), binding.code == code)) binding.code = code;
        }
        ImGui::EndCombo();
    }
    const bool gamepad = binding.device == InputDevice::GamepadButton || binding.device == InputDevice::GamepadAxis;
    if (gamepad) {
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        ImGui::TextUnformatted("패드 번호"); ImGui::SetNextItemWidth(-FLT_MIN);
        int pad = binding.pad + 1;
        if (ImGui::SliderInt("##Pad", &pad, 1, kMaxGamepads)) binding.pad = pad - 1;
    }
    if (binding.device == InputDevice::GamepadAxis || binding.device == InputDevice::Wheel) {
        if (!gamepad) ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextUnformatted("방향"); ImGui::SetNextItemWidth(-FLT_MIN);
        int direction = binding.direction > 0 ? 1 : 0;
        if (ImGui::Combo("##Direction", &direction, "음수 (-)\0양수 (+)\0")) binding.direction = direction ? 1 : -1;
    }
    ImGui::EndTable();
}
}
Expected<void, Error> EditorApp::RequestInputSettings() {
    if (!m_project || !m_project->IsOpen()) return Error{"입력 설정을 변경할 프로젝트를 먼저 여세요.", 1};
    if (m_playMode->IsPlaying()) return Error{"실행을 중지한 뒤 입력 설정을 변경하세요.", 1};
    m_inputDraft = m_project->InputSettings();
    m_inputSettingsViewportSize = {};
    m_inputActionName.fill(0);
    m_inputActionError.clear();
    m_showInputSettings = true;
    return {};
}
void EditorApp::DrawInputSettings() {
    if (!m_showInputSettings || !m_project->IsOpen()) return;
    const auto* viewport = ImGui::GetMainViewport();
    const Vec2 workSize{viewport->WorkSize.x, viewport->WorkSize.y};
    const bool viewportResized = workSize != m_inputSettingsViewportSize;
    m_inputSettingsViewportSize = workSize;
    const ImVec2 maxSize{viewport->WorkSize.x * .9f, viewport->WorkSize.y * .85f};
    ImGui::SetNextWindowSizeConstraints({std::min(maxSize.x, ImGui::GetFontSize() * 18),
                                         std::min(maxSize.y, ImGui::GetFontSize() * 24)}, maxSize);
    ImGui::SetNextWindowPos({viewport->WorkPos.x + viewport->WorkSize.x * .5f,
                            viewport->WorkPos.y + viewport->WorkSize.y * .5f},
                            viewportResized ? ImGuiCond_Always : ImGuiCond_Appearing, {.5f, .5f});
    ImGui::SetNextWindowSize({std::min(viewport->WorkSize.x * .9f, ImGui::GetFontSize() * 62),
                             std::min(viewport->WorkSize.y * .85f, ImGui::GetFontSize() * 36)}, ImGuiCond_Appearing);
    const bool visible = ImGui::Begin("입력 설정###MyEngineInputSettings", &m_showInputSettings, ImGuiWindowFlags_NoDocking);
    if (visible) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextWrapped("프로젝트: %s", m_project->Name().data());
        const bool playing = m_playMode->IsPlaying();
        if (playing) ImGui::TextWrapped("실행을 중지한 뒤 설정을 변경하세요.");
        ImGui::Separator();
        ImGui::BeginDisabled(playing);
        const float availableWidth = ImGui::GetContentRegionAvail().x;
        const float addWidth = ImGui::CalcTextSize("조작 추가").x + ImGui::GetStyle().FramePadding.x * 2;
        const bool stackedName = availableWidth < ImGui::GetFontSize() * 18 + addWidth + ImGui::GetStyle().ItemSpacing.x;
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(stackedName ? availableWidth : ImGui::GetFontSize() * 18);
        const bool addByEnter = ImGui::InputTextWithHint("##ActionName", "새 조작 이름 (예: attack)",
            m_inputActionName.data(), m_inputActionName.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::IsItemEdited()) m_inputActionError.clear();
        if (!stackedName) ImGui::SameLine();
        ImGui::BeginDisabled(m_inputDraft.actions.size() >= 64 || !m_inputActionName[0]);
        if ((ImGui::Button("조작 추가") || addByEnter) && m_inputActionName[0] && m_inputDraft.actions.size() < 64) {
            InputMap addition{{{m_inputActionName.data(), .2f, {}}}};
            auto valid = addition.Validate();
            const bool duplicate = std::any_of(m_inputDraft.actions.begin(), m_inputDraft.actions.end(),
                [&](const auto& action) { return action.name == addition.actions.front().name; });
            if (!valid) m_inputActionError = valid.GetError().message;
            else if (duplicate) m_inputActionError = "같은 이름의 조작이 이미 있습니다.";
            else { m_inputDraft.actions.push_back(std::move(addition.actions.front())); m_inputActionName.fill(0); m_inputActionError.clear(); }
        }
        ImGui::EndDisabled();
        const auto valid = m_inputDraft.Validate();
        const char* status = valid ? "저장 전에는 적용되지 않습니다. 취소하면 편집을 버립니다." : valid.GetError().message.c_str();
        const auto& style = ImGui::GetStyle();
        const float buttonWidth = ImGui::CalcTextSize("프로젝트에 저장").x + ImGui::CalcTextSize("기본 조작 복원").x +
                                  ImGui::CalcTextSize("취소").x + style.FramePadding.x * 6 + style.ItemSpacing.x * 2;
        const bool stackedFooter = availableWidth < buttonWidth;
        const float footerHeight = ImGui::CalcTextSize(status, nullptr, false, availableWidth).y + style.ItemSpacing.y +
                                   ImGui::GetFrameHeightWithSpacing() * (stackedFooter ? 3 : 1);
        if (ImGui::BeginChild("Actions", {0, std::max(ImGui::GetFrameHeight() * 2, ImGui::GetContentRegionAvail().y - footerHeight)})) {
            ImGui::PushTextWrapPos(0);
            if (ImGui::TreeNode("사용법")) {
                ImGui::TextDisabled("이름: 영문·숫자·밑줄 1~64자, 숫자 시작 제외. 최대 64개.");
                ImGui::TextWrapped("조작을 펼쳐 입력을 연결합니다. Lua에서 같은 이름으로 조회합니다.");
                ImGui::TextWrapped("물리 키 위치 · 패드 1~4번 · 회전/점프 3D · 확대/축소 2D");
                ImGui::TreePop();
            }
            if (!m_inputActionError.empty()) ImGui::TextWrapped("%s", m_inputActionError.c_str());
            ImGui::Separator();
            if (m_inputDraft.actions.empty()) ImGui::TextWrapped("저장된 조작이 없습니다. 아래의 기본 조작 복원으로 시작하세요.");
            for (size_t actionIndex = 0; actionIndex < m_inputDraft.actions.size();) {
                auto& action = m_inputDraft.actions[actionIndex];
                bool removeAction = false;
                ImGui::PushID(action.name.c_str());
                const auto description = runtime::GameActionDescription(action.name);
                const bool expanded = ImGui::TreeNodeEx(action.name.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s · %s", action.name.c_str(), description.data());
                if (expanded) {
                    ImGui::TextWrapped("%s", description.data());
                    ImGui::SetNextItemWidth(std::min(ImGui::GetFontSize() * 10, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("데드존").x - style.ItemInnerSpacing.x));
                    ImGui::SliderFloat("데드존", &action.deadzone, 0, .95f, "%.2f");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("이 값 이하의 아날로그 입력은 무시합니다. 이동은 네 방향의 평균값을 원형으로 적용합니다.");
                    if (action.bindings.empty()) ImGui::TextDisabled("입력 없음 — 이 조작은 작동하지 않습니다.");
                    for (size_t i = 0; i < action.bindings.size();) {
                        ImGui::PushID(static_cast<int>(i));
                        DrawBinding(action.bindings[i]);
                        if (ImGui::Button("제거")) action.bindings.erase(action.bindings.begin() + i);
                        else ++i;
                        ImGui::PopID();
                        ImGui::Spacing();
                    }
                    ImGui::BeginDisabled(action.bindings.size() >= 16);
                    if (ImGui::Button("입력 추가")) action.bindings.push_back({});
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    removeAction = ImGui::Button("조작 제거");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("이 조작의 모든 바인딩을 제거합니다. 저장 전 취소로 되돌릴 수 있습니다.");
                    ImGui::TreePop();
                }
                ImGui::PopID();
                if (removeAction) m_inputDraft.actions.erase(m_inputDraft.actions.begin() + actionIndex);
                else ++actionIndex;
                ImGui::Spacing();
            }
            ImGui::PopTextWrapPos();
        }
        ImGui::EndChild();
        if (!valid) ImGui::TextWrapped("%s", status);
        else ImGui::TextDisabled("%s", status);
        ImGui::BeginDisabled(!valid);
        if (ImGui::Button("프로젝트에 저장")) {
            const auto saved = m_project->SaveInputSettings(m_inputDraft);
            ReportFileResult(saved, "입력 설정을 저장했습니다.");
            if (saved) m_showInputSettings = false;
        }
        ImGui::EndDisabled();
        if (!stackedFooter) ImGui::SameLine();
        if (ImGui::Button("기본 조작 복원")) m_inputDraft = runtime::DefaultGameInputMap();
        ImGui::EndDisabled();
        if (!stackedFooter) ImGui::SameLine();
        if (ImGui::Button("취소") || ImGui::Shortcut(ImGuiKey_Escape)) m_showInputSettings = false;
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
}
} // namespace mye::editor
