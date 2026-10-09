#pragma once

#include "Script/ScriptSystem.h"

namespace pe
{
    // Script values as fields with readable labels (cas_sharpness -> Cas Sharpness), each nested under the bool value
    // its exposed_when names and shown only while that is on. get / set reach a script's table or the pipeline's
    // values. Returns true if a value changed.
    bool DrawExposedValues(const std::vector<ExposedVar> &vars, lua_State *L,
                           const std::function<sol::object(const ExposedVar &)> &get,
                           const std::function<void(const ExposedVar &, const sol::object &)> &set,
                           const char *tooltip = nullptr);

    // Render mode, HDR scene colour and dynamic rendering. Returns true if a value changed.
    bool DrawPipelineRenderPathControls();

    // The scene's pipeline (Scene Settings > Pipeline): the render path, the pipeline script (a .lua under
    // Scripts/Pipeline, or the engine default) and every value it exposes. Editor only; returns true if a value changed.
    bool DrawPipelineControls();
} // namespace pe
