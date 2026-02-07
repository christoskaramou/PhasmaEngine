#pragma once
#include "API/Vertex.h"

namespace pe
{
    class Buffer;
    struct Vertex;
    struct AabbVertex;
    class Sampler;
    class CommandBuffer;
    class Image;

    struct MeshInfo
    {
        const size_t dataSize = sizeof(mat4) * 4; // transform, previous transform, material factors (2x)
        uint32_t vertexOffset = 0, verticesCount = 0; // offset and count in used vertex buffer
        uint32_t indexOffset = 0, indicesCount = 0;   // offset and count in used index buffer
        uint32_t indirectIndex = 0;                   // index of the indirect command of the mesh in the indirect buffer
        uint32_t positionsOffset = 0;
        size_t aabbVertexOffset = 0;
        uint32_t aabbColor = 0;
        AABB boundingBox;
        RenderType renderType;

        Image *images[5]{nullptr};
        Sampler *samplers[5]{nullptr};
        uint32_t textureMask = 0;

        mat4 materialFactors[2] = {mat4(1.f), mat4(1.f)};
        float alphaCutoff = 0.5f;

    private:
        friend class Scene;

        // indices in the image views array in shader
        // 0 = Base Color
        // 1 = Normal
        // 2 = Metallic Roughness
        // 3 = Occlusion
        // 4 = Emissive
        uint32_t viewsIndex[5] = {0, 0, 0, 0, 0}; // Updated in Scene::UpdateImageViews
    };

    struct NodeInfo
    {
        AABB worldBoundingBox;
        int parent = -1;
        std::vector<int> children;
        mat4 localMatrix;
        size_t dataOffset = static_cast<size_t>(-1);
        uint32_t indirectIndex = 0;

        struct UBO
        {
            mat4 worldMatrix = mat4(1.f);
            mat4 previousWorldMatrix = mat4(1.f);
            mat4 materialFactors[2] = {mat4(1.f), mat4(1.f)};
        } ubo;

        bool dirty = false;
        std::vector<bool> dirtyUniforms;
        std::string name;
        int instanceIndex = -1; // Cache for TLAS update
    };

    // Base class for all model loaders
    class Model
    {
    public:
        Model();
        virtual ~Model();

        // Factory method to load models based on file extension
        static Model *Load(const std::filesystem::path &file);
        static void DestroyDefaults();

        // Common interface methods
        void MarkDirty(int node);
        void UpdateNodeMatrices();
        void ReparentNode(int nodeIndex, int newParentIndex);
        Image *LoadTexture(CommandBuffer *cmd, const std::filesystem::path &texturePath);
        Image *GetTextureFromCache(const std::filesystem::path &texturePath) const;

        // Getters
        size_t GetId() const { return m_id; }
        void SetRenderReady(bool ready) { m_render = ready; }
        bool IsRenderReady() const { return m_render; }

        const std::vector<Vertex> &GetVertices() const { return m_vertices; }
        const std::vector<PositionUvVertex> &GetPositionUvs() const { return m_positionUvs; }
        const std::vector<AabbVertex> &GetAabbVertices() const { return m_aabbVertices; }
        const std::vector<uint32_t> &GetIndices() const { return m_indices; }

        const std::vector<Image *> &GetImages() const { return m_images; }
        const std::vector<Sampler *> &GetSamplers() const { return m_samplers; }

        const std::vector<MeshInfo> &GetMeshInfos() const { return m_meshInfos; }
        const std::vector<NodeInfo> &GetNodeInfos() const { return m_nodeInfos; }
        std::vector<MeshInfo> &GetMeshInfos() { return m_meshInfos; }
        std::vector<NodeInfo> &GetNodeInfos() { return m_nodeInfos; }

        mat4 &GetMatrix() { return m_matrix; }
        bool &GetDirtyNodes() { return m_dirtyNodes; }
        bool IsMoved() { return !m_nodesMoved.empty(); }
        std::vector<int> &GetNodesMoved() { return m_nodesMoved; }
        void ClearNodesMoved() { m_nodesMoved.clear(); }
        std::vector<bool> &GetDirtyUniforms() { return m_dirtyUniforms; }

        uint32_t GetVerticesCount() const { return m_verticesCount; }
        uint32_t GetIndicesCount() const { return m_indicesCount; }
        uint32_t GetMeshCount() const { return m_meshCount; }

        const std::string &GetLabel() const { return m_label; }
        void SetLabel(const std::string &label) { m_label = label; }

        int GetNodeCount() const { return static_cast<int>(m_nodeInfos.size()); }
        int GetNodeMesh(int nodeIndex) const;

    protected:
        friend class Scene;
        friend class DepthPass;
        friend class GbufferOpaquePass;
        friend class GbufferTransparentPass;
        friend class ShadowPass;
        friend class AabbsPass;

        virtual void UpdateNodeMatrix(int node);
        static constexpr uint32_t TextureBit(TextureType type) { return 1u << static_cast<uint32_t>(type); }

        struct DefaultResources
        {
            Image *black = nullptr;
            Image *normal = nullptr;
            Image *white = nullptr;
            Sampler *sampler = nullptr;

            void EnsureCreated(CommandBuffer *cmd);
        };

        static DefaultResources &GetDefaultResources(CommandBuffer *cmd);
        static const DefaultResources &GetDefaultResources();
        static DefaultResources &Defaults();

        void ResetResources(CommandBuffer *cmd);

        // Resource ownership helpers:
        // - owned resources are destroyed in ~Model()
        // - shared resources are NOT destroyed (e.g. default textures/sampler)
        void AddImage(Image *image, bool owned);
        void AddSampler(Sampler *sampler, bool owned);

        std::atomic_bool m_render = false;
        size_t m_id;

        std::vector<Image *> m_images{};
        std::vector<Sampler *> m_samplers{};

        std::unordered_set<Image *> m_sharedImages{};
        std::unordered_set<Sampler *> m_sharedSamplers{};

        std::vector<MeshInfo> m_meshInfos{};
        std::vector<NodeInfo> m_nodeInfos{};
        std::vector<int> m_nodeToMesh{};

        std::vector<Vertex> m_vertices;
        std::vector<PositionUvVertex> m_positionUvs;
        std::vector<AabbVertex> m_aabbVertices;
        std::vector<uint32_t> m_indices;

        mat4 m_matrix = mat4(1.f);

        // Dirty flags are used to update nodes and uniform buffers, they are important
        bool m_dirtyNodes = false;
        std::vector<int> m_nodesMoved; // Stores indices of nodes moved this frame
        std::vector<bool> m_dirtyUniforms;

        uint32_t m_verticesCount = 0;
        uint32_t m_indicesCount = 0;
        uint32_t m_meshCount = 0;

        std::string m_label;
        std::filesystem::path m_filePath;

        std::unordered_map<std::string, Image *> m_textureCache;
        bool m_previousMatricesIsDirty = false;
    };
} // namespace pe
