#pragma once

#include "Scene/Model.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace pe
{
    class CommandBuffer;

    class ModelAssimp : public Model
    {
    public:
        static Model *Load(const std::filesystem::path &file);

        ModelAssimp();
        ~ModelAssimp() override = default;

    private:
        bool LoadFile(const std::filesystem::path &file);

        // high-level build steps
        void UploadImages(CommandBuffer *cmd);
        void BuildMeshes(); // materials + geometry + aabbs (single coherent build)
        void SetupNodes();

        // helpers
        void ProcessNode(const aiNode *node, int parentIndex);
        std::filesystem::path GetTexturePath(const aiMaterial *material, aiTextureType type, int index = 0) const;

        void AssignTexture(MeshInfo &meshInfo, TextureType type, aiMaterial *material,
                           std::initializer_list<aiTextureType> textureTypes);

        RenderType DetermineRenderType(aiMaterial *material) const;
        void ComputeMaterialData(MeshInfo &meshInfo, aiMaterial *material) const;

        Assimp::Importer m_importer;
        const aiScene *m_scene = nullptr;
    };
} // namespace pe
