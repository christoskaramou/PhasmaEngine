#pragma once

#include "Scene/Model.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <unordered_map>
#include <initializer_list>

namespace pe
{
    class CommandBuffer;

    class ModelAssimp : public Model
    {
    public:
        static ModelAssimp *Load(const std::filesystem::path &file);

        ModelAssimp();
        ~ModelAssimp() override = default;

        bool LoadFile(const std::filesystem::path &file) override;
        void UploadImages(CommandBuffer *cmd) override;
        void CreateSamplers() override;
        void ExtractMaterialInfo() override;
        void ProcessAabbs() override;
        void AcquireGeometryInfo() override;
        void SetupNodes() override;
        void UpdateAllNodeMatrices() override;
        int GetMeshCount() const override;
        int GetNodeCount() const override;
        int GetNodeMesh(int nodeIndex) const override;
        int GetMeshPrimitiveCount(int meshIndex) const override;
        int GetPrimitiveMaterial(int meshIndex, int primitiveIndex) const override;
        void GetPrimitiveMaterialFactors(int meshIndex, int primitiveIndex, mat4 &factors) const override;
        float GetPrimitiveAlphaCutoff(int meshIndex, int primitiveIndex) const override;

    private:
        void UpdateNodeMatrix(int node) override;
        void ProcessNode(const aiNode *node, int parentIndex);
        void ProcessMesh(const aiMesh *mesh, int meshIndex);
        void ProcessMaterial(const aiMaterial *material, int materialIndex);
        std::filesystem::path GetTexturePath(const aiMaterial *material, aiTextureType type, int index = 0) const;
        std::filesystem::path ResolveTexturePath(const std::filesystem::path &relativePath) const;
        Image *LoadTexture(CommandBuffer *cmd, const std::filesystem::path &texturePath);
        Image *FindTexture(const std::filesystem::path &texturePath) const;
        void AssignTexture(PrimitiveInfo &primitiveInfo, TextureType type, aiMaterial *material, std::initializer_list<aiTextureType> textureTypes, Image *defaultImage);
        RenderType DetermineRenderType(aiMaterial *material) const;

        Assimp::Importer m_importer;
        const aiScene *m_scene = nullptr;
        std::filesystem::path m_filePath;
        std::vector<int> m_nodeToMesh;         // Maps node index to mesh index (-1 if no mesh)
        std::vector<int> m_meshToMaterial;     // Maps mesh index to material index
        std::vector<aiMaterial *> m_materials; // Cached material pointers
        std::unordered_map<std::string, Image *> m_textureCache;
    };
}
