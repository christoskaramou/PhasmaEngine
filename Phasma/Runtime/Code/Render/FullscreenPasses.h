#pragma once

#include <cstdint>
#include <string>

namespace pe
{
    // A fullscreen shader pass over the scene's depth and normals, blended onto the viewport at its render-graph
    // order (phasma::FullscreenPass's contract): the shader provides mainVS / mainPS and reads the depth at binding 0
    // and the normals at binding 1; push constants vec4(1/width, 1/height, max(minThickness, thicknessAt1080 *
    // height / 1080), camera near) and vec4 params. Added by C++ scripts, Lua (render_graph.add_fullscreen_pass) and
    // the scene's pipeline script; each owner's passes are cleared together.
    struct FullscreenPassDesc
    {
        std::string shader; // asset path
        uint32_t order = 550;
        float thicknessAt1080 = 1.0f, minThickness = 0.0f;
        float params[4] = {};
    };

    // Re-adding a name replaces it (rebuilt on next use when its shader changed or failed).
    void AddFullscreenPass(const std::string &name, const FullscreenPassDesc &desc, const void *owner);
    bool RemoveFullscreenPass(const std::string &name);
    void ClearFullscreenPasses(const void *owner);
} // namespace pe
