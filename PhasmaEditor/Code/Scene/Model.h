#pragma once

#include "Script/Script.h"
#include "Systems/CameraSystem.h"
#include "API/Vertex.h"
#include "API/Pipeline.h"

namespace pe
{
    class Buffer;
    struct Vertex;
    struct AabbVertex;
    class Sampler;
    class CommandBuffer;
    class Image;

    struct MaterialInfo
    {
        vk::PrimitiveTopology topology;
        vk::CullModeFlags cullMode;
        bool alphaBlend;
    };

    struct PrimitiveInfo
    {
        bool cull = true;
        size_t dataOffset = -1;
        const size_t dataSize = sizeof(mat4);         // material factors
        uint32_t vertexOffset = 0, verticesCount = 0; // offset and count in used vertex buffer
        uint32_t indexOffset = 0, indicesCount = 0;   // offset and count in used index buffer
        uint32_t indirectIndex = 0;                   // index of the indirect command of the primitive in the indirect buffer
        uint32_t positionsOffset = 0;
        size_t aabbVertexOffset = 0;
        uint32_t aabbColor;
        vec4 boundingSphere;
        AABB boundingBox;
        AABB worldBoundingBox;
        RenderType renderType;
        Image *images[5]{nullptr};
        Sampler *samplers[5]{nullptr};
        uint32_t textureMask = 0;
        MaterialInfo materialInfo;

    private:
        friend class Geometry;

        // indices in the image views array in shader
        // 0 = Base Color
        // 1 = Normal
        // 2 = Metallic Roughness
        // 3 = Occlusion
        // 4 = Emissive
        uint32_t viewsIndex[5] = {0, 0, 0, 0, 0}; // Updated in Geometry::UploadBuffers
    };

    struct MeshInfo
    {
        size_t dataOffset = -1;
        const size_t dataSize = sizeof(mat4) * 2; // transform and previous transform
        std::vector<PrimitiveInfo> primitivesInfo{};
    };

    struct NodeInfo
    {
        int parent = -1;
        mat4 localMatrix;
        struct UBO
        {
            mat4 worldMatrix = mat4(1.f);
            mat4 previousWorldMatrix = mat4(1.f);
        } ubo;
        bool dirty = false;
        std::vector<bool> dirtyUniforms;
    };

    // Base class for all model loaders
    class Model
    {
    public:
        Model();
        virtual ~Model();

        // Factory method to load models based on file extension
        static Model *Load(const std::filesystem::path &file);

        // Common interface methods
        void UpdateNodeMatrices();
        void SetPrimitiveFactors(Buffer *uniformBuffer);
        void UploadBuffers(CommandBuffer *cmd);

        // Getters
        size_t GetId() const { return m_id; }
        bool IsRenderReady() const { return m_render; }
        const std::vector<Vertex> &GetVertices() const { return m_vertices; }
        const std::vector<PositionUvVertex> &GetPositionUvs() const { return m_positionUvs; }
        const std::vector<AabbVertex> &GetAabbVertices() const { return m_aabbVertices; }
        const std::vector<uint32_t> &GetIndices() const { return m_indices; }
        const std::vector<Image *> &GetImages() const { return m_images; }
        const std::vector<Sampler *> &GetSamplers() const { return m_samplers; }
        const std::vector<MeshInfo> &GetMeshesInfo() const { return m_meshesInfo; }
        const std::vector<NodeInfo> &GetNodesInfo() const { return m_nodesInfo; }
        std::vector<MeshInfo> &GetMeshesInfo() { return m_meshesInfo; }
        std::vector<NodeInfo> &GetNodesInfo() { return m_nodesInfo; }
        mat4 &GetMatrix() { return matrix; }
        bool &GetDirtyNodes() { return dirtyNodes; }
        std::vector<bool> &GetDirtyUniforms() { return dirtyUniforms; }
        uint32_t GetVerticesCount() const { return m_verticesCount; }
        uint32_t GetIndicesCount() const { return m_indicesCount; }
        uint32_t GetPrimitivesCount() const { return m_primitivesCount; }

        // Virtual methods to be implemented by derived classes
        virtual bool LoadFile(const std::filesystem::path &file) = 0;
        virtual void UploadImages(CommandBuffer *cmd) = 0;
        virtual void CreateSamplers() = 0;
        virtual void ExtractMaterialInfo() = 0;
        virtual void ProcessAabbs() = 0;
        virtual void AcquireGeometryInfo() = 0;
        virtual void SetupNodes() = 0;
        virtual void UpdateAllNodeMatrices() = 0;
        virtual int GetMeshCount() const = 0;
        virtual int GetNodeCount() const = 0;
        virtual int GetNodeMesh(int nodeIndex) const = 0;
        virtual int GetMeshPrimitiveCount(int meshIndex) const = 0;
        virtual int GetPrimitiveMaterial(int meshIndex, int primitiveIndex) const = 0;
        virtual void GetPrimitiveMaterialFactors(int meshIndex, int primitiveIndex, mat4 &factors) const = 0;
        virtual float GetPrimitiveAlphaCutoff(int meshIndex, int primitiveIndex) const = 0;

    protected:
        friend class Geometry;
        friend class DepthPass;
        friend class GbufferOpaquePass;
        friend class GbufferTransparentPass;
        friend class ShadowPass;
        friend class AabbsPass;

        virtual void UpdateNodeMatrix(int node);
        const PrimitiveInfo *GetPrimitiveInfo(int meshIndex, int primitiveIndex) const;
        static constexpr uint32_t TextureBit(TextureType type)
        {
            return 1u << static_cast<uint32_t>(type);
        }

        // Helper methods for vertex processing (inline for performance)
        static inline void FillVertexPosition(Vertex &vertex, float x, float y, float z);
        static inline void FillVertexPosition(PositionUvVertex &vertex, float x, float y, float z);
        static inline void FillVertexUV(Vertex &vertex, float u, float v);
        static inline void FillVertexUV(PositionUvVertex &vertex, float u, float v);
        static inline void FillVertexNormal(Vertex &vertex, float x, float y, float z);
        static inline void FillVertexColor(Vertex &vertex, float r, float g, float b, float a);
        static inline void FillVertexJointsWeights(Vertex &vertex, uint8_t j0, uint8_t j1, uint8_t j2, uint8_t j3,
                                                   float w0, float w1, float w2, float w3);
        static inline void FillVertexJointsWeights(PositionUvVertex &vertex, uint8_t j0, uint8_t j1, uint8_t j2, uint8_t j3,
                                                   float w0, float w1, float w2, float w3);

        std::atomic_bool m_render = false;
        size_t m_id;
        std::vector<Image *> m_images{};
        std::vector<Sampler *> m_samplers{};
        std::vector<MeshInfo> m_meshesInfo{};
        std::vector<NodeInfo> m_nodesInfo{};
        std::vector<Vertex> m_vertices;
        std::vector<PositionUvVertex> m_positionUvs;
        std::vector<AabbVertex> m_aabbVertices;
        std::vector<uint32_t> m_indices;
        mat4 matrix = mat4(1.f);
        // Dirty flags are used to update nodes and uniform buffers, they are important
        bool dirtyNodes = false;
        std::vector<bool> dirtyUniforms;

        uint32_t m_verticesCount = 0;
        uint32_t m_indicesCount = 0;
        uint32_t m_primitivesCount = 0;
    };

    // Inline implementations for performance
    inline void Model::FillVertexPosition(Vertex &vertex, float x, float y, float z)
    {
        vertex.position[0] = x;
        vertex.position[1] = y;
        vertex.position[2] = z;
    }

    inline void Model::FillVertexPosition(PositionUvVertex &vertex, float x, float y, float z)
    {
        vertex.position[0] = x;
        vertex.position[1] = y;
        vertex.position[2] = z;
    }

    inline void Model::FillVertexUV(Vertex &vertex, float u, float v)
    {
        vertex.uv[0] = u;
        vertex.uv[1] = v;
    }

    inline void Model::FillVertexUV(PositionUvVertex &vertex, float u, float v)
    {
        vertex.uv[0] = u;
        vertex.uv[1] = v;
    }

    inline void Model::FillVertexNormal(Vertex &vertex, float x, float y, float z)
    {
        vertex.normals[0] = x;
        vertex.normals[1] = y;
        vertex.normals[2] = z;
    }

    inline void Model::FillVertexColor(Vertex &vertex, float r, float g, float b, float a)
    {
        vertex.color[0] = r;
        vertex.color[1] = g;
        vertex.color[2] = b;
        vertex.color[3] = a;
    }

    inline void Model::FillVertexJointsWeights(Vertex &vertex, uint8_t j0, uint8_t j1, uint8_t j2, uint8_t j3,
                                               float w0, float w1, float w2, float w3)
    {
        vertex.joints[0] = j0;
        vertex.joints[1] = j1;
        vertex.joints[2] = j2;
        vertex.joints[3] = j3;
        vertex.weights[0] = w0;
        vertex.weights[1] = w1;
        vertex.weights[2] = w2;
        vertex.weights[3] = w3;
    }

    inline void Model::FillVertexJointsWeights(PositionUvVertex &vertex, uint8_t j0, uint8_t j1, uint8_t j2, uint8_t j3,
                                               float w0, float w1, float w2, float w3)
    {
        vertex.joints[0] = j0;
        vertex.joints[1] = j1;
        vertex.joints[2] = j2;
        vertex.joints[3] = j3;
        vertex.weights[0] = w0;
        vertex.weights[1] = w1;
        vertex.weights[2] = w2;
        vertex.weights[3] = w3;
    }
}
