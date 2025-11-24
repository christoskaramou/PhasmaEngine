#pragma once

#include "Scene/Model.h"
#include "tinygltf/tiny_gltf.h"

namespace pe
{
    class CommandBuffer;

    class ModelGltf : public Model
    {
    public:
        static ModelGltf *Load(const std::filesystem::path &file);

        ModelGltf();
        ~ModelGltf() override = default;

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

        tinygltf::Model &GetGltfModel() { return m_gltfModel; }
        const tinygltf::Model &GetGltfModel() const { return m_gltfModel; }

    private:
        void UpdateNodeMatrix(int node) override;

        tinygltf::Model m_gltfModel;
    };
}
