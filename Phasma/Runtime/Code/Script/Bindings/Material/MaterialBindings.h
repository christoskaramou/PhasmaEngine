#pragma once

#include <string_view>

namespace pe
{
    class Scene;
    struct NodeId;

    // material.set_render_type: "opaque", "alpha_cut", "alpha_blend", "transmission", "lines", "sprite_outline"
    // (and their aliases) on one mesh of the node. False for an unknown name, a dead node or no mesh.
    bool SetNodeRenderTypeByName(Scene *s, NodeId *nodeId, int meshIdx, std::string_view type);
    // material.set_texture(node, type, path): "base_color", "emissive", "normal", ... from an asset path.
    bool SetNodeTextureByName(Scene *s, NodeId *nodeId, int meshIdx, std::string_view type, const std::string &path);
    // material.set_double_sided, which flips the mesh's shared material.
    bool SetNodeDoubleSidedFlag(Scene *s, NodeId *nodeId, int meshIdx, bool doubleSided);
    // material.set(node, "alpha_cutoff", value) on the mesh's material instance.
    bool SetNodeAlphaCutoff(Scene *s, NodeId *nodeId, int meshIdx, float cutoff);
} // namespace pe
