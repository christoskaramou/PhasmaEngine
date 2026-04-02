#pragma once

#include "Scene/SceneNode.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace pe
{
    class CommandBuffer;
    class Material;
    class Scene;

    // Loads a 3D model file via Assimp and pushes data directly into Scene's stores.
    // Returns the root NodeId, or nullptr on failure.
    class AssimpLoader
    {
    public:
        static NodeId *Load(Scene &scene, const std::filesystem::path &file);

    private:
        AssimpLoader(Scene &scene);

        bool LoadFile(const std::filesystem::path &file);
        void UploadImages(CommandBuffer *cmd);
        void BuildMeshes();
        void SetupNodes();
        void ExtractAnimations();
        void ProcessNode(const aiNode *node, NodeId *parent);

        std::filesystem::path GetTexturePath(const aiMaterial *material, aiTextureType type, int index = 0) const;
        void AssignTexture(Material &mat, TextureType type, aiMaterial *material,
                           std::initializer_list<aiTextureType> textureTypes);
        RenderType DetermineRenderType(aiMaterial *material) const;
        void ComputeMaterialData(Material &mat, aiMaterial *material) const;

        Scene &m_scene;
        Assimp::Importer m_importer;
        const aiScene *m_aiScene = nullptr;
        std::filesystem::path m_filePath;
        NodeId *m_rootNode = nullptr;

        // Temporary map: assimp mesh index -> Scene mesh index
        std::vector<int> m_assimpToSceneMesh;
    };
} // namespace pe
