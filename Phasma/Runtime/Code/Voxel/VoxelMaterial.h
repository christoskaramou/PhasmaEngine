#pragma once

#include "API/Image.h"

namespace pe
{
    class Scene;
}

namespace pe::voxel
{
    // Builds the voxel tile atlas (array texture) and registers it with the Scene. The atlas SRV is
    // bound directly via Scene::SetVoxelAtlasView / the GBuffer voxel descriptor — there is no
    // Material object; voxel sections render through VoxelWorld's host material.
    class VoxelMaterial
    {
    public:
        void Build(Scene *scene, const std::vector<std::string> &tilePngPaths);
        ResourceHandle<Image> Atlas() const;

    private:
        ResourceHandle<Image> m_atlas;
    };
} // namespace pe::voxel
