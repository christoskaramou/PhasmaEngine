#pragma once

#include "Script/Script.h"
#include "Systems/CameraSystem.h"
#include "tinygltf/tiny_gltf.h"
#include "API/Vertex.h"
#include "API/Pipeline.h"

namespace pe
{
    class Buffer;
    struct Vertex;
    struct AabbVertex;
    class Sampler;

    struct MaterialInfo
    {
        PrimitiveTopology topology;
        CullMode cullMode;
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
        Image *images[5];
        Sampler *samplers[5];
        MaterialInfo materialInfo;

    private:
        // for drawing
        friend class ModelGltf;
        friend class Geometry;
        friend class DepthPass;
        friend class GbufferOpaquePass;
        friend class GbufferTransparentPass;
        friend class ShadowPass;

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
        bool dirtyUniforms[SWAPCHAIN_IMAGES];
    };

    class ModelGltf : public tinygltf::Model
    {
    public:
        static ModelGltf *Load(const std::filesystem::path &file);

        ModelGltf();
        ~ModelGltf();

        void UpdateNodeMatrices();
        void SetPrimitiveFactors(Buffer *uniformBuffer);

    private:
        friend class Geometry;
        friend class DepthPass;
        friend class GbufferOpaquePass;
        friend class GbufferTransparentPass;
        friend class ShadowPass;
        friend class AabbsPass;

        void UpdateNodeMatrix(int node);

        bool LoadFile(const std::filesystem::path &file, tinygltf::TinyGLTF &loader, std::string &err, std::string &warn);
        void UploadImages(CommandBuffer *cmd);
        void CreateSamplers();
        void ExtractMaterialInfo();
        void ProcessPrimitivesGeometry();
        void FillBuffersAndAABBs();
        void SetupNodes();
        void UpdateAllNodeMatrices();
        void UploadBuffers(CommandBuffer *cmd);

        std::atomic_bool m_render = false;
        size_t m_id;
        std::vector<Image *> m_images{};
        std::vector<Sampler *> m_samplers{};
        OrderedMap<size_t, Sampler *> m_samplersMap{};
        std::vector<MeshInfo> m_meshesInfo{};
        std::vector<NodeInfo> m_nodesInfo{};
        std::vector<Vertex> m_vertices;
        std::vector<PositionUvVertex> m_positionUvs;
        std::vector<AabbVertex> m_aabbVertices;
        std::vector<uint32_t> m_indices;
        mat4 matrix = mat4(1.f);
        // Dirty flags are used to update nodes and uniform buffers, they are important
        bool dirtyNodes = false;
        bool dirtyUniforms[SWAPCHAIN_IMAGES];

        uint32_t m_verticesCount = 0;
        uint32_t m_indicesCount = 0;
        uint32_t m_primitivesCount = 0;
    };
}
