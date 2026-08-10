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

    // Pointer feedback is a MULTIPLY on whatever the widget already draws — its
    // plate art, its fill, its image. Not a translucent rectangle painted over
    // the top, which is what it used to be: that added the widget's accent as a
    // visible colour, so a gold-accent tab flashed yellow and a grey-accent
    // button flashed grey, and no two buttons agreed. Scaling the drawn pixels
    // keeps every button's own art and makes the reaction identical everywhere.
    // Both DARKEN. Brightening cannot show on the common case: a plate tinted
    // white already draws its texture at full value, so a >1 multiply clamps and
    // nothing happens. Dimming reads on every plate, pale or saturated, and
    // "presses in" the way a physical button does.
    inline constexpr float kHoverTint = 0.92f; // hover: a touch darker
    inline constexpr float kPressTint = 0.80f; // press: pushed in

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
