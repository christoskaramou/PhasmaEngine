#pragma once

#include "API/Image.h"
#include "Scene/Material.h"

namespace pe
{
    class Scene;
}

namespace pe::voxel
{
    class VoxelMaterial
    {
    public:
        void Build(Scene *scene, const std::vector<std::string> &tilePngPaths);
        Material *Get() const;
        ResourceHandle<Image> Atlas() const;

    private:
        std::unique_ptr<Material> m_material;
        ResourceHandle<Image> m_atlas;
    };
} // namespace pe::voxel
