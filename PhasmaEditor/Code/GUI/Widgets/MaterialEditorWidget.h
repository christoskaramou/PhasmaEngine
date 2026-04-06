#pragma once
#include "Scene/Material.h"
#include "Scene/MaterialReflection.h"

namespace pe
{
    class Material;
    class MaterialInstance;
    class PassInfoAsset;
    class Scene;
    struct Mesh;
    struct NodeId;

    class MaterialEditorWidget
    {
    public:
        // Draw the full material editor for a mesh's material.
        // Returns true if any value was modified.
        bool Draw(Mesh *mesh, NodeId *node, Scene &scene);

    private:
        bool DrawPassInfoSelector(Material &mat);
        bool DrawReflectedParams(Material &mat, const MaterialLayout &layout);
        bool DrawReflectedTextures(Material &mat, const MaterialLayout &layout);
        bool DrawInstanceParams(MaterialInstance &inst, const MaterialLayout &layout);
        bool DrawInstanceTextures(MaterialInstance &inst, const MaterialLayout &layout);
        bool DrawField(const MaterialFieldDesc &field, MaterialParamValue &value);

        // Cached layout — invalidated when PassInfoAsset changes
        MaterialLayout m_cachedLayout;
        std::string m_cachedPassInfoId;
    };
} // namespace pe
