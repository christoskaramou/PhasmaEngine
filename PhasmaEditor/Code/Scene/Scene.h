#pragma once

#include "API/Vertex.h"
#include "Scene/SceneNode.h"
#include "Scene/SceneNodeHandle.h"
#include "Scene/SelectionManager.h"

namespace pe
{
    class AccelerationStructure;
    class AssimpLoader;
    class Buffer;
    class Camera;
    class CommandBuffer;
    class Image;
    class ImageView;
    class Material;
    class MaterialInstance;
    class ModelAsset;
    class Sampler;

    // --- Light POD structs (GPU layout) ---
    struct DirectionalLight
    {
        vec4 color; // .w = intensity
        vec4 position;
        vec4 rotation; // quaternion
    };

    struct PointLight
    {
        vec4 color;    // .w = intensity
        vec4 position; // .w = radius
    };

    struct SpotLight
    {
        vec4 color;    // .w = intensity
        vec4 position; // .w = range
        vec4 rotation; // quaternion
        vec4 params;   // .x = angle, .y = falloff
    };

    struct AreaLight
    {
        vec4 color;    // .w = intensity
        vec4 position; // .w = range
        vec4 rotation; // quaternion
        vec4 size;     // .x = width, .y = height
    };

    // --- Light editor structs (GPU data + scene metadata) ---
    struct DirectionalLightEditor : public DirectionalLight
    {
        std::string name;
        NodeId *nodeId = nullptr;
    };

    struct PointLightEditor : public PointLight
    {
        std::string name;
        NodeId *nodeId = nullptr;
    };

    struct SpotLightEditor : public SpotLight
    {
        std::string name;
        NodeId *nodeId = nullptr;
    };

    struct AreaLightEditor : public AreaLight
    {
        std::string name;
        NodeId *nodeId = nullptr;
    };

    struct LightsUBO
    {
        uint32_t numDirectionalLights;
        uint32_t numPointLights;
        uint32_t numSpotLights;
        uint32_t numAreaLights;
        uint32_t offsetDirectionalLights;
        uint32_t offsetPointLights;
        uint32_t offsetSpotLights;
        uint32_t offsetAreaLights;
    };

    struct DrawInfo
    {
        NodeId *node;
        float distance;
        bool doubleSided = false;
    };

    class ParticleManager;

    class Scene
    {
        friend class AssimpLoader;

    public:
        // Payload for PrimitiveAttachedToNode event
        struct PrimitiveAttachRequest
        {
            NodeId *node;
            ModelAsset *model;
        };

        // Payload for ModelLoadedForNode event — load model and parent its roots under node
        struct ModelLoadForNodeRequest
        {
            NodeId *parentNode;
            ModelAsset *model;
        };
        // --- Lifecycle ---
        Scene();
        ~Scene();

        void Update();
        void UpdateGeometryBuffers();
        void UpdateTextures();
        void UpdateDirtyMaterials();                          // incremental GPU update for scalar/texture property changes
        MaterialInstance *CreateMaterialInstance(Mesh &mesh); // creates instance from mesh's shared material
        void DestroyMaterialInstance(Mesh &mesh);             // removes instance, reverts to shared material
        void UploadBuffers(CommandBuffer *cmd);
        void UpdateTLASTransformations(CommandBuffer *cmd);
        void AddModel(ModelAsset *model);
        SceneNodeHandle AddModelDeferred(ModelAsset *model);
        // Returns the root nodes created by the last AddModel call for the given model.
        const std::vector<NodeId *> &GetModelRootNodes(ModelAsset *model) const;
        static const std::vector<NodeId *> &EmptyRootNodes();
        void RemoveModel(ModelAsset *model);
        void RemoveModels(std::vector<ModelAsset *> models);
        Camera *AddCamera(NodeId *parent = nullptr);
        void RemoveCamera(Camera *camera);
        void SetActiveCamera(Camera *camera);
        void CreateDirectionalLight(NodeId *parent = nullptr);
        void CreatePointLight(NodeId *parent = nullptr);
        void CreateSpotLight(NodeId *parent = nullptr);
        void CreateAreaLight(NodeId *parent = nullptr);
        void RemoveLight(LightType type, int index);
        std::pair<LightType, int> GetLightForNode(const NodeId *node) const;
        void NewScene();
        void SaveScene(const std::filesystem::path &file);
        void LoadScene(const std::filesystem::path &file);

        // Two-phase async loading
        struct ScenePreload
        {
            std::filesystem::path filePath;
            std::string jsonText;
            std::vector<ModelAsset *> models;
            bool valid = false;

            ScenePreload() = default;
            ScenePreload(const ScenePreload &) = delete;
            ScenePreload &operator=(const ScenePreload &) = delete;
            ScenePreload(ScenePreload &&) = default;
            ScenePreload &operator=(ScenePreload &&) = default;
            ~ScenePreload();
        };
        static ScenePreload PreloadScene(const std::filesystem::path &file);
        void LoadSceneApply(ScenePreload preload);
        std::string TakeSnapshot() const;
        void RestoreSnapshot(const std::string &json);

        // --- Node Graph (SoA) ---
        NodeId *CreateNode(const std::string &name, NodeId *parent = nullptr);
        void DeleteNode(NodeId *node);
        void ReparentNode(NodeId *node, NodeId *newParent);
        void SetLocalMatrix(NodeId *node, const mat4 &m, bool markDirty = true);
        void SetMeshRef(NodeId *node, int meshIndex);
        void SetNodeScript(NodeId *node, const std::string &path);
        void AttachPrimitiveToNode(NodeId *node, ModelAsset *primitiveModel);
        void SetNodeName(NodeId *node, const std::string &name)
        {
            ValidateNodeId(node);
            m_nodeNames[node->index] = name;
        }
        void UpdateNodeMatrices();
        void MarkNodeDirty(NodeId *node);
        int AddMesh(Mesh &&mesh);

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

        // --- Accessors ---
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
        Camera *GetCameraForNode(const NodeId *node) const;

        inline void AddComponentFlag(NodeId *node, uint32_t flag)
        {
            ValidateNodeId(node);
            m_componentFlags[node->index] |= flag;
        }
        OrderedMap<size_t, ModelAsset *> &GetModels() { return m_models; }
        const OrderedMap<size_t, ModelAsset *> &GetModels() const { return m_models; }

        bool HasOpaqueDrawInfo() const { return !m_drawInfosOpaque.empty() || !m_drawInfosAlphaCut.empty(); }
        bool HasAlphaDrawInfo() const { return !m_drawInfosAlphaBlend.empty() || !m_drawInfosTransmission.empty(); }
        bool HasDrawInfo() const { return HasOpaqueDrawInfo() || HasAlphaDrawInfo(); }

        uint64_t GetGeometryVersion() const { return m_geometryVersion; }

        uint32_t GetGeneration() const { return m_generation; }
        SceneNodeHandle MakeHandle(NodeId *node) const
        {
            return SceneNodeHandle(node, m_generation);
        }
        bool IsGeometryDirty() const { return m_geometryDirty; }
        void SetGeometryDirty() { m_geometryDirty = true; }
        void SetMaterialDirty() { m_materialDirty = true; }
        void SetTexturesDirty() { m_texturesDirty = true; }
        void FlushPendingGpuWork();

        Buffer *GetUniforms(uint32_t frame) { return m_storages[frame]; }
        Buffer *GetIndirect(uint32_t frame) { return m_indirects[frame]; }
        Buffer *GetIndirectAll() { return m_indirectAll; }
        Buffer *GetBuffer() { return m_buffer; }
        Buffer *GetLightUniform(uint32_t frame) { return m_lightUniforms[frame]; }
        Buffer *GetLightStorage(uint32_t frame) { return m_lightStorageBuffers[frame]; }
        std::vector<DirectionalLightEditor> &GetDirectionalLights() { return m_directionalLights; }
        std::vector<PointLightEditor> &GetPointLights() { return m_pointLights; }
        std::vector<SpotLightEditor> &GetSpotLights() { return m_spotLights; }
        std::vector<AreaLightEditor> &GetAreaLights() { return m_areaLights; }
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
        uint32_t GetOpaqueSingleSidedCount() const { return m_opaqueSingleSidedCount; }
        uint32_t GetAlphaCutSingleSidedCount() const { return m_alphaCutSingleSidedCount; }
        const std::vector<ImageView *> &GetImageViews() const { return m_imageViews; }
        uint32_t GetMeshCount() const { return m_meshCount; }
        Buffer *GetMeshConstants() { return m_meshConstants; }
        Buffer *GetMaterialTable() { return m_materialTable; }
        Sampler *GetDefaultSampler() const;
        static const std::vector<uint32_t> &GetAabbIndices() { return s_aabbIndices; }

        // Node accessors (all indexed via NodeId::index)
        const std::string &GetNodeName(const NodeId *node) const
        {
            ValidateNodeId(node);
            return m_nodeNames[node->index];
        }
        const mat4 &GetLocalMatrix(const NodeId *node) const { return m_localMatrices[node->index]; }
        const mat4 &GetWorldMatrix(const NodeId *node) const { return m_nodeRuntime[node->index].gpuData.worldMatrix; }
        const AABB &GetWorldAABB(const NodeId *node) const { return m_nodeRuntime[node->index].worldAABB; }
        NodeId *GetParent(const NodeId *node) const { return m_nodeParents[node->index]; }
        const std::vector<NodeId *> &GetChildren(const NodeId *node) const { return m_nodeChildren[node->index]; }
        uint32_t GetComponentFlags(const NodeId *node) const { return m_componentFlags[node->index]; }
        int GetMeshRef(const NodeId *node) const { return m_meshRefs[node->index]; }
        const std::string &GetNodeScriptPath(const NodeId *node) const { return m_nodeScriptPaths[node->index]; }
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

        // Data stores
        std::vector<Vertex> &GetVertexStore() { return m_vertexStore; }
        std::vector<PositionUvVertex> &GetPositionUvStore() { return m_positionUvStore; }
        std::vector<AabbVertex> &GetAabbVertexStore() { return m_aabbVertexStore; }
        std::vector<uint32_t> &GetIndexStore() { return m_indexStore; }
        std::vector<ResourceHandle<Image>> &GetImageStore() { return m_imageStore; }
        std::vector<Sampler *> &GetSamplerStore() { return m_samplerStore; }

    private:
        // --- Private types ---
        struct DrawBatch
        {
            std::vector<DrawInfo> opaque;
            std::vector<DrawInfo> alphaCut;
            std::vector<DrawInfo> alphaBlend;
            std::vector<DrawInfo> transmission;
        };

        struct alignas(64) PerFrameData
        {
            mat4 viewProjection;
            mat4 previousViewProjection;
            mat4 invView;
            mat4 invProjection;
        };

        struct SceneSource
        {
            std::filesystem::path filePath;
            std::string primitiveType;
        };

        struct MeshSourceInfo
        {
            int sourceIndex = -1;
            int sourceMeshIndex = -1;
        };

        struct alignas(16) MeshInfoGPU
        {
            uint32_t indexOffset;
            uint32_t vertexOffset;
            int textures[5];
        };

        // --- Private functions ---
        void InitLightBuffers();
        void DestroyLightBuffers();
        void UpdateLights();
        NodeId *CreateLightNode(const std::string &name, const mat4 &localMatrix, NodeId *parent);
        void UpdateGeometry();
        void CullNodeBatch(uint32_t beginNode, uint32_t endNode, const Camera *camera, bool frustumCulling, DrawBatch &out) const;
        void UpdateUniformData();
        void UpdateIndirectData();
        void ClearDrawInfos(bool reserveMax);
        void SortDrawInfos();

        // Buffer management (SceneBuffers.cpp)
        void DestroyBuffers();
        void CreateGeometryBuffer();
        void CopyIndices(CommandBuffer *cmd);
        void CopyVertices(CommandBuffer *cmd);
        void CreateStorageBuffers();
        void MarkUniformsDirty();
        void CreateIndirectBuffers(CommandBuffer *cmd);
        void UpdateImageViews();
        void CreateMaterialTable();
        void CreateMeshConstants(CommandBuffer *cmd);

        // Ray tracing (SceneRayTracing.cpp)
        void BuildAccelerationStructures(CommandBuffer *cmd);

        // Node graph internals (SceneNode.cpp)
        void SwapAndPopNode(uint32_t index);
        void UpdateNodeMatrix(NodeId *node);

        // Model geometry
        std::vector<int> AddModelGeometry(ModelAsset *model, int sourceIndex);

        // --- Private variables ---
        PerFrameData m_frameData{};
        std::vector<Camera *> m_cameras;
        OrderedMap<size_t, ModelAsset *> m_models;

        ParticleManager *m_particleManager = nullptr;

        // GPU buffers
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

        // Draw info
        std::vector<DrawInfo> m_drawInfosOpaque;
        std::vector<DrawInfo> m_drawInfosAlphaCut;
        std::vector<DrawInfo> m_drawInfosAlphaBlend;
        std::vector<DrawInfo> m_drawInfosTransmission;
        // Within opaque/alphaCut lists: [0, SS_count) = single-sided; [SS_count, end) = double-sided
        uint32_t m_opaqueSingleSidedCount = 0;
        uint32_t m_alphaCutSingleSidedCount = 0;
        std::vector<vk::DrawIndexedIndirectCommand> m_indirectCommands;
        std::vector<uint32_t> m_visibleIndirectIds;
        uint64_t m_drawInfosReservedForGeometryVersion = 0;
        bool m_hasDrawInfosReservation = false;
        std::vector<DrawBatch> m_cullBatches;

        // Ray tracing
        std::vector<AccelerationStructure *> m_blases;
        AccelerationStructure *m_tlas = nullptr;
        Buffer *m_instanceBuffer = nullptr;
        Buffer *m_blasMergedBuffer = nullptr;
        Buffer *m_scratchBuffer = nullptr;
        Buffer *m_meshInfoBuffer = nullptr;
        Buffer *m_meshConstants = nullptr;
        Buffer *m_materialTable = nullptr;
        Sampler *m_defaultSampler = nullptr;

        static std::vector<uint32_t> s_aabbIndices;

        // Scene source tracking
        std::vector<SceneSource> m_sources;
        std::vector<MeshSourceInfo> m_meshSourceInfos;
        std::unordered_map<size_t, std::vector<NodeId *>> m_modelRootNodes;

        std::filesystem::path m_scenePath;
        bool m_dirty = false;

        // Node Graph SoA Storage
        std::vector<NodeId *> m_nodeIds;
        std::vector<std::string> m_nodeNames;
        std::vector<mat4> m_localMatrices;
        std::vector<NodeId *> m_nodeParents;
        std::vector<std::vector<NodeId *>> m_nodeChildren;
        std::vector<uint32_t> m_componentFlags;
        std::vector<int> m_meshRefs;
        std::vector<std::string> m_nodeScriptPaths;
        std::vector<NodeRuntime> m_nodeRuntime;
        std::vector<NodeId *> m_freeNodeIds;

        // Mesh store
        std::vector<Mesh> m_meshes;
        std::vector<MeshRuntime> m_meshRuntimes;

        // Owned materials (from AssimpLoader or scene-level material creation)
        std::vector<std::unique_ptr<Material>> m_ownedMaterials;

        // Owned material instances (created by editor when marking a mesh as "Instanced")
        std::vector<std::unique_ptr<MaterialInstance>> m_ownedMaterialInstances;

        // Data stores
        std::vector<Vertex> m_vertexStore;
        std::vector<PositionUvVertex> m_positionUvStore;
        std::vector<AabbVertex> m_aabbVertexStore;
        std::vector<uint32_t> m_indexStore;
        std::vector<ResourceHandle<Image>> m_imageStore;
        std::vector<Sampler *> m_samplerStore;

        bool m_nodesDirty = false;
        std::vector<NodeId *> m_nodesMoved;

        uint32_t m_generation = 0;    // Incremented on NewScene/LoadSceneApply
        bool m_geometryDirty = false; // Pending geometry GPU upload
        bool m_materialDirty = false; // Pending material table update
        bool m_texturesDirty = false; // Pending image view update

        // --- Light data ---
        LightsUBO m_lightsUBO{};
        std::vector<Buffer *> m_lightUniforms;
        std::vector<Buffer *> m_lightStorageBuffers;
        std::vector<DirectionalLightEditor> m_directionalLights;
        std::vector<PointLightEditor> m_pointLights;
        std::vector<SpotLightEditor> m_spotLights;
        std::vector<AreaLightEditor> m_areaLights;
        // Reused scratch buffers for GPU upload (avoids per-frame allocations)
        std::vector<DirectionalLight> m_directionalLightsPOD;
        std::vector<PointLight> m_pointLightsPOD;
        std::vector<SpotLight> m_spotLightsPOD;
        std::vector<AreaLight> m_areaLightsPOD;
    };

    inline bool SceneNodeHandle::IsValid(const Scene &scene) const
    {
        return nodeId && nodeId->index != UINT32_MAX && generation == scene.GetGeneration();
    }

    inline bool SceneNodeHandle::IsReady(const Scene &scene) const
    {
        if (!IsValid(scene))
            return false;
        return !(scene.GetComponentFlags(nodeId) & Component_GpuPending);
    }
} // namespace pe
