#pragma once

#include "Animation/AnimationTypes.h"
#include "API/Vertex.h"
#include "Scene/LightTypes.h"
#include "Scene/NodeComponents.h"
#include "Scene/SceneNode.h"
#include "Scene/SceneNodeHandle.h"
#include "Scene/SceneScriptManifest.h"

namespace pe
{
    class AccelerationStructure;
    class Buffer;
    class Camera;
    class CommandBuffer;
    class Image;
    class ImageView;
    class Material;
    class MaterialInstance;
    class ModelAsset;
    class Sampler;
    struct SceneSerializationHelper;

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

    // --- Scene light structs (GPU data + scene metadata) ---
    struct SceneDirectionalLight : public DirectionalLight
    {
        std::string name;
        NodeId *nodeId = nullptr;
    };

    struct ScenePointLight : public PointLight
    {
        std::string name;
        NodeId *nodeId = nullptr;
    };

    struct SceneSpotLight : public SpotLight
    {
        std::string name;
        NodeId *nodeId = nullptr;
    };

    struct SceneAreaLight : public AreaLight
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
        int meshIndex = -1;    // index into scene m_meshes
        uint32_t meshSlot = 0; // position within node's meshRefs (for meshRefIndirect lookup)
        float distance;
        bool doubleSided = false;
    };

    class ParticleManager;

    class Scene
    {
        friend struct SceneSerializationHelper;

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
        void UpdateCameraRenderState();
        void UpdateGeometryBuffers();
        void UpdateRasterInstances(); // creates cmd, calls RebuildRasterInstances, submits
        void UpdateTextures();
        bool UpdateDirtyMaterials();                          // returns true when a material update must retry next frame
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
        bool SavePrefab(NodeId *root, const std::filesystem::path &file);
        SceneNodeHandle InstantiatePrefab(const std::filesystem::path &file, NodeId *parent = nullptr);

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
        bool RestoreSnapshot(const std::string &json);

        // --- Node Graph ---
        NodeId *CreateNode(const std::string &name, NodeId *parent = nullptr);
        void DeleteNode(NodeId *node);
        void ReparentNode(NodeId *node, NodeId *newParent);
        void SetLocalMatrix(NodeId *node, const mat4 &m, bool markDirty = true);
        void SetNodeEnabled(NodeId *node, bool enabled);
        bool IsNodeEnabled(const NodeId *node) const;
        bool IsNodeHierarchyEnabled(const NodeId *node) const;
        bool SubtreeHasMeshRefs(const NodeId *node) const; // node or any descendant has mesh refs
        // Cheap per-instance render visibility (a cull-flag in NodeGpuData honored by CullingCS),
        // distinct from hierarchy enable/disable: flips a flag re-uploaded via the per-frame
        // dirtyUniforms path with NO RebuildRasterInstances / TLAS rebuild. Use for frequent
        // show/hide (pooled props, LOD popping); set_enabled remains the structural path.
        void SetNodeRenderVisible(NodeId *node, bool visible);
        bool IsNodeRenderVisible(const NodeId *node) const;
        bool IsValidMeshIndex(int meshIndex) const;
        void SetMeshRef(NodeId *node, int meshIndex);    // single mesh (clears others)
        void AddMeshRef(NodeId *node, int meshIndex);    // append mesh ref
        void RemoveMeshRef(NodeId *node, int meshIndex); // remove specific mesh ref
        void SetNodeScript(NodeId *node, const std::string &path);
        void SetNodePrefabPath(NodeId *node, const std::string &path, bool markDirty = true);
        void ClearNodePrefab(NodeId *node);
        const std::string &GetNodePrefabPath(const NodeId *node) const;
        NodeRuntimeUiTag *GetRuntimeUiComponent(NodeId *node);
        const NodeRuntimeUiTag *GetRuntimeUiComponent(const NodeId *node) const;
        NodeRuntimeUiTag &GetOrCreateRuntimeUiComponent(NodeId *node);
        void ClearRuntimeUiComponent(NodeId *node);
        NodeSpriteComponent *GetSpriteComponent(NodeId *node);
        const NodeSpriteComponent *GetSpriteComponent(const NodeId *node) const;
        NodeSpriteComponent &GetOrCreateSpriteComponent(NodeId *node);
        void ClearSpriteComponent(NodeId *node);
        bool LoadSpriteMetadata(NodeId *node, std::string *outError = nullptr);
        bool SetSpriteFrame(NodeId *node, int frameIndex, int meshSlot = -1, std::string *outError = nullptr);
        bool SetSpriteFrame(NodeId *node, const std::string &frameName, int meshSlot = -1, std::string *outError = nullptr);
        bool PlaySpriteClip(NodeId *node, const std::string &clipName = {}, bool restart = true, int meshSlot = -1, std::string *outError = nullptr);
        void SetSpritePlaying(NodeId *node, bool playing);
        void StopSprite(NodeId *node);
        void UpdateSpriteAnimations(float dt);
        NodeSkinnedStrip2DComponent *GetSkinnedStrip2DState(NodeId *node);
        const NodeSkinnedStrip2DComponent *GetSkinnedStrip2DState(const NodeId *node) const;
        NodeSkinnedStrip2DComponent &GetOrCreateSkinnedStrip2DState(NodeId *node);
        void ClearSkinnedStrip2DState(NodeId *node);
        void AttachPrimitiveToNode(NodeId *node, ModelAsset *primitiveModel);
        NodeId *CreateSkyboxNode(NodeId *parent = nullptr, bool markDirty = true);
        NodeId *GetSkyboxNode() const;
        NodeSkyboxTag *GetSkyboxForNode(const NodeId *node) const;
        void SetSkyboxPath(NodeId *node, std::string path, bool markDirty = true);
        void ApplySkyboxSettingsFromNode(NodeId *node = nullptr);
        void EnsureSkyboxNodeFromSettings(bool markDirty = false);
        // --- Trigger Zone: one bounded box driving script / post-process / audio sections ---
        NodeId *CreateTriggerZoneNode(NodeId *parent = nullptr, bool markDirty = true);
        NodeTriggerZoneTag *GetTriggerZoneForNode(const NodeId *node) const;
        // Highest-priority post-process-enabled zone whose bounds contain cameraPos, blended over the
        // scene default. Returns null when none apply, so the caller falls back to the scene default.
        PostProcessProfile *ResolvePostProcessProfile(const vec3 &cameraPos);
        // Shared zone-region core: world-unit distance from p to the node's box surface (0 inside,
        // >0 outside). global -> 0 (unbounded, always inside).
        float VolumeDistanceOutside(const NodeId *node, const vec3 &p, bool global) const;
        // Per-frame enter/exit detection for the active camera; fires the zone's script section
        // onEnter/onExit on transitions (gated by runMode). Called from Update() after cameras update.
        void UpdateTriggerZones();
        // Per-frame: apply the highest-priority audio-enabled zone the camera is inside (volumes).
        void UpdateAudioZones();
        // Per-frame (during play): keep each physics-enabled zone's box registered as a Jolt sensor so a
        // physics body overlapping it fires the zone script's onEnter/onExit. Adds/removes the body, and
        // (force-field option) pushes bodies inside the sensor every frame.
        void UpdatePhysicsZones();
        // Trigger-zone section actions, fired on a camera enter (inside=true) / exit (inside=false) edge.
        void ApplyZoneSpawn(NodeId *zoneNode, NodeTriggerZoneTag &z, bool inside);
        void ApplyZoneStream(NodeTriggerZoneTag &z, bool inside);
        void ApplyZoneCamera(NodeTriggerZoneTag &z, bool inside);
        // First alive node whose name exactly matches `name`, or nullptr.
        NodeId *FindNodeByName(const std::string &name) const;
        NodeId *GetSceneSettingsNode() const;
        // Resolve the Scene Settings master switch + active post-process profile for this frame.
        // Called at the top of UpdateCameras() so cameras read the fresh profile when they jitter.
        void UpdateActiveSceneSettings();
        // Ensures the scene has exactly one Scene Settings anchor node (created if missing).
        void EnsureSceneSettingsNodeFromSettings(bool markDirty = false);
        bool SetMeshUvRect(int meshIndex, const vec4 &uvRect);
        bool SetMeshUvRectTransient(int meshIndex, const vec4 &uvRect);
        void SetNodeName(NodeId *node, const std::string &name)
        {
            ValidateNodeId(node);
            m_nodeComponentCache[node->index].name->name = name;
        }
        void UpdateNodeMatrices();
        void MarkNodeDirty(NodeId *node);
        int AddMesh(Mesh &&mesh);

        // Incremental geometry arena (Task 7, productized from Spike 0A). Lets a voxel world stream
        // meshes into the SHARED geometry buffer with no UploadBuffers rebuild, so chunks inherit the
        // existing GBuffer/indirect/frustum/Hi-Z/shadow path. Allocation is owned by the caller's
        // free list; Scene only places/registers/frees at explicit locations.
        int ReserveArenaCapacity(uint32_t vtxHeadroomBytes, uint32_t idxHeadroomBytes,
                                 uint32_t posUvHeadroomBytes, uint32_t extraDrawCapacity);
        // Place one mesh at an arena-allocated location: vertexIndex is shared by BOTH vertex streams
        // (GBuffer Vertex + depth/shadow PositionUvVertex — the per-draw vertexOffset indexes both);
        // idxByteOffset is into the index tail. If cmd != nullptr the GPU copies/barriers are RECORDED
        // into it (no stall — caller submits before the cull dispatch); if nullptr a transient cmd is
        // acquired/submitted/waited (one-shot). Returns the scene mesh slot, or -1 on failure.
        int AddArenaMesh(uint32_t vertexIndex, size_t idxByteOffset,
                         const std::vector<Vertex> &verts,
                         const std::vector<PositionUvVertex> &posUv,
                         const std::vector<uint32_t> &indices, const AABB &localBox,
                         uint32_t reuseDataOffset, const MeshRuntime &runtimeForImages,
                         CommandBuffer *cmd = nullptr);
        // Free an arena slot via swap-remove: relocates the last arena slot into `idx` (patching its
        // firstInstance, which IS the storage index the culling/GBuffer shaders read) and neuters the
        // removed mesh's index bytes (degenerates any stale two-phase-occlusion filtered draw).
        // Returns the slot that was relocated into `idx` (so the caller can fix its handle->slot map),
        // or -1 if `idx` was already the last slot (no relocation). cmd semantics as AddArenaMesh.
        int RemoveArenaMesh(int idx, CommandBuffer *cmd = nullptr);

        // Arena layout, published by ReserveArenaCapacity for the GeometryArena's free lists (vertices
        // are a shared index across both vertex streams; index space is bytes into the tail).
        uint32_t GetArenaVertexBase() const { return m_arenaVertexBase; }
        uint32_t GetArenaVertexCapacity() const { return m_arenaVertexCapacity; }
        size_t GetArenaIdxByteBase() const { return m_arenaIdxByteBase; }
        size_t GetArenaIdxCapacity() const { return m_arenaIdxCapacity; }
        uint32_t GetArenaSlotBase() const { return m_arenaSlotBase; }
        bool HasArenaVoxels() const { return m_meshCount > m_arenaSlotBase && !m_arenaSlots.empty(); }
        // Transform-storage offset of a node (its NodeGpuData / world matrix). Only valid for nodes
        // with a drawable mesh ref (others are SIZE_MAX). Arena meshes point their meshDataOffset at a
        // persistent identity host node so the VS applies an identity transform to already-world-baked
        // section vertices.
        size_t GetNodeDataOffset(const NodeId *node) const;

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

        bool IsNodeAlive(const NodeId *node) const
        {
            return node &&
                   node->index < m_nodeIds.size() &&
                   m_nodeIds[node->index] == node;
        }

        // --- Accessors ---
        const std::filesystem::path &GetScenePath() const { return m_scenePath; }
        void SetScenePath(const std::filesystem::path &path) { m_scenePath = path; }

        // Scene-owned Lua script references (on_play scripts + named actions). Persisted as
        // the top-level "scene_scripts" object; ScriptSystem reads this to run on_play scripts
        // at play and to dispatch scene.run_action(id).
        const SceneScriptManifest &GetScriptManifest() const { return m_scriptManifest; }
        SceneScriptManifest &GetScriptManifest() { return m_scriptManifest; }
        std::string GetSceneName() const;
        bool IsDirty() const { return m_dirty; }
        void MarkDirty() { m_dirty = true; }
        void ClearDirty() { m_dirty = false; }

        ParticleManager *GetParticleManager() { return m_particleManager; }
        Camera *GetActiveCamera() const { return m_cameras.at(0); }
        const Skeleton &GetSkeleton() const;
        const std::vector<AnimationClip> &GetAnimationClips() const;
        ModelAsset *FindSkeletonModel() const;
        void ResetSkeletonCache() const;
        const Skeleton &GetSkeletonForNode(const NodeId *node) const;
        const std::vector<AnimationClip> &GetAnimationClipsForNode(const NodeId *node) const;
        int GetJointCountForNode(const NodeId *node) const;
        int GetMaxJointCount() const;
        bool NodeHasSkinnedMesh(const NodeId *node) const;
        bool NodeUsesSkinnedStrip2D(const NodeId *node) const;
        Camera *GetCamera(int index) const { return m_cameras.at(index); }
        const std::vector<Camera *> &GetCameras() const { return m_cameras; }
        Camera *GetCameraForNode(const NodeId *node) const;

        void AddComponentFlag(NodeId *node, uint32_t flag);
        void RemoveComponentFlag(NodeId *node, uint32_t flag);
        OrderedMap<size_t, ModelAsset *> &GetModels() { return m_models; }
        const OrderedMap<size_t, ModelAsset *> &GetModels() const { return m_models; }

        // Returns the ModelAsset that provided this node's meshes, or nullptr.
        ModelAsset *GetModelForNode(const NodeId *node) const;

        uint64_t GetGeometryVersion() const { return m_geometryVersion; }

        uint32_t GetGeneration() const { return m_generation; }
        SceneNodeHandle MakeHandle(NodeId *node) const
        {
            return SceneNodeHandle(node, m_generation, node ? node->revision : 0);
        }
        bool IsGeometryDirty() const { return m_geometryDirty; }
        void SetGeometryDirty() { m_geometryDirty = true; }
        bool HasDirtyCameras() const;
        bool HasPendingRenderUpdate() const;
        void SetMaterialDirty() { m_materialDirty = true; }
        void SetTexturesDirty() { m_texturesDirty = true; }
        void FlushPendingGpuWork();

        Buffer *GetUniforms(uint32_t frame);
        void UploadDynamicUniforms(CommandBuffer *cmd);

        void DispatchCulling(CommandBuffer *cmd, PassInfo *passInfo, PassInfo *sortPassInfo,
                             Image *hiZPyramid = nullptr, Buffer *occlusionData = nullptr);

        // Two-phase temporal Hi-Z occlusion cull (opaque-only). Phase1: emit last-frame-visible
        // opaque draws -> set A. Phase2: test all vs this-frame Hi-Z, rewrite m_visibility, emit
        // newly-disoccluded opaque -> set B. Leaves the frustum DispatchCulling outputs untouched.
        enum class CullPhase
        {
            Phase1,
            Phase2
        };
        void DispatchCullingPhase(CommandBuffer *cmd, PassInfo *passInfo, CullPhase phase,
                                  Image *hiZPyramid = nullptr, Buffer *occlusionData = nullptr);
        Buffer *GetBuffer() { return m_buffer; }
        Buffer *GetLightUniform(uint32_t frame) { return m_lightUniforms[frame]; }
        Buffer *GetLightStorage(uint32_t frame) { return m_lightStorageBuffers[frame]; }
        std::vector<SceneDirectionalLight> &GetDirectionalLights() { return m_directionalLights; }
        std::vector<ScenePointLight> &GetPointLights() { return m_pointLights; }
        std::vector<SceneSpotLight> &GetSpotLights() { return m_spotLights; }
        std::vector<SceneAreaLight> &GetAreaLights() { return m_areaLights; }
        AccelerationStructure *GetTLAS() { return m_tlas; }
        Buffer *GetInstanceBuffer() { return m_instanceBuffer; }
        Buffer *GetMeshInfoBuffer() { return m_meshInfoBuffer; }
        size_t GetVerticesOffset() const { return m_verticesOffset; }
        size_t GetPositionsOffset() const { return m_positionsOffset; }
        size_t GetAabbVerticesOffset() const { return m_aabbVerticesOffset; }
        size_t GetAabbIndicesOffset() const { return m_aabbIndicesOffset; }
        Buffer *GetIndirectAll() const { return m_indirectAll; }
        Buffer *GetCullingCountersBuffer(uint32_t frame) const { return m_cullingCountersBuffers[frame]; }
        Buffer *GetIndirectOpaqueSS(uint32_t frame) const { return m_indirectOpaqueSS[frame]; }
        Buffer *GetIndirectAlphaCutSS(uint32_t frame) const { return m_indirectAlphaCutSS[frame]; }
        Buffer *GetIndirectOpaqueDS(uint32_t frame) const { return m_indirectOpaqueDS[frame]; }
        Buffer *GetIndirectAlphaCutDS(uint32_t frame) const { return m_indirectAlphaCutDS[frame]; }
        Buffer *GetIndirectAlphaBlend(uint32_t frame) const { return m_indirectAlphaBlend[frame]; }
        Buffer *GetIndirectTransmission(uint32_t frame) const { return m_indirectTransmission[frame]; }
        Buffer *GetIndirectSelected(uint32_t frame) const { return m_indirectSelected[frame]; }
        // Two-phase Hi-Z occlusion sets (opaque-only). A = last-frame-visible (phase 1),
        // B = newly-disoccluded (phase 2). Consumed by DepthPass/DepthLatePass/GBuffer only when
        // occlusion_culling is on; the frustum getters above stay for shadows/transparents/perception.
        Buffer *GetOccCountersA(uint32_t frame) const { return m_occCountersA[frame]; }
        Buffer *GetOccCountersB(uint32_t frame) const { return m_occCountersB[frame]; }
        Buffer *GetOccOpaqueSSA(uint32_t frame) const { return m_occOpaqueSSA[frame]; }
        Buffer *GetOccAlphaCutSSA(uint32_t frame) const { return m_occAlphaCutSSA[frame]; }
        Buffer *GetOccOpaqueDSA(uint32_t frame) const { return m_occOpaqueDSA[frame]; }
        Buffer *GetOccAlphaCutDSA(uint32_t frame) const { return m_occAlphaCutDSA[frame]; }
        Buffer *GetOccOpaqueSSB(uint32_t frame) const { return m_occOpaqueSSB[frame]; }
        Buffer *GetOccAlphaCutSSB(uint32_t frame) const { return m_occAlphaCutSSB[frame]; }
        Buffer *GetOccOpaqueDSB(uint32_t frame) const { return m_occOpaqueDSB[frame]; }
        Buffer *GetOccAlphaCutDSB(uint32_t frame) const { return m_occAlphaCutDSB[frame]; }
        Buffer *GetVisibilityBuffer() const { return m_visibility; }
        bool HasTransparentMeshes() const { return m_hasTransparentMeshes; }
        bool HasAlphaBlendMeshes() const { return m_hasAlphaBlendMeshes; }
        bool HasTransmissionMeshes() const { return m_hasTransmissionMeshes; }
        bool HasLinesMeshes() const { return m_hasLinesMeshes; }

        const std::vector<ImageView *> &GetImageViews() const { return m_imageViews; }
        void SetVoxelAtlasView(ImageView *v) { m_voxelAtlasView = v; }
        ImageView *GetVoxelAtlasView() const { return m_voxelAtlasView; }
        uint32_t GetMeshCount() const { return m_meshCount; }
        Buffer *GetMeshConstants();
        Buffer *GetMaterialTable() { return m_materialTable; }
        Buffer *GetMaterialByteBuffer() { return m_materialByteBuffer; }
        Sampler *GetDefaultSampler() const;
        static const std::vector<uint32_t> &GetAabbIndices() { return s_aabbIndices; }

        Entity *GetNodeEntity(const NodeId *node) const
        {
            ValidateNodeId(node);
            return node->entity;
        }

        template <class T>
        T *GetNodeComponent(const NodeId *node)
        {
            ValidateNodeId(node);
            return node->entity ? node->entity->GetComponent<T>() : nullptr;
        }

        const NodeComponentCache &GetNodeCache(const NodeId *node) const
        {
            ValidateNodeId(node);
            return m_nodeComponentCache[node->index];
        }

        // Node accessors
        const std::string &GetNodeName(const NodeId *node) const
        {
            ValidateNodeId(node);
            return m_nodeComponentCache[node->index].name->name;
        }
        const mat4 &GetLocalMatrix(const NodeId *node) const { return m_nodeComponentCache[node->index].transform->localMatrix; }
        const mat4 &GetWorldMatrix(const NodeId *node) const { return m_nodeRuntime[node->index].gpuData.worldMatrix; }
        const AABB &GetWorldAABB(const NodeId *node) const { return m_nodeRuntime[node->index].worldAABB; }
        NodeId *GetParent(const NodeId *node) const { return m_nodeComponentCache[node->index].hierarchy->parent; }
        const std::vector<NodeId *> &GetChildren(const NodeId *node) const { return m_nodeComponentCache[node->index].hierarchy->children; }
        uint32_t GetComponentFlags(const NodeId *node) const
        {
            const uint32_t idx = node->index;
            const auto &c = m_nodeComponentCache[idx];
            uint32_t flags = 0;
            if (!c.meshRefs->meshRefs.empty())
                flags |= Component_Mesh;
            if (c.camera)
                flags |= Component_Camera;
            if (c.light)
                flags |= Component_Light;
            if (c.physics)
                flags |= Component_Physics;
            if (c.physics2d)
                flags |= Component_Physics2D;
            if (!c.script->path.empty())
                flags |= Component_Script;
            if (c.audio)
                flags |= Component_Audio;
            if (c.skybox)
                flags |= Component_Skybox;
            if (c.sceneSettings)
                flags |= Component_SceneSettings;
            if (c.triggerZone)
                flags |= Component_TriggerZone;
            if (c.runtimeUi)
                flags |= Component_RuntimeUi;
            if (c.prefab)
                flags |= Component_Prefab;
            if (c.sprite)
                flags |= Component_Sprite;
            if (m_nodeRuntime[idx].gpuPending)
                flags |= Component_GpuPending;
            return flags;
        }
        int GetMeshRef(const NodeId *node) const
        {
            const auto &refs = m_nodeComponentCache[node->index].meshRefs->meshRefs;
            return refs.empty() ? -1 : refs[0];
        }
        const std::vector<int> &GetMeshRefs(const NodeId *node) const
        {
            return m_nodeComponentCache[node->index].meshRefs->meshRefs;
        }
        const std::string &GetNodeScriptPath(const NodeId *node) const { return m_nodeComponentCache[node->index].script->path; }
        NodeRuntime &GetNodeRuntime(const NodeId *node) { return m_nodeRuntime[node->index]; }
        const NodeRuntime &GetNodeRuntime(const NodeId *node) const { return m_nodeRuntime[node->index]; }
        uint32_t GetNodeCount() const { return static_cast<uint32_t>(m_nodeIds.size()); }
        NodeId *GetNodeId(uint32_t index) { return m_nodeIds[index]; }
        const NodeId *GetNodeId(uint32_t index) const { return m_nodeIds[index]; }
        std::vector<int> BuildDrawIndexToNodeIndex() const;

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
            vec4 primitiveParams = vec4(0.f);
            uint32_t primitiveParamCount = 0;
            size_t modelId = 0;
        };

        struct MeshSourceInfo
        {
            int sourceIndex = -1;
            int sourceMeshIndex = -1;
        };

        struct PrimitiveGeometryCacheEntry
        {
            int meshIndex = -1;
        };

        struct RtInstanceRecord
        {
            int meshIndex = -1;
            uint32_t constantsIndex = 0;
            uint32_t nodeIndex = 0;
            uint32_t meshSlot = 0;
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
        void UpdateCameras(bool markDocumentDirty = true);
        void UpdateLights();
        NodeId *CreateLightNode(const std::string &name, const mat4 &localMatrix, NodeId *parent);
        void UpdateGeometry();
        void UpdateUniformData();
        bool ApplyMeshUvRect(int meshIndex, const vec4 &uvRect, bool markGeometryDirty, bool uploadGpu);

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
        void RebuildRasterInstances(CommandBuffer *cmd); // rebuild instance data without geometry buffer

        // Ray tracing (SceneRayTracing.cpp)
        void BuildAccelerationStructures(CommandBuffer *cmd); // full BLAS+TLAS rebuild
        void BuildAllBLASes(CommandBuffer *cmd);              // only BLAS build, populates m_blasByMesh
        void BuildTLASFromInstances(CommandBuffer *cmd);      // only TLAS build, uses m_blasByMesh
        void RebuildTLASOnly();                               // creates cmd, rebuilds TLAS, submits
        void RetireTLASUpdateInstanceBuffers();
        size_t RTInstanceDescSize() const;
        Buffer *CreateRTInstanceBuffer(const std::string &name) const;
        bool WriteRTInstances(Buffer *buffer);

        // Node graph internals (SceneNode.cpp)
        void SwapAndPopNode(uint32_t index);
        void UpdateNodeMatrix(NodeId *node);
        void DestroyAllNodeEntities();
        void RetireAllNodeIds();

        // Model geometry
        std::vector<int> AddModelGeometry(ModelAsset *model, int sourceIndex);
        SceneNodeHandle AddPrimitiveDeferred(ModelAsset *model);

        // --- Private variables ---
        PerFrameData m_frameData{};
        std::vector<Camera *> m_cameras;
        OrderedMap<size_t, ModelAsset *> m_models;

        ParticleManager *m_particleManager = nullptr;

        // GPU buffers
        Buffer *m_buffer = nullptr;
        std::vector<Buffer *> m_storages;
        std::vector<Buffer *> m_storagesDevice;
        std::vector<Buffer *> m_cullingCountersBuffers;
        std::vector<Buffer *> m_indirectOpaqueSS;
        std::vector<Buffer *> m_indirectAlphaCutSS;
        std::vector<Buffer *> m_indirectOpaqueDS;
        std::vector<Buffer *> m_indirectAlphaCutDS;
        std::vector<Buffer *> m_indirectAlphaBlend;
        std::vector<Buffer *> m_indirectTransmission;
        std::vector<Buffer *> m_indirectSelected;
        std::vector<Buffer *> m_sortKeysAlphaBlend;
        std::vector<Buffer *> m_sortKeysTransmission;
        Buffer *m_indirectAll = nullptr;

        // Two-phase Hi-Z occlusion (opaque-only). Per-frame A/B indirect sets + per-set counters
        // (7-slot layout, only opaque slots 0/1/5/6 used); m_visibility is one persistent buffer
        // (uint per draw, 1 = visible last frame) seeded to 1 on every draw-index rebuild.
        std::vector<Buffer *> m_occCountersA;
        std::vector<Buffer *> m_occCountersB;
        std::vector<Buffer *> m_occOpaqueSSA;
        std::vector<Buffer *> m_occAlphaCutSSA;
        std::vector<Buffer *> m_occOpaqueDSA;
        std::vector<Buffer *> m_occAlphaCutDSA;
        std::vector<Buffer *> m_occOpaqueSSB;
        std::vector<Buffer *> m_occAlphaCutSSB;
        std::vector<Buffer *> m_occOpaqueDSB;
        std::vector<Buffer *> m_occAlphaCutDSB;
        Buffer *m_visibility = nullptr;

        size_t m_verticesOffset = 0;
        size_t m_positionsOffset = 0;
        size_t m_aabbVerticesOffset = 0;
        size_t m_aabbIndicesOffset = 0;
        uint32_t m_meshCount = 0;
        uint32_t m_indirectCapacity = 1;
        bool m_hasTransparentMeshes = false;
        bool m_hasAlphaBlendMeshes = false;
        bool m_hasTransmissionMeshes = false;
        bool m_hasLinesMeshes = false;
        uint32_t m_alphaBlendMeshCount = 0;
        uint32_t m_transmissionMeshCount = 0;
        uint32_t m_indicesCount = 0;
        uint32_t m_verticesCount = 0;
        uint32_t m_positionsCount = 0;
        uint32_t m_aabbVerticesCount = 0;

        // Arena bookkeeping (Spike 0A). Vertices use a SHARED index across the Vertex and
        // PositionUvVertex streams (the per-draw vertexOffset must index both identically — the
        // GBuffer reads Vertex, the depth prepass reads PositionUvVertex). Indices live in a tail.
        uint32_t m_arenaVertexBase = 0;                                            // first arena vertex index (== orig m_verticesCount)
        uint32_t m_arenaVertexUsed = 0;                                            // live arena vertices (informational; allocation owned by GeometryArena)
        uint32_t m_arenaVertexCapacity = 0;                                        // headroom in vertices
        size_t m_arenaIdxByteBase = 0, m_arenaIdxUsed = 0, m_arenaIdxCapacity = 0; // index tail (bytes); m_arenaIdxUsed is informational
        // Per-arena-slot CPU shadow, dense and parallel to scene slots [m_arenaSlotBase, m_meshCount).
        // Holds exactly what swap-remove needs without a GPU readback: the draw fields to rebuild a
        // relocated entry, and the index byte-range to neuter the freed geometry.
        struct ArenaSlot
        {
            uint32_t indexCount = 0;
            uint32_t firstIndex = 0;
            int32_t vertexOffset = 0;
            size_t idxByteOffset = 0;
            size_t idxBytes = 0;
            uint32_t vertexCount = 0;
        };
        std::vector<ArenaSlot> m_arenaSlots;
        uint32_t m_arenaSlotBase = 0; // first arena scene-slot (== m_meshCount when capacity reserved)

        std::vector<ImageView *> m_imageViews;
        ImageView *m_voxelAtlasView = nullptr;
        uint64_t m_geometryVersion = 0;

        // Ray tracing
        std::vector<AccelerationStructure *> m_blases;
        std::unordered_map<int, AccelerationStructure *> m_blasByMesh; // keyed by mesh index, persistent across TLAS-only rebuilds
        AccelerationStructure *m_tlas = nullptr;
        Buffer *m_instanceBuffer = nullptr;
        std::vector<Buffer *> m_tlasUpdateInstanceBuffers;
        Buffer *m_blasMergedBuffer = nullptr;
        Buffer *m_scratchBuffer = nullptr;
        Buffer *m_meshInfoBuffer = nullptr;
        uint32_t m_rtInstanceCount = 0;
        std::vector<RtInstanceRecord> m_rtInstances;
        Buffer *m_meshConstants = nullptr;
        Buffer *m_meshConstantsDevice = nullptr; // DX12: GPU-cached DEFAULT mirror (see GetMeshConstants)
        Buffer *m_materialTable = nullptr;
        Buffer *m_materialByteBuffer = nullptr; // ByteAddressBuffer for shader-driven materials
        uint32_t m_materialByteBufferUsed = 0;  // current byte offset (append-only)
        Sampler *m_defaultSampler = nullptr;

        static std::vector<uint32_t> s_aabbIndices;

        // Scene source tracking
        std::vector<SceneSource> m_sources;
        std::vector<MeshSourceInfo> m_meshSourceInfos;
        std::unordered_map<size_t, std::vector<NodeId *>> m_modelRootNodes;
        std::unordered_map<std::string, PrimitiveGeometryCacheEntry> m_primitiveGeometryCache;

        bool m_autoplayAnimations = true;
        mutable ModelAsset *m_skeletonModel = nullptr;
        mutable int m_maxJointCount = -1;

        std::filesystem::path m_scenePath;
        bool m_dirty = false;
        SceneScriptManifest m_scriptManifest;

        // Node Graph Storage
        std::vector<NodeComponentCache> m_nodeComponentCache;
        std::vector<NodeId *> m_nodeIds;
        std::vector<NodeRuntime> m_nodeRuntime;
        std::vector<NodeId *> m_freeNodeIds;
        std::vector<NodeId *> m_retiredNodeIds;

        // Indexed access helpers
        int MeshRefAt(uint32_t index) const
        {
            const auto &refs = m_nodeComponentCache[index].meshRefs->meshRefs;
            return refs.empty() ? -1 : refs[0];
        }

        bool IsGpuPending(uint32_t index) const { return m_nodeRuntime[index].gpuPending; }

        // Mesh store
        std::vector<Mesh> m_meshes;
        std::vector<MeshRuntime> m_meshRuntimes;

        // Owned materials (from imported models or scene-level material creation)
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

        // Scratch for ResolvePostProcessProfile() when volumes are blended: holds the composited
        // profile so the renderer's active-profile pointer can target it for the frame.
        PostProcessProfile m_resolvedPostProcessProfile{};

        uint32_t m_generation = 0;     // Incremented on full scene identity changes
        bool m_geometryDirty = false;  // Pending full geometry GPU upload (new mesh data)
        bool m_instancesDirty = false; // Pending raster instance data rebuild (mesh refs changed, no new geometry)
        bool m_materialDirty = false;  // Pending material table update
        bool m_texturesDirty = false;  // Pending image view update

        // RT dirty flags (independent of raster geometry)
        bool m_blasDirty = false; // BLAS rebuild needed (geometry buffer was recreated)
        bool m_tlasDirty = false; // TLAS rebuild needed (instance set changed)

        // --- Light data ---
        LightsUBO m_lightsUBO{};
        std::vector<Buffer *> m_lightUniforms;
        std::vector<Buffer *> m_lightStorageBuffers;
        std::vector<SceneDirectionalLight> m_directionalLights;
        std::vector<ScenePointLight> m_pointLights;
        std::vector<SceneSpotLight> m_spotLights;
        std::vector<SceneAreaLight> m_areaLights;
        // Reused scratch buffers for GPU upload (avoids per-frame allocations)
        std::vector<DirectionalLight> m_directionalLightsPOD;
        std::vector<PointLight> m_pointLightsPOD;
        std::vector<SpotLight> m_spotLightsPOD;
        std::vector<AreaLight> m_areaLightsPOD;
    };

    inline bool SceneNodeHandle::IsValid(const Scene &scene) const
    {
        if (!nodeId || generation != scene.GetGeneration() || nodeRevision != nodeId->revision)
            return false;
        if (nodeId->index == UINT32_MAX || nodeId->index >= scene.GetNodeCount())
            return false;
        return scene.GetNodeId(nodeId->index) == nodeId;
    }

    inline bool SceneNodeHandle::IsReady(const Scene &scene) const
    {
        if (!IsValid(scene))
            return false;
        return !(scene.GetComponentFlags(nodeId) & Component_GpuPending);
    }

    // --- Scene spatial digest (perception for agent / Lua authoring) ---
    // One mesh-bearing node's world footprint. `id` is the stable
    // "node:<index>:<revision>" form ResolveNode() accepts, so a digest entry
    // round-trips into frame_node / set_camera / get_node_info.
    struct SceneDigestNode
    {
        std::string id;
        std::string name;
        AABB aabb{};
        bool enabled = true;        // hierarchy-enabled (disabled pool members stay listed)
        bool visible = true;        // per-instance render-visible flag
        bool inFrustum = true;      // vs active camera (true when no camera exists)
        bool groundOutlier = false; // far vertical outlier (e.g. authored-parked pool); excluded from bounds
        int parentIndex = -1;       // immediate parent node index (-1 = scene root); siblings = parts of one object
        std::string parentName;     // immediate parent's name ("" at root) — lets consumers see object grouping
    };

    // Aggregate scene perception. world_bounds / ground_y / overlaps consider
    // ENABLED AND VISIBLE nodes only (so pool members parked by hierarchy-disable or
    // the render-visible cull-flag don't corrupt the play area), AND additionally
    // reject far vertical outliers via a median+MAD band (groundOutlier) — e.g. a pool
    // the author parks at Y=-1000 in the .pescene while still enabled+visible before
    // play. Overlaps also skip flat-in-Y nodes (floors / decals), props resting on a
    // larger footprint, and siblings of one composite object (nodes sharing an immediate
    // parent — a creature's Body/Head/Legs, a tree's Trunk/Canopy). All mesh nodes are
    // still listed; outliers just carry groundOutlier=true.
    struct SceneDigest
    {
        bool hasBounds = false;
        AABB worldBounds{};
        float groundY = 0.0f;                      // estimate = enabled world_bounds.min.y
        uint32_t totalNodeCount = 0;               // every node in the scene
        uint32_t meshNodeCount = 0;                // == nodes.size()
        std::vector<SceneDigestNode> nodes;        // mesh-bearing nodes
        std::vector<std::pair<int, int>> overlaps; // index pairs into nodes
        bool overlapsTruncated = false;            // sweep-and-prune hit the max-pair cap (pathological scene)
    };

    SceneDigest ComputeSceneDigest(Scene &scene);
} // namespace pe
