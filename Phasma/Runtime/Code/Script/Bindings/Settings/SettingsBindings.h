#pragma once

#include "API/RHITypes.h"

namespace pe
{
    // Lua settings.get / settings.set on the bool and number keys, a bool as 0 or 1 (the native script view);
    // false for any other key or a non-finite value. render_scale is clamped like rhi.set_render_scale.
    // ui_reference_width / ui_reference_height reach RuntimeUiSystem::SetReferenceSurface (0 = off).
    [[nodiscard]] bool GetSceneSettingNumber(std::string_view name, double &value);
    bool SetSceneSettingNumber(std::string_view name, double value);
    // rhi.change_present_mode: a surface without the mode falls back; returns the mode in effect (none without
    // a surface).
    std::optional<PePresentMode> RequestPresentModeChange(PePresentMode mode);
    // engine.get_window_mode / set_window_mode tokens: "windowed", "borderless", "fullscreen". Setting is false in
    // the editor (the window is the tool), for an unknown token or without a window, and a no-op on Android.
    [[nodiscard]] const char *GetWindowModeToken();
    bool SetWindowModeToken(std::string_view mode);
    // engine.get_window_size: the window in pixels, 0 x 0 without one.
    void GetWindowSize(int &width, int &height);
} // namespace pe
