#pragma once

namespace pe
{
    // Draws the scene-owned settings (render scale, present mode, lighting, atmosphere, debug overlays,
    // simulation, navigation) under collapsible sections. The render path, passes and post-process
    // defaults are the pipeline's (PipelineControls.h). Returns true if any serializable value changed.
    bool DrawSceneSettingsControls();
} // namespace pe
