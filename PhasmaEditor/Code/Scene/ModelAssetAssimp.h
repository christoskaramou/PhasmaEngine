#pragma once

#include "Scene/ModelAsset.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace pe
{
    class CommandBuffer;
    class Material;

    class ModelAssetAssimp : public ModelAsset
    {
    public:
        static ModelAsset *Load(const std::filesystem::path &file);

        // Two-phase load for async: CPU work on any thread, GPU upload on main thread
        static ModelAsset *LoadCpuOnly(const std::filesystem::path &file);
        void UploadGpu();

        ModelAssetAssimp();
        ~ModelAssetAssimp() override = default;

    private:
        bool LoadFile(const std::filesystem::path &file);

        // high-level build steps
        void UploadImages(CommandBuffer *cmd);
        void BuildMeshes(); // materials + geometry + aabbs (single coherent build)
        void SetupNodes();

        // helpers
        void ProcessNode(const aiNode *node, int parentIndex);
        std::filesystem::path GetTexturePath(const aiMaterial *material, aiTextureType type, int index = 0) const;

        void AssignTexture(Material &mat, TextureType type, aiMaterial *material,
                           std::initializer_list<aiTextureType> textureTypes);

        RenderType DetermineRenderType(aiMaterial *material) const;
        void ComputeMaterialData(Material &mat, aiMaterial *material) const;

        Assimp::Importer m_importer;
        const aiScene *m_scene = nullptr;
    };
} // namespace pe
