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
        uint32_t vertexOffset = 0, verticesCount = 0; // offset and count in used vertex buffer
        uint32_t indexOffset = 0, indicesCount = 0;   // offset and count in used index buffer
        uint32_t positionsOffset = 0;
        size_t aabbVertexOffset = 0;
        uint32_t aabbColor = 0;
        AABB boundingBox;
        RenderType renderType;

        ResourceHandle<Image> images[5];
        Sampler *samplers[5]{nullptr};
        uint32_t textureMask = 0;

        mat4 materialFactors[2] = {mat4(1.f), mat4(1.f)};
    };

    struct NodeInfo
    {
        int parent = -1;
        std::vector<int> children;
        mat4 localMatrix;

        std::string name;
    };

    class ModelAsset : public Resource
    {
    public:
        struct NodeGpuData
        {
            mat4 worldMatrix = mat4(1.f);
            mat4 previousWorldMatrix = mat4(1.f);
            mat4 materialFactors[2] = {mat4(1.f), mat4(1.f)};
        };

        ModelAsset();
        virtual ~ModelAsset();

        virtual void Load() override {}
        virtual void Unload() override;

        // Factory method to load models based on file extension
        static ModelAsset *Load(const std::filesystem::path &file);
        static void DestroyDefaults();

        // Common interface methods
        int CreateNode(const std::string &name, int parentIndex = -1, const mat4 &localMatrix = mat4(1.f), int meshIndex = -1);
        void MarkDirty(int node);
        void UpdateNodeMatrices();
        void ReparentNode(int nodeIndex, int newParentIndex);

        // Removes a node and its subtree. Returns true if the model is now empty (caller should delete it).
        bool RemoveNode(int nodeIndex);

        // Removes only the mesh from a node, keeping the node and its children intact.
        void RemoveMesh(int nodeIndex);
        ResourceHandle<Image> LoadTexture(CommandBuffer *cmd, const std::filesystem::path &texturePath);

        // Getters
        size_t GetId() const { return m_id; }
        void SetRenderReady(bool ready) { m_render = ready; }
        bool IsRenderReady() const { return m_render; }

        const std::vector<Vertex> &GetVertices() const { return m_vertices; }
        const std::vector<PositionUvVertex> &GetPositionUvs() const { return m_positionUvs; }
        const std::vector<AabbVertex> &GetAabbVertices() const { return m_aabbVertices; }
        const std::vector<uint32_t> &GetIndices() const { return m_indices; }

        const std::vector<ResourceHandle<Image>> &GetImages() const { return m_images; }
        const std::vector<Sampler *> &GetSamplers() const { return m_samplers; }

        const std::vector<MeshInfo> &GetMeshInfos() const { return m_meshInfos; }
        const std::vector<NodeInfo> &GetNodeInfos() const { return m_nodeInfos; }

        mat4 &GetMatrix() { return m_matrix; }
        const mat4 &GetMatrix() const { return m_matrix; }
        void SetMatrix(const mat4 &matrix, bool markDirty = true);
        bool HasDirtyNodes() const { return m_dirtyNodes; }
        void SetDirtyNodes(bool dirty) { m_dirtyNodes = dirty; }
        bool HasMovedNodes() const { return !m_nodesMoved.empty(); }
        const std::vector<int> &GetMovedNodes() const { return m_nodesMoved; }
        void ClearMovedNodes() { m_nodesMoved.clear(); }
        void MarkAllUniformsDirty();
        bool IsUniformDirty(uint32_t frameIndex) const;
        void SetUniformDirty(uint32_t frameIndex, bool dirty);

        uint32_t GetVerticesCount() const { return m_verticesCount; }
        uint32_t GetIndicesCount() const { return m_indicesCount; }
        uint32_t GetMeshCount() const { return m_meshCount; }
        int GetRootNodeIndex() const;
        static constexpr size_t GetNodeGpuDataSize() { return sizeof(NodeGpuData); }

        const std::string &GetLabel() const { return m_label; }
        const std::filesystem::path &GetFilePath() const { return m_filePath; }
        void SetLabel(const std::string &label) { m_label = label; }
        const std::string &GetPrimitiveType() const { return m_primitiveType; }
        void SetPrimitiveType(const std::string &type) { m_primitiveType = type; }
        bool IsPrimitive() const { return !m_primitiveType.empty(); }
        void SetNodeName(int nodeIndex, const std::string &name);
        const std::string &GetNodeName(int nodeIndex) const;
        NodeInfo *GetNodeInfo(int nodeIndex);
        const NodeInfo *GetNodeInfo(int nodeIndex) const;
        const NodeGpuData *GetNodeGpuData(int nodeIndex) const;
        void SyncNodeGpuDataFromMesh(int nodeIndex);
        const mat4 &GetNodeLocalMatrix(int nodeIndex) const;
        void SetNodeLocalMatrix(int nodeIndex, const mat4 &localMatrix, bool markDirty = true);
        const mat4 &GetNodeWorldMatrix(int nodeIndex) const;
        int GetNodeParentIndex(int nodeIndex) const;
        void SetNodeParentIndex(int nodeIndex, int parentIndex);
        const std::vector<int> &GetNodeChildren(int nodeIndex) const;
        void RebuildNodeChildrenFromParents();
        const AABB &GetNodeWorldBoundingBox(int nodeIndex) const;

        int GetNodeCount() const { return static_cast<int>(m_nodeInfos.size()); }
        int GetMeshInfoCount() const { return static_cast<int>(m_meshInfos.size()); }
        MeshInfo *GetMeshInfo(int meshIndex);
        const MeshInfo *GetMeshInfo(int meshIndex) const;
        int GetNodeMesh(int nodeIndex) const;
        size_t GetNodeDataOffset(int nodeIndex) const;
        void SetNodeDataOffset(int nodeIndex, size_t offset);
        uint32_t GetNodeIndirectIndex(int nodeIndex) const;
        void SetNodeIndirectIndex(int nodeIndex, uint32_t index);
        int GetNodeInstanceIndex(int nodeIndex) const;
        void SetNodeInstanceIndex(int nodeIndex, int instanceIndex);
        bool IsNodeDirty(int nodeIndex) const;
        void SetNodeDirty(int nodeIndex, bool dirty);
        void MarkNodeUniformsDirty(int nodeIndex);
        bool IsNodeUniformDirty(int nodeIndex, uint32_t frameIndex) const;
        void SetNodeUniformDirty(int nodeIndex, uint32_t frameIndex, bool dirty);
        uint32_t GetMeshImageViewIndex(int meshIndex, int slot) const;
        void SetMeshImageViewIndex(int meshIndex, int slot, uint32_t imageViewIndex);

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

    protected:
        friend class Scene;
        friend class Primitives;

        virtual void UpdateNodeMatrix(int node);
        static constexpr uint32_t TextureBit(TextureType type) { return 1u << static_cast<uint32_t>(type); }

        void ResetResources(CommandBuffer *cmd);
        void ResetRuntimeState();

        // Resource ownership helpers:
        // - owned resources are destroyed in ~ModelAsset()
        // - shared resources are NOT destroyed (e.g. default textures/sampler)
        void AddImage(Image *image, bool owned);
        void AddSampler(Sampler *sampler, bool owned);

        struct MeshRuntimeInfo
        {
            uint32_t imageViewIndices[5] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
        };

        struct NodeRuntimeInfo
        {
            size_t dataOffset = static_cast<size_t>(-1);
            uint32_t indirectIndex = 0;
            int instanceIndex = -1;
            AABB worldBoundingBox;
            NodeGpuData gpuData;
            bool dirty = false;
            std::vector<bool> dirtyUniforms;
        };

        std::atomic_bool m_render = false;
        size_t m_id;

        std::vector<ResourceHandle<Image>> m_images{};
        std::vector<Sampler *> m_samplers{};

        std::unordered_set<Sampler *> m_sharedSamplers{};

        std::vector<MeshInfo> m_meshInfos{};
        std::vector<MeshRuntimeInfo> m_meshRuntimeInfos{};
        std::vector<NodeInfo> m_nodeInfos{};
        std::vector<NodeRuntimeInfo> m_nodeRuntimeInfos{};
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
        std::string m_primitiveType; // "cube", "sphere", "plane", etc. Empty for file-loaded models.
        std::filesystem::path m_filePath;

        bool m_previousMatricesIsDirty = false;
    };
} // namespace pe
