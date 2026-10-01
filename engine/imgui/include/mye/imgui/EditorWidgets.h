#pragma once

namespace mye::imgui {
enum class EditorIcon { NewFile, Open, Save, Play, Stop, Pause, Step };
// Native ImGui button/navigation, with a vector icon and a visible text label.
bool EditorButton(EditorIcon icon, const char* label);
bool EditorTextButton(const char* label, bool selected = false);
bool EditorIconButton(EditorIcon icon, const char* id, const char* tooltip, bool selected = false);
}
