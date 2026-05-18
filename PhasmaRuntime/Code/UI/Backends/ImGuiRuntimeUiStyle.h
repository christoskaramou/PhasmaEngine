#pragma once

#include "imgui.h"

namespace pe::runtime_ui_imgui
{
    inline constexpr float kWindowWidth = 300.0f;
    inline constexpr float kViewportPadding = 12.0f;
    inline constexpr float kScreenGap = 8.0f;

    inline constexpr ImGuiWindowFlags kPlayerWindowFlags = ImGuiWindowFlags_AlwaysAutoResize;
    inline constexpr ImGuiWindowFlags kEditorWindowFlags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_AlwaysAutoResize;

    inline void ApplyContextSettings(ImGuiIO &io)
    {
        io.ConfigFlags |= ImGuiConfigFlags_IsSRGB;
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
    }

    inline ImGuiStyle BuildStyle(float scale = 1.0f)
    {
        ImGuiStyle style;
        ImGui::StyleColorsDark(&style);
        if (scale != 1.0f)
            style.ScaleAllSizes(scale);
        return style;
    }

    inline void ApplyStyle(float scale = 1.0f)
    {
        ImGui::GetStyle() = BuildStyle(scale);
    }
} // namespace pe::runtime_ui_imgui
