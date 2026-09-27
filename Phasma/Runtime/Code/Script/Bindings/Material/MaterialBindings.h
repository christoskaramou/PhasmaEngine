#pragma once

#include <string_view>

namespace pe
{
    class Scene;
    struct NodeId;

    // material.set_render_type: "opaque", "alpha_cut", "alpha_blend", "transmission", "lines", "sprite_outline"
    // (and their aliases) on one mesh of the node. False for an unknown name, a dead node or no mesh.
    bool SetNodeRenderTypeByName(Scene *s, NodeId *nodeId, int meshIdx, std::string_view type);
} // namespace pe
