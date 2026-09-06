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
    struct Mesh_Constants;
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
        void AttachDefaultCameraScript(NodeId *camNode);
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
        // Player/Lua path: attach Component_Sprite to an existing quad (or create node+quad)
        // from a .sprite.json metadata path. Does not depend on Editor SpriteAuthoring.
        bool SetupSpriteFromMetadata(NodeId *node, const std::string &metadataPath, int meshSlot = 0,
                                     std::string *outError = nullptr);
        bool EnsureSpriteOutlineMesh(NodeId *node, NodeSpriteComponent &sprite,
                                     std::string *outError = nullptr);
        NodeId *CreateSpriteNode(const std::string &name, NodeId *parent, const std::string &metadataPath,
                                 float quadWidth = 1.0f, float quadHeight = 1.0f, std::string *outError = nullptr);
        bool SetSpriteFrame(NodeId *node, int frameIndex, int meshSlot = -1, std::string *outError = nullptr);
        bool SetSpriteFrame(NodeId *node, const std::string &frameName, int meshSlot = -1, std::string *outError = nullptr);
        bool PlaySpriteClip(NodeId *node, const std::string &clipName = {}, bool restart = true, int meshSlot = -1, std::string *outError = nullptr);
        void SetSpritePlaying(NodeId *node, bool playing);
        void SetSpriteInterpolation(NodeId *node, bool interpolate);
        bool SetSpriteOutlineFrame(NodeId *node, int frameIndex, bool transientGpuUpdate,
                                   std::string *outError = nullptr);
        bool SetSpriteOutlineColor(NodeId *node, const vec4 &color);
        void StopSprite(NodeId *node);
        void UpdateSpriteAnimations(float dt);
        NodeSkinnedStrip2DComponent *GetSkinnedStrip2DState(NodeId *node);
        const NodeSkinnedStrip2DComponent *GetSkinnedStrip2DState(const NodeId *node) const;
        NodeSkinnedStrip2DComponent &GetOrCreateSkinnedStrip2DState(NodeId *node);
        void ClearSkinnedStrip2DState(NodeId *node);
        // shareGeometry=false gives the node private vertices (sprites rewrite their quad UVs in place).
        void AttachPrimitiveToNode(NodeId *node, ModelAsset *primitiveModel, bool shareGeometry = true);
        NodeId *CreateSkyboxNode(NodeId *parent = nullptr, bool markDirty = true);
        NodeId *GetSkyboxNode() const;
        NodeSkyboxTag *GetSkyboxForNode(const NodeId *node) const;
        void SetSkyboxPath(NodeId *node, std::string path, bool markDirty = true);
        void ApplySkyboxSettingsFromNode(NodeId *node = nullptr);
        void EnsureSkyboxNodeFromSettings(bool markDirty = false);
        // --- Trigger Zone: one bounded box driving script / post-process / audio sections ---
        NodeId *CreateTriggerZoneNode(NodeId *parent = nullptr, bool markDirty = true);
        NodeTriggerZoneTag *GetTriggerZoneForNode(const NodeId *node) const;
        NodeId *CreateVoxelWorldNode(NodeId *parent = nullptr, bool markDirty = true);
        NodeId *GetVoxelWorldNode() const;
        NodeVoxelWorldTag *GetVoxelWorldForNode(const NodeId *node) const;
        // --- Terrain: one singleton heightfield node driving TerrainSystem ---
        NodeId *CreateTerrainNode(NodeId *parent = nullptr, bool markDirty = true);
        NodeId *GetTerrainNode() const;
        NodeTerrainTag *GetTerrainForNode(const NodeId *node) const;
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
        bool SetMeshSpriteFrameBlendTransient(int meshIndex, const vec4 &nextUvRect, float blend);
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
        // Grow the dedicated voxel buffers, preserving live geometry. Records the live-geometry copy into
        // `cmd` (the frame voxel cmd) and frees the old buffers fence-deferred via the deletion queue — no
        // GPU drain. Must run before the same frame's section uploads (it barriers the copy ahead of them).
        // Each buffer is recreated only if its cap actually grew. Returns true if any buffer grew; the
        // GeometryArena's free lists must be Grow()n to match.
        bool GrowArenaVoxelCapacity(CommandBuffer *cmd, uint32_t newVtxCapVertices, size_t newIdxCapBytes);
        // Place one packed voxel mesh at an arena-allocated location: vertexIndex indexes the dedicated
        // voxel vertex buffer (8 B/vert), shared by the voxel GBuffer draw and the voxel shadow draw (the
        // shadow VS unpacks position from the same packed verts — no separate position stream);
        // idxByteOffset is into the voxel index buffer. If cmd != nullptr the GPU copies/barriers are
        // RECORDED into it (no stall — caller submits before the cull dispatch); if nullptr a transient
        // cmd is acquired/submitted/waited (one-shot). Returns the scene mesh slot, or -1 on failure.
        // transparent=true tags the slot editorFlags bit3 (0x8) so the cull keeps it out of the opaque
        // voxel bucket (bit2 still set); the transparent GBuffer pass draws it via GetVoxelTransparentDraws.
        int AddArenaMesh(uint32_t vertexIndex, size_t idxByteOffset,
                         const std::vector<VoxelVertex> &verts,
                         const std::vector<uint16_t> &indices, const AABB &localBox,
                         uint32_t reuseDataOffset, const MeshRuntime &runtimeForImages,
                         bool transparent = false, CommandBuffer *cmd = nullptr);
        // Tombstone an arena slot: zero its indirect draw + visibility and neuter its index bytes so the
        // cull/GBuffer emit nothing and any stale two-phase-occlusion filtered draw degenerates. The slot
        // is NOT reused or relocated here — relocation would CPU-rewrite a slot's Mesh_Constants (incl the
        // AABB origin the voxel VS reads for world position) while in-flight frames still read it on
        // Vulkan (host-mapped, no device mirror) → garbage. The caller returns the slot via FreeArenaSlot
        // after the retire delay, once no in-flight frame can reference it. Always returns -1.
        int RemoveArenaMesh(int idx, CommandBuffer *cmd = nullptr);
        // Return a tombstoned slot to the free pool for reuse (called after the GeometryArena retire delay,
        // so the slot's Mesh_Constants are only overwritten once no in-flight frame still reads them).
        void FreeArenaSlot(int idx);
        // Emit one coarse whole-buffer barrier per arena buffer. Add/RemoveArenaMesh skip their per-section
        // barriers on the streamed (externalCmd) path; the caller flushes once per frame after all uploads.
        void FlushArenaBarriers(CommandBuffer *cmd);

        // Arena layout, published by ReserveArenaCapacity for the GeometryArena's free lists (vertices
        // are a shared index across both vertex streams; index space is bytes into the tail).
        uint32_t GetArenaVertexBase() const { return m_arenaVertexBase; }
        uint32_t GetArenaVertexCapacity() const { return m_arenaVertexCapacity; }
        size_t GetArenaIdxByteBase() const { return m_arenaIdxByteBase; }
        size_t GetArenaIdxCapacity() const { return m_arenaIdxCapacity; }
        uint32_t GetArenaSlotBase() const { return m_arenaSlotBase; }
        bool HasArenaVoxels() const { return m_meshCount > m_arenaSlotBase && !m_arenaSlots.empty(); }
        // One per live transparent (water) arena slot. firstInstance = slot (the voxel VS reads it for the
        // identity-host transform + section-origin AABB), drawn UNCULLED in the transparent GBuffer pass.
        struct VoxelTransparentDraw
        {
            uint32_t indexCount;
            uint32_t firstIndex;
            int32_t vertexOffset;
            uint32_t slot;
        };
        std::vector<VoxelTransparentDraw> GetVoxelTransparentDraws() const;
        // Transform-storage offset of a node (its NodeGpuData / world matrix). Only valid for nodes
        // with a drawable mesh ref (others are SIZE_MAX). Arena meshes point their meshDataOffset at a
        // persistent identity host node so the VS applies an identity transform to already-world-baked
        // section vertices.
        size_t GetNodeDataOffset(const NodeId *node) const;

        // --- In-place streamed-mesh updates (terrain tiles) ---
        // Indirect-draw / Mesh_Constants slot of node's meshRefs[refSlot], assigned by the last
        // geometry rebuild; UINT32_MAX if the mesh was skipped (disabled node, empty mesh, Lines).
        // Slots shuffle on every rebuild — re-query when GetGeometryVersion() changes.
        uint32_t GetMeshRefIndirectSlot(const NodeId *node, uint32_t refSlot) const;
        // Overwrite a REGULAR mesh's reserved region of the shared geometry buffer, its indirect draw
        // and its Mesh_Constants in place — no geometry rebuild; the streamed-terrain fast path (same
        // staged-copy pattern AddArenaMesh proves for the voxel buffers). The caller has already
        // rewritten the CPU stores inside the mesh's reserved ranges and updated the Mesh's live
        // indexCount / boundingBox / lod tables; this stages the first vertexCopyCount vertices (both
        // vertex streams), indexCopyCount indices and the 8 AABB corners to the GPU via `cmd` (the
        // caller's frame command buffer — queue order lands the copies before this frame's draws, and
        // frames already in flight keep reading the old bytes, so content never tears). Vulkan reads
        // Mesh_Constants host-mapped, so an in-flight cull may see a half-written struct for one
        // frame — transient cull wobble at worst, same acceptance as the arena path.
        bool UpdateStreamedMesh(NodeId *node, uint32_t refSlot, int meshIndex,
                                uint32_t vertexCopyCount, uint32_t indexCopyCount, CommandBuffer *cmd);

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
        // Bumped when a node's script path attaches/clears (or identity-affecting
        // script membership changes). ScriptSystem gates per-frame reconcile on this.
        uint32_t GetScriptAttachGeneration() const { return m_scriptAttachGeneration; }
        void BumpScriptAttachGeneration() { ++m_scriptAttachGeneration; }
        SceneNodeHandle MakeHandle(NodeId *node) const
        {
            return SceneNodeHandle(node, m_generation, node ? node->revision : 0);
        }
        bool IsGeometryDirty() const { return m_geometryDirty; }
        void SetGeometryDirty() { m_geometryDirty = true; }
        void SetInstancesDirty()
        {
            m_instancesDirty = true;
            m_tlasDirty = true;
        }
        bool HasDirtyCameras() const;
        bool HasPendingRenderUpdate() const;
        void SetMaterialDirty() { m_materialDirty = true; }
        void SetTexturesDirty() { m_texturesDirty = true; }
        void FlushPendingGpuWork();
        // GPU side of an instance-only rebuild (indirect commands, visibility seed, DX12 mesh-constants
        // mirror), recorded at the front of the frame command buffer instead of a Submit+Wait.
        void RecordPendingInstanceUploads(CommandBuffer *cmd);
        void RecordPendingUvUploads(CommandBuffer *cmd);
        bool TryBindCachedTexture(int meshIndex, int textureSlot, const ResourceHandle<Image> &image);
        void RecordPendingTextureUploads(CommandBuffer *cmd);

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
        // Per-cascade light-frustum cull for ShadowPass: compacts m_indirectAll into shadow-only
        // regular + voxel indirect buffers (not the camera frustum / Hi-Z buckets).
        void DispatchShadowCull(CommandBuffer *cmd, PassInfo *passInfo, const vec4 frustumPlanes[6]);
        // Refill this frame's LOD params UBO from SceneSettings and return it (bound at CullingCS binding 16).
        Buffer *UpdateLodUniforms(uint32_t frame);
        Buffer *GetBuffer() { return m_buffer; }
        // Voxel arena geometry lives in dedicated voxel-owned buffers (NOT the shared m_buffer), so
        // it can be packed/grown independently. The packed 8 B Vertex buffer feeds BOTH the voxel
        // GBuffer draw and the voxel shadow draw (each VS unpacks what it needs); Index is shared.
        // vertexOffset/firstIndex in the arena's indirect commands are base-0 relative to these buffers.
        Buffer *GetVoxelVertexBuffer() { return m_voxelVertexBuf; }
        Buffer *GetVoxelIndexBuffer() { return m_voxelIndexBuf; }
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
        void BindDrawIdBuffer(CommandBuffer *cmd) const;
        Buffer *GetCullingCountersBuffer(uint32_t frame) const { return m_cullingCountersBuffers[frame]; }
        Buffer *GetIndirectOpaqueSS(uint32_t frame) const { return m_indirectOpaqueSS[frame]; }
        Buffer *GetIndirectAlphaCutSS(uint32_t frame) const { return m_indirectAlphaCutSS[frame]; }
        Buffer *GetIndirectOpaqueDS(uint32_t frame) const { return m_indirectOpaqueDS[frame]; }
        Buffer *GetIndirectAlphaCutDS(uint32_t frame) const { return m_indirectAlphaCutDS[frame]; }
        Buffer *GetIndirectAlphaBlend(uint32_t frame) const { return m_indirectAlphaBlend[frame]; }
        Buffer *GetIndirectTransmission(uint32_t frame) const { return m_indirectTransmission[frame]; }
        Buffer *GetIndirectSelected(uint32_t frame) const { return m_indirectSelected[frame]; }
        bool HasSelectedRenderableMeshes() const;
        // Push the per-mesh "selected" editorFlags bit into meshConstants in place, so selection
        // changes reach the GPU selected-indirect bucket without a full geometry rebuild.
        void UpdateMeshSelectionFlags();
        Buffer *GetIndirectVoxels(uint32_t frame) const { return m_indirectVoxels[frame]; }
        Buffer *GetIndirectTerrain(uint32_t frame) const { return m_indirectTerrain[frame]; }
        Buffer *GetShadowIndirectRegular(uint32_t frame) const { return m_shadowIndirectRegular[frame]; }
        Buffer *GetShadowIndirectVoxels(uint32_t frame) const { return m_shadowIndirectVoxels[frame]; }
        Buffer *GetShadowCullCounters(uint32_t frame) const { return m_shadowCullCounters[frame]; }
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
        // Dedicated terrain triplanar-splat descriptor: the splat map + 4 layer albedos, bound by
        // GbufferPass to the terrain pipeline. Set by TerrainWorld; nullptr = no terrain drawn.
        void SetTerrainSplatView(ImageView *v) { m_terrainSplatView = v; }
        ImageView *GetTerrainSplatView() const { return m_terrainSplatView; }
        void SetTerrainLayerView(int i, ImageView *v)
        {
            if (i >= 0 && i < 4)
                m_terrainLayerViews[i] = v;
        }
        ImageView *GetTerrainLayerView(int i) const { return (i >= 0 && i < 4) ? m_terrainLayerViews[i] : nullptr; }
        // Optional per-layer material maps (tangent normal + roughness); a flat 1x1 default is always
        // bound so the terrain shader can sample unconditionally.
        void SetTerrainMaterialView(int i, ImageView *v)
        {
            if (i >= 0 && i < 4)
                m_terrainMaterialViews[i] = v;
        }
        ImageView *GetTerrainMaterialView(int i) const { return (i >= 0 && i < 4) ? m_terrainMaterialViews[i] : nullptr; }
        // Metres per triplanar texture tile; GbufferPass feeds it to the terrain draw's push constant.
        void SetTerrainTexScale(float m) { m_terrainTexScale = m > 0.05f ? m : 0.05f; }
        float GetTerrainTexScale() const { return m_terrainTexScale; }
        bool HasTerrain() const { return m_terrainSplatView != nullptr && m_terrainLayerViews[0] != nullptr; }
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
            if (c.voxelWorld)
                flags |= Component_VoxelWorld;
            if (c.terrain)
                flags |= Component_Terrain;
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
        ScriptRunMode GetNodeScriptRunMode(const NodeId *node) const
        {
            const auto *s = m_nodeComponentCache[node->index].script;
            return s ? s->runMode : ScriptRunMode::Player;
        }
        void SetNodeScriptRunMode(const NodeId *node, ScriptRunMode mode)
        {
            if (auto *s = m_nodeComponentCache[node->index].script)
            {
                s->runMode = mode;
                MarkDirty();
            }
        }
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
            int sourceIndex = -1;
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
        // One mesh's Mesh_Constants, derived from the current Mesh/material/node state. Shared by the
        // full rebuild (CreateMeshConstants) and the in-place streamed update (UpdateStreamedMesh).
        Mesh_Constants ComputeMeshConstants(uint32_t nodeIndex, int meshIndex) const;
        void RebuildRasterInstances(CommandBuffer *cmd); // rebuild instance data without geometry buffer; cmd may be null (GPU work deferred)
        bool RtBuildsWanted() const;                     // RT caps present and render_mode uses ray tracing

        // Ray tracing (SceneRayTracing.cpp)
        void AliasOrDirtyBlas(int meshIndex, int sourceMeshIndex); // geometry-sharing copy reuses the source BLAS
        void BuildAccelerationStructures(CommandBuffer *cmd);      // full BLAS+TLAS rebuild
        void BuildAllBLASes(CommandBuffer *cmd);                   // only BLAS build, populates m_blasByMesh
        void BuildTLASFromInstances(CommandBuffer *cmd);           // only TLAS build, uses m_blasByMesh
        void RebuildTLASOnly();                                    // creates cmd, rebuilds TLAS, submits
        void RetireTLASUpdateInstanceBuffers();
        size_t RTInstanceDescSize() const;
        Buffer *CreateRTInstanceBuffer(const std::string &name) const;
        bool WriteRTInstances(Buffer *buffer);

        // Node graph internals (SceneNode.cpp)
        void SwapAndPopNode(uint32_t index);
        void UpdateNodeMatrix(NodeId *node);
        void DestroyAllNodeEntities();
        void RetireAllNodeIds();
        void ForgetSingletonNode(const NodeId *node);

        // Model geometry
        std::vector<int> AddModelGeometry(ModelAsset *model, int sourceIndex);
        SceneNodeHandle AddPrimitiveDeferred(ModelAsset *model);
        static bool CanSharePrimitiveGeometry(const ModelAsset *model);
        static std::string PrimitiveGeometryKey(const ModelAsset &model);

        // --- Private variables ---
        PerFrameData m_frameData{};
        std::vector<Camera *> m_cameras;
        OrderedMap<size_t, ModelAsset *> m_models;

        ParticleManager *m_particleManager = nullptr;

        // GPU buffers
        Buffer *m_buffer = nullptr;
        Buffer *m_voxelVertexBuf = nullptr; // dedicated packed voxel vertex stream (VoxelVertex, 8 B)
        Buffer *m_voxelIndexBuf = nullptr;  // dedicated voxel index buffer (uint32)
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
        std::vector<Buffer *> m_indirectVoxels;
        std::vector<Buffer *> m_indirectTerrain; // dedicated terrain pipeline bucket (cull counter 8)
        // ShadowPass per-cascade light-frustum cull (reused each cascade; counters reset per dispatch).
        std::vector<Buffer *> m_shadowIndirectRegular;
        std::vector<Buffer *> m_shadowIndirectVoxels;
        std::vector<Buffer *> m_shadowCullCounters;
        std::vector<Buffer *> m_sortKeysAlphaBlend;
        std::vector<Buffer *> m_sortKeysTransmission;
        Buffer *m_indirectAll = nullptr;

        // LOD params UBO (one per swapchain image), bound at CullingCS binding 16. Byte-identical to the
        // cbuffer LodUBO: enabled/bias + three world-unit switch distances; refilled each frame from
        // SceneSettings in UpdateLodUniforms.
        struct LodUBOData
        {
            uint32_t enabled = 0;
            uint32_t pad0 = 0;
            float bias = 1.0f;
            float pad1 = 0.0f;
            float distances[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        };
        std::vector<Buffer *> m_lodUniforms;

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
            bool transparent = false; // alpha-blended (water): drawn by the transparent GBuffer pass
        };
        std::vector<ArenaSlot> m_arenaSlots;
        uint32_t m_arenaSlotBase = 0;           // first arena scene-slot (== m_meshCount when capacity reserved)
        std::vector<uint32_t> m_arenaFreeSlots; // tombstoned slots awaiting reuse (refilled after retire delay)

        std::vector<ImageView *> m_imageViews;
        std::vector<int> m_pendingTextureMeshUploads;
        ImageView *m_voxelAtlasView = nullptr;
        ImageView *m_terrainSplatView = nullptr;
        ImageView *m_terrainLayerViews[4] = {nullptr, nullptr, nullptr, nullptr};
        ImageView *m_terrainMaterialViews[4] = {nullptr, nullptr, nullptr, nullptr};
        float m_terrainTexScale = 3.0f;
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
        // XOR set-signature of the currently selected meshes last published to m_meshConstantsDevice.
        // UpdateMeshSelectionFlags re-copies the DX12 mirror only when this changes (~0ull = force).
        uint64_t m_meshSelectionMirrorSignature = ~0ull;
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
        // Singleton authoring nodes, kept by Add/RemoveComponentFlag + DeleteNode so the Get*Node()
        // lookups never scan the node array.
        NodeId *m_skyboxNode = nullptr;
        NodeId *m_sceneSettingsNode = nullptr;
        NodeId *m_voxelWorldNode = nullptr;
        NodeId *m_terrainNode = nullptr;

        // Instance-rebuild GPU work waiting for the next frame command buffer (RecordPendingInstanceUploads).
        std::vector<PeDrawIndexedIndirectCommand> m_pendingIndirectCommands;
        bool m_pendingIndirectUpload = false;
        bool m_pendingVisibilitySeed = false;
        bool m_pendingMeshConstantsMirror = false;

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
        uint32_t m_motionRollFrame = UINT32_MAX; // Frame the previousWorldMatrix roll last ran on

        // Scratch for ResolvePostProcessProfile() when volumes are blended: holds the composited
        // profile so the renderer's active-profile pointer can target it for the frame.
        PostProcessProfile m_resolvedPostProcessProfile{};

        uint32_t m_generation = 0;             // Incremented on full scene identity changes
        uint32_t m_scriptAttachGeneration = 0; // Node script path attach/clear membership
        bool m_geometryDirty = false;          // Pending full geometry GPU upload (new mesh data)
        bool m_instancesDirty = false;         // Pending raster instance data rebuild (mesh refs changed, no new geometry)
        bool m_materialDirty = false;          // Pending material table update
        bool m_texturesDirty = false;          // Pending image view update

        // Mesh indices whose sprite vertices changed on the CPU stores and await a batched
        // copy at the front of the next render command. This covers quad UVs and optional
        // authored silhouette vertices without a per-animation-frame Submit+Wait.
        std::vector<int> m_pendingUvUploads;

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
