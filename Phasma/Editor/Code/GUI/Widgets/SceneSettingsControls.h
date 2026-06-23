#pragma once

namespace pe
{
    // Draws the non-post-process scene/device settings (render scale, present mode, render mode,
    // lighting toggles, shadows, culling, debug, time). Pairs with DrawPostProcessControls for the
    // full Scene Settings inspector. Returns true if any serializable value changed.
    bool DrawSceneSettingsControls();
} // namespace pe
