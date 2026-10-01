#include "mye/imgui/EditorWidgets.h"
#include "imgui.h"
#include <string>

namespace mye::imgui {
bool EditorButton(EditorIcon icon, const char* label) {
    const std::string caption = "    " + std::string(label);
    const bool pressed = ImGui::Button(caption.c_str());
    const auto low = ImGui::GetItemRectMin(), high = ImGui::GetItemRectMax();
    const float size = ImGui::GetFontSize() * .7f;
    const ImVec2 origin(low.x + ImGui::GetStyle().FramePadding.x, (low.y + high.y - size) * .5f);
    const auto color = ImGui::GetColorU32(ImGuiCol_Text);
    auto& draw = *ImGui::GetWindowDrawList();
    const auto point = [&](float x, float y) { return ImVec2(origin.x + x * size, origin.y + y * size); };
    const auto line = [&](float x, float y, float a, float b) { draw.AddLine(point(x,y),point(a,b),color,1.5f); };
    switch (icon) {
    case EditorIcon::NewFile:
        draw.AddRect(point(.1f,0),point(.85f,1),color);
        line(.25f,.5f,.7f,.5f); line(.475f,.275f,.475f,.725f); break;
    case EditorIcon::Open:
        line(0,.3f,0,1); line(0,1,1,1); line(1,1,1,.25f); line(1,.25f,.5f,.25f);
        line(.5f,.25f,.4f,.05f); line(.4f,.05f,0,.05f); line(0,.05f,0,.3f); break;
    case EditorIcon::Save:
        draw.AddRect(point(0,0),point(1,1),color);
        draw.AddRect(point(.2f,0),point(.7f,.35f),color);
        draw.AddRect(point(.2f,.6f),point(.8f,1),color); break;
    case EditorIcon::Play:
        draw.AddTriangleFilled(point(.15f,0),point(.95f,.5f),point(.15f,1),color); break;
    case EditorIcon::Stop:
        draw.AddRectFilled(point(.1f,.1f),point(.9f,.9f),color); break;
    case EditorIcon::Pause:
        draw.AddRectFilled(point(.1f,0),point(.35f,1),color);
        draw.AddRectFilled(point(.65f,0),point(.9f,1),color); break;
    case EditorIcon::Step:
        draw.AddTriangleFilled(point(0,0),point(.7f,.5f),point(0,1),color);
        line(.9f,0,.9f,1); break;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",label);
    return pressed;
}
}
