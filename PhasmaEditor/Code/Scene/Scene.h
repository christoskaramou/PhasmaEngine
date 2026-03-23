#pragma once

#include "API/Vertex.h"
#include "Scene/SceneNode.h"

namespace pe
{
    class AccelerationStructure;
    class AssimpLoader;
    class Buffer;
    class Camera;
    class CommandBuffer;
    class Image;
    class ImageView;
    class ModelAsset;
    class Sampler;

    struct DrawInfo
    {
        NodeId *node;
        float distance;
    };

    class ParticleManager;

    class Scene
    {
        friend class AssimpLoader;

    public:
        Scene();
        ~Scene();

        void Update();
        void UpdateGeometryBuffers();
        void UpdateTextures();
        void UploadBuffers(CommandBuffer *cmd);
        void UpdateTLASTransformations(CommandBuffer *cmd); // Update instance transforms and rebuild TLAS
        void AddModel(ModelAsset *model);
        void RemoveModel(ModelAsset *model);
        void RemoveModels(std::vector<ModelAsset *> models);
        Camera *AddCamera();
        void RemoveCamera(Camera *camera);
        void SetActiveCamera(Camera *camera);
        void NewScene();
        void SaveScene(const std::filesystem::path &file);
        void LoadScene(const std::filesystem::path &file);

        // Two-phase async loading.
        // Phase 1 — safe on any thread, no scene mutation: parses JSON + loads ModelAssets from disk.
        struct ScenePreload
        {
            std::filesystem::path filePath;
            std::string           jsonText;
            std::vector<ModelAsset *> models; // pre-loaded in source/model order; may contain nulls
            bool                  valid = false;

            ScenePreload() = default;
            ScenePreload(const ScenePreload &) = delete;
            ScenePreload &operator=(const ScenePreload &) = delete;
            ScenePreload(ScenePreload &&) = default;
            ScenePreload &operator=(ScenePreload &&) = default;
            ~ScenePreload();
        };
        static ScenePreload PreloadScene(const std::filesystem::path &file);
        // Phase 2 — must run on the main render thread after WaitAllFramesCommands():
        // clears old scene and applies the pre-loaded content.
        void LoadSceneApply(ScenePreload preload);
        std::string TakeSnapshot() const;
        void RestoreSnapshot(const std::string &json);

        const std::filesystem::path &GetScenePath() const { return m_scenePath; }
        void SetScenePath(const std::filesystem::path &path) { m_scenePath = path; }
        std::string GetSceneName() const;
        bool IsDirty() const { return m_dirty; }
        void MarkDirty() { m_dirty = true; }
        void ClearDirty() { m_dirty = false; }

        ParticleManager *GetParticleManager() { return m_particleManager; }
        Camera *GetActiveCamera() const { return m_cameras.at(0); }
        Camera *GetCamera(int index) const { return m_cameras.at(index); }
        const std::vector<Camera *> &GetCameras() const { return m_cameras; }
        OrderedMap<size_t, ModelAsset *> &GetModels() { return m_models; }
        const OrderedMap<size_t, ModelAsset *> &GetModels() const { return m_models; }

        bool HasOpaqueDrawInfo() const { return !m_drawInfosOpaque.empty() || !m_drawInfosAlphaCut.empty(); }
        bool HasAlphaDrawInfo() const { return !m_drawInfosAlphaBlend.empty() || !m_drawInfosTransmission.empty(); }
        bool HasDrawInfo() const { return HasOpaqueDrawInfo() || HasAlphaDrawInfo(); }

        uint64_t GetGeometryVersion() const { return m_geometryVersion; }

        Buffer *GetUniforms(uint32_t frame) { return m_storages[frame]; }
        Buffer *GetIndirect(uint32_t frame) { return m_indirects[frame]; }
        Buffer *GetIndirectAll() { return m_indirectAll; }
        Buffer *GetBuffer() { return m_buffer; }
        AccelerationStructure *GetTLAS() { return m_tlas; }
        Buffer *GetInstanceBuffer() { return m_instanceBuffer; }
        Buffer *GetMeshInfoBuffer() { return m_meshInfoBuffer; }
        size_t GetVerticesOffset() const { return m_verticesOffset; }
        size_t GetPositionsOffset() const { return m_positionsOffset; }
        size_t GetAabbVerticesOffset() const { return m_aabbVerticesOffset; }
        size_t GetAabbIndicesOffset() const { return m_aabbIndicesOffset; }
        const std::vector<DrawInfo> &GetDrawInfosOpaque() const { return m_drawInfosOpaque; }
        const std::vector<DrawInfo> &GetDrawInfosAlphaCut() const { return m_drawInfosAlphaCut; }
        const std::vector<DrawInfo> &GetDrawInfosAlphaBlend() const { return m_drawInfosAlphaBlend; }
        const std::vector<DrawInfo> &GetDrawInfosTransmission() const { return m_drawInfosTransmission; }
        const std::vector<ImageView *> &GetImageViews() const { return m_imageViews; }
        uint32_t GetMeshCount() const { return m_meshCount; }
        Buffer *GetMeshConstants() { return m_meshConstants; }
        Sampler *GetDefaultSampler() const;
        static const std::vector<uint32_t> &GetAabbIndices() { return s_aabbIndices; }

        // --- Node Graph (SoA) ---
        NodeId *CreateNode(const std::string &name, NodeId *parent = nullptr);
        void DeleteNode(NodeId *node);
        void ReparentNode(NodeId *node, NodeId *newParent);

        // Node accessors (all indexed via NodeId::index)
#ifdef PE_DEBUG
        void ValidateNodeId(const NodeId *node) const
        {
            PE_ERROR_IF(!node, "ValidateNodeId: null NodeId");
            PE_ERROR_IF(node->index >= m_nodeIds.size(), "ValidateNodeId: index %u out of range (size %zu)", node->index, m_nodeIds.size());
            PE_ERROR_IF(m_nodeIds[node->index] != node, "ValidateNodeId: stale NodeId (recycled or corrupted)");
        }
#else
        void ValidateNodeId(const NodeId *) const {}
#endif

        const std::string &GetNodeName(const NodeId *node) const { ValidateNodeId(node); return m_nodeNames[node->index]; }
        void SetNodeName(NodeId *node, const std::string &name) { ValidateNodeId(node); m_nodeNames[node->index] = name; }

        const mat4 &GetLocalMatrix(const NodeId *node) const { return m_localMatrices[node->index]; }
        void SetLocalMatrix(NodeId *node, const mat4 &m, bool markDirty = true);

        const mat4 &GetWorldMatrix(const NodeId *node) const { return m_nodeRuntime[node->index].gpuData.worldMatrix; }
        const AABB &GetWorldAABB(const NodeId *node) const { return m_nodeRuntime[node->index].worldAABB; }

        NodeId *GetParent(const NodeId *node) const { return m_nodeParents[node->index]; }
        const std::vector<NodeId *> &GetChildren(const NodeId *node) const { return m_nodeChildren[node->index]; }

        uint32_t GetComponentFlags(const NodeId *node) const { return m_componentFlags[node->index]; }
        int GetMeshRef(const NodeId *node) const { return m_meshRefs[node->index]; }
        void SetMeshRef(NodeId *node, int meshIndex);

        NodeRuntime &GetNodeRuntime(const NodeId *node) { return m_nodeRuntime[node->index]; }
        const NodeRuntime &GetNodeRuntime(const NodeId *node) const { return m_nodeRuntime[node->index]; }

        uint32_t GetNodeCount() const { return static_cast<uint32_t>(m_nodeIds.size()); }
        NodeId *GetNodeId(uint32_t index) { return m_nodeIds[index]; }
        const NodeId *GetNodeId(uint32_t index) const { return m_nodeIds[index]; }

        // Mesh store
        const std::vector<Mesh> &GetMeshes() const { return m_meshes; }
        Mesh &GetMesh(int index) { return m_meshes[index]; }
        const Mesh &GetMesh(int index) const { return m_meshes[index]; }
        MeshRuntime &GetMeshRuntime(int index) { return m_meshRuntimes[index]; }
        int AddMesh(Mesh &&mesh);

        // Data stores
        std::vector<Vertex> &GetVertexStore() { return m_vertexStore; }
        std::vector<PositionUvVertex> &GetPositionUvStore() { return m_positionUvStore; }
        std::vector<AabbVertex> &GetAabbVertexStore() { return m_aabbVertexStore; }
        std::vector<uint32_t> &GetIndexStore() { return m_indexStore; }
        std::vector<ResourceHandle<Image>> &GetImageStore() { return m_imageStore; }
        std::vector<Sampler *> &GetSamplerStore() { return m_samplerStore; }

        void UpdateNodeMatrices();
        void MarkNodeDirty(NodeId *node);

    private:
        struct DrawBatch
        {
            std::vector<DrawInfo> opaque;
            std::vector<DrawInfo> alphaCut;
            std::vector<DrawInfo> alphaBlend;
            std::vector<DrawInfo> transmission;
        };

        void UpdateGeometry();
        DrawBatch CullNodeBatch(uint32_t beginNode, uint32_t endNode, const Camera *camera, bool frustumCulling) const;
        void UpdateUniformData();
        void UpdateIndirectData();
        void ClearDrawInfos(bool reserveMax);
        void DestroyBuffers();
        void SortDrawInfos();
        void MarkUniformsDirty();
        void CreateGeometryBuffer();
        void CopyIndices(CommandBuffer *cmd);
        void CopyVertices(CommandBuffer *cmd);
        void CreateStorageBuffers();
        void CreateIndirectBuffers(CommandBuffer *cmd);
        void UpdateImageViews();
        void CreateMeshConstants(CommandBuffer *cmd);
        void BuildAccelerationStructures(CommandBuffer *cmd);

        struct alignas(64) PerFrameData
        {
            mat4 viewProjection;
            mat4 previousViewProjection;
            mat4 invView;
            mat4 invProjection;
        };

        PerFrameData m_frameData{};
        std::vector<Camera *> m_cameras;
        OrderedMap<size_t, ModelAsset *> m_models;

        ParticleManager *m_particleManager = nullptr;

        Buffer *m_buffer = nullptr;
        std::vector<Buffer *> m_storages;
        std::vector<Buffer *> m_indirects;
        Buffer *m_indirectAll = nullptr;

        size_t m_verticesOffset = 0;
        size_t m_positionsOffset = 0;
        size_t m_aabbVerticesOffset = 0;
        size_t m_aabbIndicesOffset = 0;
        uint32_t m_meshCount = 0;
        uint32_t m_indicesCount = 0;
        uint32_t m_verticesCount = 0;
        uint32_t m_positionsCount = 0;
        uint32_t m_aabbVerticesCount = 0;

        std::vector<ImageView *> m_imageViews;
        uint64_t m_geometryVersion = 0;

        std::vector<DrawInfo> m_drawInfosOpaque;
        std::vector<DrawInfo> m_drawInfosAlphaCut;
        std::vector<DrawInfo> m_drawInfosAlphaBlend;
        std::vector<DrawInfo> m_drawInfosTransmission;
        std::vector<vk::DrawIndexedIndirectCommand> m_indirectCommands;
        std::vector<uint32_t> m_visibleIndirectIds;
        uint64_t m_drawInfosReservedForGeometryVersion = 0;
        bool m_hasDrawInfosReservation = false;

        // Ray Tracing
        struct alignas(16) MeshInfoGPU
        {
            uint32_t indexOffset;
            uint32_t vertexOffset;
            int textures[5]; // BaseColor, Normal, Metallic, Occlusion, Emissive
            // int padding; // implicit padding or explicit? alignas(16) might need care.
            // 3 uints + 5 ints = 32 bytes (perfectly aligned to 16 if indices are 4 bytes)
            // 3*4 + 5*4 = 12 + 20 = 32 bytes. No padding needed if struct is 32 bytes.
        };
        std::vector<AccelerationStructure *> m_blases;
        AccelerationStructure *m_tlas = nullptr;
        Buffer *m_instanceBuffer = nullptr;
        Buffer *m_blasMergedBuffer = nullptr;
        Buffer *m_scratchBuffer = nullptr;
        Buffer *m_meshInfoBuffer = nullptr;
        Buffer *m_meshConstants = nullptr;
        Sampler *m_defaultSampler = nullptr;

        static std::vector<uint32_t> s_aabbIndices;

        // Scene source: tracks where geometry came from (for save/load)
        struct SceneSource
        {
            std::filesystem::path filePath;
            std::string primitiveType; // non-empty for primitives
        };

        // Per-mesh source tracking (parallel to m_meshes)
        struct MeshSourceInfo
        {
            int sourceIndex = -1;     // index into m_sources
            int sourceMeshIndex = -1; // mesh index within the original file
        };

        // Copies geometry (vertices, indices, images, meshes) from ModelAsset into Scene stores.
        // Does NOT create nodes. Returns meshMap (source mesh → scene mesh).
        std::vector<int> AddModelGeometry(ModelAsset *model, int sourceIndex);

        std::vector<SceneSource>    m_sources;
        std::vector<MeshSourceInfo> m_meshSourceInfos; // parallel to m_meshes
        std::unordered_map<size_t, std::vector<NodeId *>> m_modelRootNodes; // model ID → root nodes for cleanup

        std::filesystem::path m_scenePath;
        bool m_dirty = false;

        // --- Node Graph SoA Storage ---
        void SwapAndPopNode(uint32_t index);
        void UpdateNodeMatrix(NodeId *node);

        std::vector<NodeId *>              m_nodeIds;        // stable identity per node
        std::vector<std::string>           m_nodeNames;
        std::vector<mat4>                  m_localMatrices;
        std::vector<NodeId *>              m_nodeParents;    // nullptr = root
        std::vector<std::vector<NodeId *>> m_nodeChildren;
        std::vector<uint32_t>              m_componentFlags;
        std::vector<int>                   m_meshRefs;       // -1 = empty node
        std::vector<NodeRuntime>           m_nodeRuntime;    // GPU hot-path data
        std::vector<NodeId *>              m_freeNodeIds;    // recycled NodeId pointers

        // Mesh store (lightweight descriptors referencing data stores)
        std::vector<Mesh>                  m_meshes;
        std::vector<MeshRuntime>           m_meshRuntimes;

        // Data stores (the actual heavy data)
        std::vector<Vertex>                m_vertexStore;
        std::vector<PositionUvVertex>      m_positionUvStore;
        std::vector<AabbVertex>            m_aabbVertexStore;
        std::vector<uint32_t>              m_indexStore;
        std::vector<ResourceHandle<Image>> m_imageStore;
        std::vector<Sampler *>             m_samplerStore;

        bool m_nodesDirty = false;
        std::vector<NodeId *>              m_nodesMoved;
    };
} // namespace pe
