#pragma once

#include "Scene/Model.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

namespace pe
{
    class CommandBuffer;

    class ModelAssimp : public Model
    {
    public:
        static ModelAssimp *Load(const std::filesystem::path &file);

        ModelAssimp();
        ~ModelAssimp() override = default;

        int GetNodeCount() const override;
        int GetNodeMesh(int nodeIndex) const override;

    private:
        bool LoadFile(const std::filesystem::path &file);
        void UploadImages(CommandBuffer *cmd);
        void ExtractMaterialInfo();
        void ProcessAabbs();
        void AcquireGeometryInfo();
        void SetupNodes();
        void UpdateAllNodeMatrices();
        void UpdateNodeMatrix(int node) override;
        void ProcessNode(const aiNode *node, int parentIndex);
        std::filesystem::path GetTexturePath(const aiMaterial *material, aiTextureType type, int index = 0) const;
        std::filesystem::path ResolveTexturePath(const std::filesystem::path &relativePath) const;
        Image *LoadTexture(CommandBuffer *cmd, const std::filesystem::path &texturePath);
        Image *FindTexture(const std::filesystem::path &texturePath) const;
        void AssignTexture(MeshInfo &meshInfo, TextureType type, aiMaterial *material, std::initializer_list<aiTextureType> textureTypes, Image *defaultImage);
        RenderType DetermineRenderType(aiMaterial *material) const;
        void ComputeMaterialData(MeshInfo &meshInfo, aiMaterial *material) const;

        Assimp::Importer m_importer;
        const aiScene *m_scene = nullptr;
        std::filesystem::path m_filePath;
        std::vector<int> m_nodeToMesh; // Maps node index to mesh index (-1 if no mesh)
        std::unordered_map<std::string, Image *> m_textureCache;
    };
}
