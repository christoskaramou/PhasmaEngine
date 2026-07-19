#include "Scene/Scene.h"
#include <meshoptimizer.h> // meshopt_simplify for mesh LOD generation
#include "Scene/Material.h"
#include "Scene/ModelAsset.h"
#include "Scene/SceneRuntimeHooks.h"
#include "Script/ScriptRuntimeHooks.h"
#include "Camera/Camera.h"
#include "API/AccelerationStructure.h"
#include "API/Buffer.h"
#include "API/Pipeline.h"
#include "API/Descriptor.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "Camera/Camera.h"
#include "Particles/ParticleManager.h"
#ifdef PE_PHYSICS2D
#include "Systems/Physics2DSystem.h" // Physics2DBodyDesc/enums for the zone 2D physics path
#endif

namespace pe
{
    namespace
    {
        bool CanSharePrimitiveGeometry(const ModelAsset *model)
        {
            return model && model->IsPrimitive() && !model->HasSkeleton() && !model->HasAnimations() &&
                   model->GetNodeCount() == 1 && model->GetMeshInfoCount() == 1 && model->GetNodeMesh(0) == 0;
        }

        std::string PrimitiveGeometryKey(const ModelAsset &model)
        {
            std::ostringstream key;
            key << model.GetPrimitiveType() << ':' << model.GetPrimitiveParamCount();
            const vec4 &p = model.GetPrimitiveParams();
            key << std::setprecision(9) << ':' << p.x << ':' << p.y << ':' << p.z << ':' << p.w;
            return key.str();
        }
    } // namespace

    std::vector<uint32_t> Scene::s_aabbIndices = {
        0, 1, 1, 2, 2, 3, 3, 0,
        4, 5, 5, 6, 6, 7, 7, 4,
        0, 4, 1, 5, 2, 6, 3, 7};

    ModelAsset *Scene::GetModelForNode(const NodeId *node) const
    {
        if (!IsNodeAlive(node))
            return nullptr;

        const auto &refs = m_nodeComponentCache[node->index].meshRefs->meshRefs;
        if (refs.empty())
            return nullptr;

        ModelAsset *firstModel = nullptr;
        for (int meshIdx : refs)
        {
            if (meshIdx < 0 || meshIdx >= (int)m_meshSourceInfos.size())
                continue;

            int srcIdx = m_meshSourceInfos[meshIdx].sourceIndex;
            if (srcIdx < 0 || srcIdx >= (int)m_sources.size())
                continue;

            auto it = m_models.find(m_sources[srcIdx].modelId);
            if (it == m_models.end())
                continue;

            ModelAsset *model = *it;
            if (!firstModel)
                firstModel = model;
            if (model && model->HasSkeleton())
                return model;
        }

        return firstModel;
    }

    ModelAsset *Scene::FindSkeletonModel() const
    {
        if (m_skeletonModel && m_skeletonModel->HasSkeleton())
            return m_skeletonModel;

        for (auto *model : m_models)
        {
            if (model->HasSkeleton())
            {
                m_skeletonModel = model;
                return model;
            }
        }
        ResetSkeletonCache();
        return nullptr;
    }

    const Skeleton &Scene::GetSkeleton() const
    {
        static const Skeleton empty;
        ModelAsset *model = FindSkeletonModel();
        if (!model)
            return empty;

        return model->GetSkeleton();
    }

    const std::vector<AnimationClip> &Scene::GetAnimationClips() const
    {
        static const std::vector<AnimationClip> empty;
        ModelAsset *model = FindSkeletonModel();
        return (model && model->HasAnimations()) ? model->GetAnimations() : empty;
    }

    void Scene::ResetSkeletonCache() const
    {
        m_skeletonModel = nullptr;
        m_maxJointCount = -1;
    }

    const Skeleton &Scene::GetSkeletonForNode(const NodeId *node) const
    {
        static const Skeleton empty;
        ModelAsset *model = GetModelForNode(node);
        return (model && model->HasSkeleton()) ? model->GetSkeleton() : empty;
    }

    const std::vector<AnimationClip> &Scene::GetAnimationClipsForNode(const NodeId *node) const
    {
        static const std::vector<AnimationClip> empty;
        ModelAsset *model = GetModelForNode(node);
        return (model && model->HasAnimations()) ? model->GetAnimations() : empty;
    }

    int Scene::GetJointCountForNode(const NodeId *node) const
    {
        return GetSkeletonForNode(node).GetBoneCount();
    }

    int Scene::GetMaxJointCount() const
    {
        if (m_maxJointCount >= 0)
            return m_maxJointCount;

        int maxJointCount = 0;
        for (auto *model : m_models)
        {
            if (model && model->HasSkeleton())
                maxJointCount = std::max(maxJointCount, model->GetSkeleton().GetBoneCount());
        }
        m_maxJointCount = maxJointCount;
        return m_maxJointCount;
    }

    bool Scene::NodeHasSkinnedMesh(const NodeId *node) const
    {
        ValidateNodeId(node);

        const auto &refs = m_nodeComponentCache[node->index].meshRefs->meshRefs;
        for (int meshRef : refs)
        {
            if (meshRef >= 0 && meshRef < static_cast<int>(m_meshes.size()) && m_meshes[meshRef].skinned)
                return true;
        }
        return false;
    }

    std::vector<int> Scene::BuildDrawIndexToNodeIndex() const
    {
        std::vector<int> result(m_meshCount, -1);
        for (uint32_t i = 0; i < GetNodeCount(); i++)
        {
            const auto &drawSlots = m_nodeRuntime[i].meshRefIndirect;
            for (uint32_t slot = 0; slot < static_cast<uint32_t>(drawSlots.size()); slot++)
            {
                const uint32_t drawIndex = drawSlots[slot];
                if (drawIndex != std::numeric_limits<uint32_t>::max() && drawIndex < static_cast<uint32_t>(m_meshCount))
                    result[drawIndex] = static_cast<int>(i);
            }
        }
        return result;
    }

    bool Scene::NodeUsesSkinnedStrip2D(const NodeId *node) const
    {
        ModelAsset *model = GetModelForNode(node);
        return model && model->GetPrimitiveType() == "skinned_strip_2d";
    }

    Scene::Scene()
    {
        m_defaultSampler = Sampler::Create(Sampler::CreateInfoInit(), "defaultSampler");

        Camera *camera = new Camera();
        camera->SetName("Camera_" + std::to_string(ID::NextID()));
        NodeId *camNode = CreateNode(camera->GetName());
        AddComponentFlag(camNode, Component_Camera);
        m_nodeComponentCache[camNode->index].camera->camera = camera;
        camera->SetNodeId(camNode);
        m_cameras.push_back(camera);
        AttachDefaultCameraScript(camNode);
        EnsureSkyboxNodeFromSettings(false);

        uint32_t swapchainImageCount = RHII.GetSwapchainImageCount();
        m_storages.resize(swapchainImageCount, nullptr);
        m_storagesDevice.resize(swapchainImageCount, nullptr);

        m_particleManager = new ParticleManager();
        m_particleManager->Init(); // disable until it is done

        InitLightBuffers();
    }

    Sampler *Scene::GetDefaultSampler() const
    {
        return m_defaultSampler;
    }

    Buffer *Scene::GetUniforms(uint32_t frame)
    {
        return RHII.GetApi() == PE_GRAPHICS_API_DX12 ? m_storagesDevice[frame] : m_storages[frame];
    }

    Buffer *Scene::GetMeshConstants()
    {
        // DX12: read from the GPU-cached DEFAULT mirror (populated once per geometry rebuild) instead
        // of the CPU-written GPU_UPLOAD buffer — uncached GPU_UPLOAD reads dominate the cull pass.
        // Falls back to m_meshConstants on Vulkan (BAR is already cached) or before the mirror exists.
        return (RHII.GetApi() == PE_GRAPHICS_API_DX12 && m_meshConstantsDevice) ? m_meshConstantsDevice
                                                                                : m_meshConstants;
    }

    Scene::~Scene()
    {
        DestroyAllNodeEntities();

        // Free all NodeId allocations
        for (NodeId *id : m_nodeIds)
            delete id;
        m_nodeIds.clear();
        for (NodeId *id : m_freeNodeIds)
            delete id;
        m_freeNodeIds.clear();
        for (NodeId *id : m_retiredNodeIds)
            delete id;
        m_retiredNodeIds.clear();

        for (auto *model : m_models)
            delete model;
        m_models.clear();

        for (auto *camera : m_cameras)
            delete camera;
        m_cameras.clear();

        DestroyBuffers();
        DestroyLightBuffers();

        if (m_particleManager)
        {
            delete m_particleManager;
            m_particleManager = nullptr;
        }

        if (m_defaultSampler)
        {
            RHII.AddToDeletionQueue([s = m_defaultSampler]()
                                    { Sampler* sampler = s; Sampler::Destroy(sampler); });
        }

        for (auto *blas : m_blases)
            RHII.AddToDeletionQueue([blas]()
                                    { AccelerationStructure* b = blas; AccelerationStructure::Destroy(b); });
        m_blases.clear();
        m_blasByMesh.clear();

        RHII.AddToDeletionQueue([t = m_tlas]()
                                { AccelerationStructure* as = t; AccelerationStructure::Destroy(as); });
        RHII.AddToDeletionQueue([b = m_instanceBuffer]()
                                { Buffer* buf = b; Buffer::Destroy(buf); });
        RetireTLASUpdateInstanceBuffers();
        RHII.AddToDeletionQueue([b = m_blasMergedBuffer]()
                                { Buffer* buf = b; Buffer::Destroy(buf); });
        RHII.AddToDeletionQueue([b = m_scratchBuffer]()
                                { Buffer* buf = b; Buffer::Destroy(buf); });
        RHII.AddToDeletionQueue([b = m_meshInfoBuffer]()
                                { Buffer* buf = b; Buffer::Destroy(buf); });
        RHII.AddToDeletionQueue([b = m_meshConstants]()
                                { Buffer* buf = b; Buffer::Destroy(buf); });
        RHII.AddToDeletionQueue([b = m_meshConstantsDevice]()
                                { Buffer *buf = b; Buffer::Destroy(buf); });
    }

    Camera *Scene::GetCameraForNode(const NodeId *node) const
    {
        if (!node || node->index >= m_nodeComponentCache.size())
            return nullptr;
        const auto *tag = m_nodeComponentCache[node->index].camera;
        return tag ? tag->camera : nullptr;
    }

    void Scene::UpdateActiveSceneSettings()
    {
        // Resolve BEFORE cameras update their projection: Camera::UpdateProjection() reads
        // ActivePostProcessProfile().taa to decide jitter. Volume containment uses the camera's
        // pre-update position; one frame of lag there is sub-pixel and invisible.
        NodeId *settingsNode = GetSceneSettingsNode();
        SetSceneSettingsActive(settingsNode && IsNodeHierarchyEnabled(settingsNode));
        Camera *cam = GetActiveCamera();
        SetActivePostProcessProfile(cam ? ResolvePostProcessProfile(cam->GetPosition()) : nullptr);
    }

    void Scene::UpdateCameras(bool markDocumentDirty)
    {
        UpdateActiveSceneSettings();
        for (auto *camera : m_cameras)
        {
            camera->Update();

            // Sync camera transform to its scene node so gizmos and hierarchy reflect it
            NodeId *camNode = camera->GetNodeId();
            if (camNode)
            {
                const vec3 &pos = camera->GetPosition();
                const vec3 &euler = camera->GetEuler();
                mat4 localMat = glm::translate(mat4(1.f), pos) * mat4_cast(quat(euler));
                const bool transformChanged = GetLocalMatrix(camNode) != localMat;
                SetLocalMatrix(camNode, localMat, markDocumentDirty);
                if (!markDocumentDirty && transformChanged)
                    MarkNodeDirty(camNode);
            }
        }
    }

    void Scene::UpdateCameraRenderState()
    {
        {
            PE_PROFILE_SCOPE("Camera Update Late");
            UpdateCameras();
        }

        {
            PE_PROFILE_SCOPE("Update Node Matrices Late");
            UpdateNodeMatrices();
        }

        {
            PE_PROFILE_SCOPE("Update Uniforms Late");
            UpdateUniformData();
        }
    }

    void Scene::UpdateTriggerZones()
    {
        // Script section: runMode decides where On Enter/On Exit fire: Editor (while editing), Player
        // (play mode / standalone player), or Both. Node script instances exist in edit mode — the
        // chunk runs on creation, defining the functions; only init() is deferred to play. Disable a
        // zone node (uncheck it) to stop it firing.
        Camera *cam = GetActiveCamera();
        if (!cam)
            return;
        const bool playing = IsScriptPlayMode();
        const vec3 camPos = cam->GetPosition();
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_nodeIds.size()); ++i)
        {
            NodeTriggerZoneTag *z = m_nodeComponentCache[i].triggerZone;
            // Every camera-triggered section (Script / Spawn / Streaming / Camera) shares one enter/exit
            // edge; skip the zone only if none of them are on.
            if (!z || !(z->scriptEnabled || z->spawnEnabled || z->streamEnabled || z->cameraEnabled))
                continue;
            NodeId *node = m_nodeIds[i];
            const bool alive = IsNodeAlive(node);
            bool inside;
            if (!alive || !IsNodeHierarchyEnabled(node))
            {
                inside = false; // disabled/destroyed -> treat as outside so exit cleanup runs once
            }
            else
            {
                const bool runHere = z->runMode == TriggerRunMode::Both ||
                                     (z->runMode == TriggerRunMode::Player && playing) ||
                                     (z->runMode == TriggerRunMode::Editor && !playing);
                if (!runHere)
                    continue; // not the active context for this zone; leave wasInside so it re-fires on switch
                inside = z->fireForCamera && VolumeDistanceOutside(node, camPos, false) <= 0.0f;
            }
            if (inside == z->wasInside)
                continue;
            z->wasInside = inside;
            if (z->scriptEnabled && alive)
                InvokeSceneNodeScriptFunction(node, inside ? z->onEnter.c_str() : z->onExit.c_str());
            if (z->spawnEnabled)
                ApplyZoneSpawn(node, *z, inside);
            if (z->streamEnabled)
                ApplyZoneStream(*z, inside);
            if (z->cameraEnabled)
                ApplyZoneCamera(*z, inside);
        }
    }

    NodeId *Scene::FindNodeByName(const std::string &name) const
    {
        if (name.empty())
            return nullptr;
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_nodeIds.size()); ++i)
        {
            NodeId *n = m_nodeIds[i];
            if (n && IsNodeAlive(n) && GetNodeName(n) == name)
                return n;
        }
        return nullptr;
    }

    void Scene::ApplyZoneSpawn(NodeId *zoneNode, NodeTriggerZoneTag &z, bool inside)
    {
        if (inside)
        {
            if (z.spawnPrefabPath.empty() || (z.spawnedNode && IsNodeAlive(z.spawnedNode)))
                return; // nothing to spawn, or a previous instance is still live (no duplicates)
            std::filesystem::path resolved = z.spawnPrefabPath;
            if (!AssetFileExists(resolved))
                resolved = Path::ResolveAsset(z.spawnPrefabPath);
            // Spawn at the world root (not parented under the zone, whose box scale would distort it),
            // then place it at the zone's world position.
            SceneNodeHandle h = InstantiatePrefab(resolved, nullptr);
            if (h.nodeId)
            {
                const vec3 pos(GetWorldMatrix(zoneNode)[3]);
                SetLocalMatrix(h.nodeId, glm::translate(mat4(1.0f), pos));
            }
            z.spawnedNode = h.nodeId;
        }
        else
        {
            if (z.spawnDespawnOnExit && z.spawnedNode && IsNodeAlive(z.spawnedNode))
                DeleteNode(z.spawnedNode);
            z.spawnedNode = nullptr;
        }
    }

    void Scene::ApplyZoneStream(NodeTriggerZoneTag &z, bool inside)
    {
        if (NodeId *target = FindNodeByName(z.streamTargetName))
            SetNodeEnabled(target, inside);
    }

    void Scene::ApplyZoneCamera(NodeTriggerZoneTag &z, bool inside)
    {
        if (inside)
        {
            Camera *active = GetActiveCamera();
            z.cameraPrev = active;
            z.cameraPrevFovDeg = active ? glm::degrees(active->Fovx()) : 0.0f;
            if (NodeId *camNode = FindNodeByName(z.cameraTargetName))
            {
                if (Camera *target = GetCameraForNode(camNode))
                    SetActiveCamera(target);
            }
            if (z.cameraFovDeg > 0.0f)
            {
                if (Camera *now = GetActiveCamera())
                    now->SetFovx(glm::radians(z.cameraFovDeg));
            }
        }
        else
        {
            if (z.cameraPrev)
            {
                SetActiveCamera(z.cameraPrev);
                if (z.cameraPrevFovDeg > 0.0f)
                    z.cameraPrev->SetFovx(glm::radians(z.cameraPrevFovDeg));
                z.cameraPrev = nullptr;
            }
        }
    }

    void Scene::UpdateAudioZones()
    {
        // Audio section: each audio-enabled zone drives its OWN node audio source. Outside the band the
        // source is stopped; entering it plays and blends volume 0 -> blend over blend_distance world
        // units (full blend inside the box). Source pitch/loop/spatial stay exactly as authored.
        Camera *cam = GetActiveCamera();
        const vec3 camPos = cam ? cam->GetPosition() : vec3(0.0f);
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_nodeIds.size()); ++i)
        {
            NodeTriggerZoneTag *z = m_nodeComponentCache[i].triggerZone;
            if (!z || (!z->audioEnabled && z->audioSource.filePath.empty()))
                continue; // no zone, or one that has never used audio
            NodeId *node = m_nodeIds[i];
            float gain = 0.0f;
            if (z->audioEnabled && cam && IsNodeAlive(node) && IsNodeHierarchyEnabled(node))
            {
                const float distOutside = VolumeDistanceOutside(node, camPos, false);
                const float full = std::clamp(z->blend, 0.0f, 1.0f);
                if (z->blend_distance > 0.0f)
                {
                    if (distOutside < z->blend_distance)
                        gain = full * (1.0f - distOutside / z->blend_distance);
                }
                else if (distOutside <= 0.0f)
                {
                    gain = full;
                }
            }
            // gain == 0 (outside, or audio disabled) stops the zone sound.
            ApplySceneAudioZoneSource(node, z->audioSource, gain);
        }
    }

    void Scene::UpdatePhysicsZones()
    {
        // Physics section: only meaningful while simulating (Jolt bodies are live in play). Each enabled
        // zone registers one body shaped to the common `shape` — half-extents/radius = 0.5 local, which
        // CreateJoltBody multiplies by world scale, exactly matching VolumeDistanceOutside. Sensor mode is
        // a pass-through trigger that fires the Physics-section script; Solid mode is a collider. We only
        // ever add/remove the body WE created (physicsBodyActive) and never touch a node that already has
        // its own physics body.
        const bool simulating = IsScenePhysicsSimulating();
        const bool playing = IsScriptPlayMode();
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_nodeIds.size()); ++i)
        {
            NodeTriggerZoneTag *z = m_nodeComponentCache[i].triggerZone;
            if (!z)
                continue;
            NodeId *node = m_nodeIds[i];
            const bool runHere = z->runMode == TriggerRunMode::Both ||
                                 (z->runMode == TriggerRunMode::Player && playing) ||
                                 (z->runMode == TriggerRunMode::Editor && !playing);
#ifdef PE_PHYSICS2D
            const bool is2D = z->physicsEngine == ZonePhysicsEngine::Physics2D;
#else
            const bool is2D = false;
#endif
            // 3D physics is live only while the Jolt sim runs (StartSimulation); 2D (Box2D) steps whenever
            // playing, so gate it on play mode.
            const bool worldActive = is2D ? playing : simulating;
            const bool want = z->physicsEnabled && worldActive && runHere && IsNodeAlive(node) &&
                              IsNodeHierarchyEnabled(node);
            if (want && !z->physicsBodyActive)
            {
#ifdef PE_PHYSICS2D
                if (is2D)
                {
                    if (HasScenePhysics2DBody(node))
                        continue; // node already owns a 2D body; leave it to the plain physics2d path
                    // 2D applies no world scale to shapes, so pass the actual world dimensions (XY).
                    const mat4 &w = GetWorldMatrix(node);
                    const vec3 size(length(vec3(w[0])), length(vec3(w[1])), length(vec3(w[2])));
                    Physics2DBodyDesc d;
                    d.bodyType = z->physicsBodyType == PhysicsBodyType::Dynamic     ? Physics2DBodyType::Dynamic
                                 : z->physicsBodyType == PhysicsBodyType::Kinematic ? Physics2DBodyType::Kinematic
                                                                                    : Physics2DBodyType::Static;
                    d.shapeType = (z->shape == ZoneShape::Sphere) ? Physics2DShapeType::Circle : Physics2DShapeType::Box;
                    d.isSensor = z->physicsMode == ZonePhysicsMode::Sensor;
                    d.width = size.x;
                    d.height = size.y;
                    d.radius = std::max(size.x, size.y) * 0.5f;
                    d.density = z->physicsMass; // "Mass" field reused as density for 2D
                    d.friction = z->physicsFriction;
                    d.restitution = z->physicsRestitution;
                    AddScenePhysics2DBody(*this, node, d);
                    if (d.isSensor)
                        SetSceneZonePhysics2DScriptTrigger(node, z->physicsScriptPath.c_str(), z->physicsOnEnter.c_str(),
                                                           z->physicsOnExit.c_str(), z->physicsFilterTag.c_str());
                    z->physicsBodyActive = true;
                    continue;
                }
#endif
                if (HasScenePhysicsBody(node))
                    continue; // node already owns a physics body; leave it to the plain physics path
                PhysicsBodyDesc d;
                d.bodyType = z->physicsBodyType;
                d.shapeType = (z->shape == ZoneShape::Sphere) ? PhysicsShapeType::Sphere : PhysicsShapeType::Box;
                d.isTrigger = z->physicsMode == ZonePhysicsMode::Sensor;
                d.autoFitShape = false; // zones have no mesh; size from the transform, not mesh AABBs
                d.boxHalfExtents = vec3(0.5f);
                d.sphereRadius = 0.5f;
                d.mass = z->physicsMass;
                d.friction = z->physicsFriction;
                d.restitution = z->physicsRestitution;
                AddScenePhysicsBody(*this, node, d);
                if (d.isTrigger)
                    SetSceneZonePhysicsScriptTrigger(node, z->physicsScriptPath.c_str(), z->physicsOnEnter.c_str(),
                                                     z->physicsOnExit.c_str(), z->physicsFilterTag.c_str());
                z->physicsBodyActive = true;
            }
            else if (!want && z->physicsBodyActive)
            {
#ifdef PE_PHYSICS2D
                if (is2D)
                    RemoveScenePhysics2DBody(node);
                else
#endif
                    RemoveScenePhysicsBody(node);
                z->physicsBodyActive = false;
                z->physicsBodiesInside.clear();
            }

            // Force field: push every body currently overlapping the sensor by physicsForce each frame.
            // The inside-set is maintained by the sensor enter/exit callbacks (SetSceneZonePhysics*Trigger).
            if (z->physicsBodyActive && z->physicsForceField && z->physicsMode == ZonePhysicsMode::Sensor)
            {
                auto &inside = z->physicsBodiesInside;
                for (size_t k = 0; k < inside.size();)
                {
                    NodeId *b = inside[k];
                    if (!b || !IsNodeAlive(b))
                    {
                        inside[k] = inside.back();
                        inside.pop_back();
                        continue;
                    }
#ifdef PE_PHYSICS2D
                    if (is2D)
                        ApplyScenePhysics2DForce(b, z->physicsForce);
                    else
#endif
                        ApplyScenePhysicsForce(b, z->physicsForce);
                    ++k;
                }
            }
        }
    }

    void Scene::Update()
    {
        {
            PE_PROFILE_SCOPE("Camera Update");
            UpdateCameras();
        }

        UpdateTriggerZones();
        UpdateAudioZones();
        UpdatePhysicsZones();

        UpdateSpriteAnimations(std::max(0.0f, static_cast<float>(FrameTimer::Instance().GetDelta()) *
                                                  Settings::Get<SceneSettings>().time_scale));

        UpdateGeometry();
        if (Settings::Get<SceneSettings>().selection_outline)
            UpdateMeshSelectionFlags();
        UpdateLights();
        if (m_particleManager)
            m_particleManager->Update();
    }

    bool Scene::HasPendingRenderUpdate() const
    {
        const bool rtDirty = RHII.GetCaps().rayTracing && (m_blasDirty || m_tlasDirty);
        return m_nodesDirty || m_geometryDirty || m_instancesDirty || m_materialDirty || m_texturesDirty || rtDirty;
    }

    bool Scene::HasDirtyCameras() const
    {
        return std::any_of(m_cameras.begin(),
                           m_cameras.end(),
                           [](const Camera *camera)
                           { return camera && camera->IsDirty(); });
    }

    void Scene::UpdateGeometryBuffers()
    {
        CommandBuffer *cmd = RHII.GetMainQueue()->AcquireCommandBuffer();
        cmd->Begin();
        UploadBuffers(cmd);
        cmd->End();
        RHII.GetMainQueue()->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        cmd->Return();
        // m_blasDirty is set inside UploadBuffers() when rtSupport is true
    }

    void Scene::UpdateRasterInstances()
    {
        CommandBuffer *cmd = RHII.GetMainQueue()->AcquireCommandBuffer();
        cmd->Begin();
        RebuildRasterInstances(cmd);
        cmd->End();
        RHII.GetMainQueue()->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        cmd->Return();
    }

    void Scene::UpdateTextures()
    {
        Queue *queue = RHII.GetMainQueue();
        CommandBuffer *cmd = queue->AcquireCommandBuffer();
        cmd->Begin();
        UpdateImageViews();
        CreateMaterialTable();
        CreateMeshConstants(cmd);
        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        queue->ReturnCommandBuffer(cmd);
    }

    MaterialInstance *Scene::CreateMaterialInstance(Mesh &mesh)
    {
        if (!mesh.material)
            return nullptr;

        if (mesh.materialInstance)
            return mesh.materialInstance;

        auto inst = std::make_unique<MaterialInstance>(mesh.material);
        MaterialInstance *ptr = inst.get();
        mesh.materialInstance = ptr;
        m_ownedMaterialInstances.push_back(std::move(inst));
        m_texturesDirty = true;
        m_materialDirty = true;
        return ptr;
    }

    void Scene::DestroyMaterialInstance(Mesh &mesh)
    {
        if (!mesh.materialInstance)
            return;

        auto it = std::find_if(m_ownedMaterialInstances.begin(), m_ownedMaterialInstances.end(),
                               [&](const std::unique_ptr<MaterialInstance> &p)
                               { return p.get() == mesh.materialInstance; });

        if (it != m_ownedMaterialInstances.end())
        {
            // Swap-and-pop for O(1) removal
            std::swap(*it, m_ownedMaterialInstances.back());
            m_ownedMaterialInstances.pop_back();
        }

        mesh.materialInstance = nullptr;
    }

    std::vector<int> Scene::AddModelGeometry(ModelAsset *model, int sourceIndex)
    {
        const uint32_t vertexBase = static_cast<uint32_t>(m_vertexStore.size());
        const uint32_t indexBase = static_cast<uint32_t>(m_indexStore.size());
        const uint32_t posUvBase = static_cast<uint32_t>(m_positionUvStore.size());
        const size_t aabbBase = m_aabbVertexStore.size();

        // Bulk copy geometry data
        const auto &srcVerts = model->GetVertices();
        const auto &srcPosUvs = model->GetPositionUvs();
        const auto &srcAabbs = model->GetAabbVertices();
        const auto &srcIndices = model->GetIndices();

        m_vertexStore.insert(m_vertexStore.end(), srcVerts.begin(), srcVerts.end());
        m_positionUvStore.insert(m_positionUvStore.end(), srcPosUvs.begin(), srcPosUvs.end());
        m_aabbVertexStore.insert(m_aabbVertexStore.end(), srcAabbs.begin(), srcAabbs.end());
        m_indexStore.insert(m_indexStore.end(), srcIndices.begin(), srcIndices.end());

        for (const auto &img : model->GetImages())
            m_imageStore.push_back(img);
        for (auto *samp : model->GetSamplers())
            m_samplerStore.push_back(samp);

        // Create scene meshes from ModelAsset meshes
        std::vector<int> meshMap(model->GetMeshInfoCount(), -1);
        for (int i = 0; i < model->GetMeshInfoCount(); i++)
        {
            const MeshInfo *mi = model->GetMeshInfo(i);
            if (!mi)
                continue;

            Mesh mesh{};
            mesh.vertexOffset = mi->vertexOffset + vertexBase;
            mesh.vertexCount = mi->verticesCount;
            mesh.indexOffset = mi->indexOffset + indexBase;
            mesh.indexCount = mi->indicesCount;
            mesh.positionsOffset = mi->vertexOffset + posUvBase;
            mesh.aabbVertexOffset = mi->aabbVertexOffset + aabbBase;
            mesh.aabbColor = mi->aabbColor;
            mesh.boundingBox = mi->boundingBox;
            mesh.renderType = mi->renderType;
            mesh.material = mi->material;
            mesh.skinned = mi->skinned;

            // Build discrete LODs: meshopt_simplify yields reduced index sets that index the SAME
            // vertices (subset), so every level shares mesh.vertexOffset; we append each to the shared
            // index store and record its range. lods[0] is the full-detail range. Skinned meshes are
            // skipped (joint-weighted simplification would need attribute-aware collapse). Distance-based
            // level pick + index-range swap happens on the GPU in CullingCS.
            mesh.lodIndexOffset[0] = mesh.indexOffset;
            mesh.lodIndexCount[0] = mesh.indexCount;
            mesh.lodCount = 1;
            const uint32_t wantLods = std::clamp(Settings::Get<SceneSettings>().lod_count, 1u, Mesh::kMaxLods);
            const uint32_t baseIdxCount = mesh.indexCount;
            if (wantLods > 1 && baseIdxCount >= 256 && !mesh.skinned && mi->verticesCount > 0)
            {
                const uint32_t *baseIndices = srcIndices.data() + mi->indexOffset; // 0-based within this mesh
                const float *positions = reinterpret_cast<const float *>(srcVerts.data() + mi->vertexOffset);
                const size_t vtxCount = mi->verticesCount;
                static constexpr float kRatios[Mesh::kMaxLods] = {1.0f, 0.5f, 0.25f, 0.12f};
                std::vector<uint32_t> simplified(baseIdxCount);
                uint32_t prevCount = baseIdxCount;
                for (uint32_t lod = 1; lod < wantLods; ++lod)
                {
                    size_t target = (static_cast<size_t>(baseIdxCount * kRatios[lod]) / 3) * 3; // whole tris
                    if (target < 12)
                        break;
                    float err = 0.0f;
                    size_t resCount = meshopt_simplify(simplified.data(), baseIndices, baseIdxCount, positions,
                                                       vtxCount, sizeof(Vertex), target, 0.1f, 0, &err);
                    if (resCount == 0 || resCount >= static_cast<size_t>(prevCount * 0.95f))
                        break; // no meaningful reduction past this level
                    uint32_t off = static_cast<uint32_t>(m_indexStore.size());
                    m_indexStore.insert(m_indexStore.end(), simplified.begin(), simplified.begin() + resCount);
                    mesh.lodIndexOffset[lod] = off;
                    mesh.lodIndexCount[lod] = static_cast<uint32_t>(resCount);
                    mesh.lodCount = lod + 1;
                    prevCount = static_cast<uint32_t>(resCount);
                }
            }

            int sceneMeshIdx = AddMesh(std::move(mesh));
            meshMap[i] = sceneMeshIdx;

            // Track source for save/load
            if (sceneMeshIdx >= 0)
            {
                if (static_cast<int>(m_meshSourceInfos.size()) <= sceneMeshIdx)
                    m_meshSourceInfos.resize(sceneMeshIdx + 1);
                m_meshSourceInfos[sceneMeshIdx] = {sourceIndex, i};
            }
        }

        return meshMap;
    }

    void Scene::AddModel(ModelAsset *model)
    {
        m_models.insert(model->GetId(), model);
        ResetSkeletonCache();

        int sourceIndex = static_cast<int>(m_sources.size());
        SceneSource source;
        source.filePath = model->GetFilePath();
        source.primitiveType = model->GetPrimitiveType();
        source.primitiveParams = model->GetPrimitiveParams();
        source.primitiveParamCount = model->GetPrimitiveParamCount();
        source.modelId = model->GetId();
        m_sources.push_back(std::move(source));

        // Copy geometry and create meshes
        std::vector<int> meshMap = AddModelGeometry(model, sourceIndex);

        // Create nodes matching ModelAsset's node hierarchy — two-pass to handle
        // out-of-order parent indices (parent index > child index is valid in Assimp output).
        const mat4 &modelMatrix = model->GetMatrix();
        std::vector<NodeId *> nodeMap(model->GetNodeCount(), nullptr);

        // Pass 1 — create all nodes as roots with local matrices and mesh refs
        for (int i = 0; i < model->GetNodeCount(); i++)
        {
            const NodeInfo *ni = model->GetNodeInfo(i);
            if (!ni)
                continue;

            NodeId *node = CreateNode(ni->name, nullptr);
            SetLocalMatrix(node, ni->localMatrix, false);

            int meshIdx = model->GetNodeMesh(i);
            if (meshIdx >= 0 && meshIdx < static_cast<int>(meshMap.size()) && meshMap[meshIdx] >= 0)
                SetMeshRef(node, meshMap[meshIdx]);

            nodeMap[i] = node;
        }

        // Pass 2 — wire up parent–child relationships; bake model matrix into true roots
        std::vector<NodeId *> roots;
        for (int i = 0; i < model->GetNodeCount(); i++)
        {
            if (!nodeMap[i])
                continue;
            const NodeInfo *ni = model->GetNodeInfo(i);
            if (ni->parent >= 0 && ni->parent < static_cast<int>(nodeMap.size()) && nodeMap[ni->parent])
            {
                ReparentNode(nodeMap[i], nodeMap[ni->parent]);
            }
            else
            {
                // True root — bake model matrix into local transform
                SetLocalMatrix(nodeMap[i], modelMatrix * ni->localMatrix, false);
                roots.push_back(nodeMap[i]);
            }
        }
        m_modelRootNodes[model->GetId()] = std::move(roots);

        // Mark all new nodes dirty for initial transform computation
        for (NodeId *node : nodeMap)
        {
            if (node)
                MarkNodeDirty(node);
        }

        UpdateNodeMatrices();

        if (m_autoplayAnimations && model->HasAnimations())
        {
            for (NodeId *node : nodeMap)
            {
                if (node && NodeHasSkinnedMesh(node))
                    PlaySceneAnimation(*this, node, 0, true);
            }
        }
    }

    SceneNodeHandle Scene::AddPrimitiveDeferred(ModelAsset *model)
    {
        if (!model)
            return {};

        const std::string key = PrimitiveGeometryKey(*model);
        const MeshInfo *sourceMesh = model->GetMeshInfo(0);
        if (!sourceMesh)
        {
            delete model;
            return {};
        }

        int meshIndex = -1;
        int sourceIndex = -1;
        auto cacheIt = m_primitiveGeometryCache.find(key);
        if (cacheIt != m_primitiveGeometryCache.end() && IsValidMeshIndex(cacheIt->second.meshIndex) &&
            cacheIt->second.sourceIndex >= 0 && cacheIt->second.sourceIndex < static_cast<int>(m_sources.size()))
        {
            sourceIndex = cacheIt->second.sourceIndex;
            Mesh mesh = m_meshes[cacheIt->second.meshIndex];
            mesh.renderType = sourceMesh->renderType;
            mesh.material = sourceMesh->material;
            mesh.materialInstance = nullptr;
            mesh.skinned = false;
            mesh.boundingBox = sourceMesh->boundingBox;
            mesh.aabbColor = sourceMesh->aabbColor;
            meshIndex = AddMesh(std::move(mesh));
            if (RHII.GetCaps().rayTracing)
                m_blasDirty = true;
        }
        else
        {
            sourceIndex = static_cast<int>(m_sources.size());
            SceneSource source;
            source.filePath = model->GetFilePath();
            source.primitiveType = model->GetPrimitiveType();
            source.primitiveParams = model->GetPrimitiveParams();
            source.primitiveParamCount = model->GetPrimitiveParamCount();
            m_sources.push_back(std::move(source));

            std::vector<int> meshMap = AddModelGeometry(model, sourceIndex);
            if (!meshMap.empty())
                meshIndex = meshMap[0];
            if (meshIndex >= 0)
                m_primitiveGeometryCache[key] = {meshIndex, sourceIndex};
            m_geometryDirty = true;
        }

        if (meshIndex >= 0)
        {
            if (static_cast<int>(m_meshSourceInfos.size()) <= meshIndex)
                m_meshSourceInfos.resize(meshIndex + 1);
            m_meshSourceInfos[meshIndex] = {sourceIndex, 0};
        }

        const NodeInfo *nodeInfo = model->GetNodeInfo(0);
        NodeId *node = CreateNode(nodeInfo ? nodeInfo->name : model->GetLabel(), nullptr);
        if (nodeInfo)
            SetLocalMatrix(node, model->GetMatrix() * nodeInfo->localMatrix, false);
        if (meshIndex >= 0)
            SetMeshRef(node, meshIndex);
        m_nodeRuntime[node->index].gpuPending = true;
        MarkNodeDirty(node);
        UpdateNodeMatrices();

        for (auto &mat : model->GetOwnedMaterials())
            m_ownedMaterials.push_back(std::move(mat));
        delete model;

        return MakeHandle(node);
    }

    SceneNodeHandle Scene::AddModelDeferred(ModelAsset *model)
    {
        if (CanSharePrimitiveGeometry(model))
            return AddPrimitiveDeferred(model);

        m_models.insert(model->GetId(), model);
        ResetSkeletonCache();

        int sourceIndex = static_cast<int>(m_sources.size());
        SceneSource source;
        source.filePath = model->GetFilePath();
        source.primitiveType = model->GetPrimitiveType();
        source.primitiveParams = model->GetPrimitiveParams();
        source.primitiveParamCount = model->GetPrimitiveParamCount();
        source.modelId = model->GetId();
        m_sources.push_back(std::move(source));

        std::vector<int> meshMap = AddModelGeometry(model, sourceIndex);

        const mat4 &modelMatrix = model->GetMatrix();
        std::vector<NodeId *> nodeMap(model->GetNodeCount(), nullptr);

        for (int i = 0; i < model->GetNodeCount(); i++)
        {
            const NodeInfo *ni = model->GetNodeInfo(i);
            if (!ni)
                continue;
            NodeId *node = CreateNode(ni->name, nullptr);
            SetLocalMatrix(node, ni->localMatrix, false);
            int meshIdx = model->GetNodeMesh(i);
            if (meshIdx >= 0 && meshIdx < static_cast<int>(meshMap.size()) && meshMap[meshIdx] >= 0)
                SetMeshRef(node, meshMap[meshIdx]);
            m_nodeRuntime[node->index].gpuPending = true;
            nodeMap[i] = node;
        }

        std::vector<NodeId *> roots;
        for (int i = 0; i < model->GetNodeCount(); i++)
        {
            if (!nodeMap[i])
                continue;
            const NodeInfo *ni = model->GetNodeInfo(i);
            if (ni->parent >= 0 && ni->parent < static_cast<int>(nodeMap.size()) && nodeMap[ni->parent])
                ReparentNode(nodeMap[i], nodeMap[ni->parent]);
            else
            {
                SetLocalMatrix(nodeMap[i], modelMatrix * ni->localMatrix, false);
                roots.push_back(nodeMap[i]);
            }
        }
        m_modelRootNodes[model->GetId()] = std::move(roots);

        for (NodeId *node : nodeMap)
            if (node)
                MarkNodeDirty(node);
        UpdateNodeMatrices();

        m_geometryDirty = true;

        // Return handle to first root, or wrap multi-root in synthetic parent
        const auto &finalRoots = m_modelRootNodes[model->GetId()];
        if (finalRoots.size() == 1)
            return MakeHandle(finalRoots[0]);

        // Multi-root: create synthetic parent
        NodeId *synth = CreateNode(model->GetLabel().empty() ? "Model" : model->GetLabel());
        m_nodeRuntime[synth->index].gpuPending = true;
        for (NodeId *root : finalRoots)
            ReparentNode(root, synth);
        return MakeHandle(synth);
    }

    const std::vector<NodeId *> &Scene::GetModelRootNodes(ModelAsset *model) const
    {
        auto it = m_modelRootNodes.find(model->GetId());
        if (it != m_modelRootNodes.end())
            return it->second;
        return EmptyRootNodes();
    }

    const std::vector<NodeId *> &Scene::EmptyRootNodes()
    {
        static const std::vector<NodeId *> empty;
        return empty;
    }

    void Scene::RemoveModel(ModelAsset *model)
    {
        size_t modelId = model->GetId();

        auto rootIt = m_modelRootNodes.find(modelId);
        if (rootIt != m_modelRootNodes.end())
        {
            std::vector<NodeId *> roots = rootIt->second;
            for (NodeId *root : roots)
            {
                if (IsNodeAlive(root))
                    DeleteNode(root);
            }
            if (m_modelRootNodes.find(modelId) == m_modelRootNodes.end())
                return;
            m_modelRootNodes.erase(modelId);
        }

        if (m_models.erase(modelId))
        {
            ResetSkeletonCache();
            delete model;
        }
    }

    void Scene::RemoveModels(std::vector<ModelAsset *> models)
    {
        for (ModelAsset *model : models)
            RemoveModel(model);
    }

    Camera *Scene::AddCamera(NodeId *parent)
    {
        Camera *camera = new Camera();
        camera->SetName("Camera_" + std::to_string(ID::NextID()));
        NodeId *camNode = CreateNode(camera->GetName(), parent);
        AddComponentFlag(camNode, Component_Camera);
        m_nodeComponentCache[camNode->index].camera->camera = camera;
        camera->SetNodeId(camNode);
        m_cameras.push_back(camera);
        AttachDefaultCameraScript(camNode);
        return camera;
    }

    // Fresh cameras get the free-fly script for EDITOR viewport navigation only.
    // Play/player sessions must not steal WASD/Space — games own their own camera.
    void Scene::AttachDefaultCameraScript(NodeId *camNode)
    {
        if (!camNode)
            return;
        SetNodeScript(camNode, "Assets/Scripts/fly_camera_play.lua");
        if (auto *s = m_nodeComponentCache[camNode->index].script)
            s->runMode = ScriptRunMode::Editor;
    }

    void Scene::RemoveCamera(Camera *camera)
    {
        if (m_cameras.size() <= 1)
            return;

        auto it = std::find(m_cameras.begin(), m_cameras.end(), camera);
        if (it != m_cameras.end())
        {
            ClearSceneSelection();

            // DeleteNode handles camera vector cleanup (delete + erase) via Component_Camera check
            NodeId *camNode = camera->GetNodeId();
            if (camNode)
                DeleteNode(camNode);
        }
    }

    void Scene::SetActiveCamera(Camera *camera)
    {
        if (!camera)
            return;

        auto it = std::find(m_cameras.begin(), m_cameras.end(), camera);
        if (it != m_cameras.end())
        {
            // Swap to index 0 to make it active
            std::swap(m_cameras[0], *it);
        }
    }

    void Scene::UpdateUniformData()
    {
        uint32_t frame = RHII.GetFrameIndex();

        if (!m_cameras.empty())
        {
            m_frameData.viewProjection = m_cameras[0]->GetViewProjection();
            m_frameData.previousViewProjection = m_cameras[0]->GetPreviousViewProjection();
            m_frameData.invView = m_cameras[0]->GetInvView();
            m_frameData.invProjection = m_cameras[0]->GetInvProjection();
        }

        BufferRange range{};
        range.data = &m_frameData;
        range.size = sizeof(PerFrameData);
        range.offset = 0;
        m_storages[frame]->Copy(1, &range, true);

        // Batch all dirty node GPU data uploads into a single Copy call.
        // rt.gpuData lives inside m_nodeRuntime (stable address) — safe to take pointer.
        // Joint matrices are computed into a pre-reserved scratch buffer so data()
        // never moves while jointRanges pointers are being accumulated.
        static thread_local std::vector<BufferRange> nodeRanges;
        static thread_local std::vector<BufferRange> jointRanges;
        static thread_local std::vector<mat4> allJointMatrices;
        nodeRanges.clear();
        jointRanges.clear();
        allJointMatrices.clear();

        const int maxJointCount = GetMaxJointCount();
        if (maxJointCount > 0)
            allJointMatrices.reserve(static_cast<size_t>(GetNodeCount()) * static_cast<size_t>(maxJointCount));

        for (uint32_t i = 0; i < GetNodeCount(); i++)
        {
            NodeRuntime &rt = m_nodeRuntime[i];
            if (!(rt.dirtyUniforms & (1u << frame)))
                continue;

            // Skip nodes pending GPU upload — their storage offsets aren't assigned yet
            if (rt.gpuPending)
                continue;

            rt.dirtyUniforms &= ~(1u << frame);

            if (!rt.hasUniformData)
                continue;

            BufferRange r{};
            r.data = &rt.gpuData;
            r.size = sizeof(NodeGpuData);
            r.offset = rt.dataOffset;
            nodeRanges.push_back(r);

            const bool skinned = NodeHasSkinnedMesh(m_nodeIds[i]);
            int jointCount = skinned ? GetJointCountForNode(m_nodeIds[i]) : 0;
            if (jointCount <= 0 && skinned)
                jointCount = maxJointCount;
            if (jointCount > 0)
            {
                const Skeleton &skeleton = GetSkeletonForNode(m_nodeIds[i]);
                const bool hasMatchingSkeleton = skeleton.GetBoneCount() == jointCount;
                const mat4 invRoot = hasMatchingSkeleton ? glm::inverse(skeleton.rootTransform) : mat4(1.f);
                size_t base = allJointMatrices.size();
                allJointMatrices.resize(base + static_cast<size_t>(jointCount));
                if (!rt.jointMatrices.empty() && static_cast<int>(rt.jointMatrices.size()) == jointCount)
                {
                    // Joint matrices from EvaluatePose include the skeleton hierarchy transforms
                    // (via intermediatePrefix) which bake in the glTF root node rotation.
                    // The shader does: boneTransform * worldMatrix, and the worldMatrix also
                    // contains that root rotation, causing it to be double-applied.
                    //
                    // Fix: strip the baked-in root transform from joint matrices so the
                    // shader's worldMatrix applies it instead.  This preserves correct
                    // skeleton orientation while allowing user transforms (move/rotate/scale)
                    // to affect skinned meshes.
                    //
                    // Math: uploadedJoint = inv(rootTransform) * skeletonPose
                    // Shader: inv(rootTransform) * skeletonPose * worldMatrix
                    //       = inv(R) * R * animatedPose * R * userTransform
                    //       = animatedPose * R * userTransform   (correct: R once, userTransform once)
                    for (int j = 0; j < jointCount; j++)
                        allJointMatrices[base + j] = invRoot * rt.jointMatrices[j];
                }
                else
                {
                    for (int j = 0; j < jointCount; j++)
                        allJointMatrices[base + j] = mat4(1.f);
                }

                BufferRange jr{};
                jr.data = allJointMatrices.data() + base;
                jr.size = static_cast<size_t>(jointCount) * sizeof(mat4);
                jr.offset = rt.dataOffset + sizeof(NodeGpuData);
                jointRanges.push_back(jr);
            }
        }

        if (!nodeRanges.empty())
            m_storages[frame]->Copy(static_cast<uint32_t>(nodeRanges.size()), nodeRanges.data(), true);

        if (!jointRanges.empty())
            m_storages[frame]->Copy(static_cast<uint32_t>(jointRanges.size()), jointRanges.data(), true);
    }

    void Scene::UploadDynamicUniforms(CommandBuffer *cmd)
    {
        if (RHII.GetApi() != PE_GRAPHICS_API_DX12)
            return;

        if (!cmd || m_storages.empty() || m_storagesDevice.empty())
            return;

        const uint32_t frame = RHII.GetFrameIndex();
        if (frame >= m_storages.size() || frame >= m_storagesDevice.size())
            return;

        Buffer *src = m_storages[frame];
        Buffer *dst = m_storagesDevice[frame];
        if (!src || !dst)
            return;

        PE_ERROR_IF(src->Size() != dst->Size(), "Scene::UploadDynamicUniforms: storage mirror size mismatch");
        const size_t copySize = src->Size();
        if (!copySize)
            return;

        cmd->CopyBuffer(src, dst, copySize, 0, 0);

        BufferBarrierInfo barrier{};
        barrier.buffer = dst;
        barrier.stageMask = PE_STAGE_COMPUTE_SHADER | PE_STAGE_VERTEX_SHADER;
        barrier.accessMask = PE_ACCESS_SHADER_STORAGE_READ;
        barrier.offset = 0;
        barrier.size = copySize;
        cmd->BufferBarrier(barrier);

        dst->GetTrackInfo() = barrier;
    }

    Buffer *Scene::UpdateLodUniforms(uint32_t frame)
    {
        if (m_lodUniforms.empty() || frame >= m_lodUniforms.size())
            return nullptr;
        const auto &s = Settings::Get<SceneSettings>();
        LodUBOData ubo{};
        ubo.enabled = s.lod_enabled ? 1u : 0u;
        ubo.bias = s.lod_bias > 0.0f ? s.lod_bias : 1.0f;
        ubo.distances[0] = s.lod_distances[0];
        ubo.distances[1] = s.lod_distances[1];
        ubo.distances[2] = s.lod_distances[2];
        BufferRange range{};
        range.data = &ubo;
        range.size = sizeof(ubo);
        range.offset = 0;
        m_lodUniforms[frame]->Copy(1, &range, false);
        return m_lodUniforms[frame];
    }

    void Scene::DispatchCulling(CommandBuffer *cmd, PassInfo *passInfo, PassInfo *sortPassInfo,
                                Image *hiZPyramid, Buffer *occlusionData)
    {
        uint32_t frame = RHII.GetFrameIndex();
        const bool hasAlphaBlendMeshes = m_hasAlphaBlendMeshes;
        const bool hasTransmissionMeshes = m_hasTransmissionMeshes;
        const bool hasTransparentMeshes = hasAlphaBlendMeshes || hasTransmissionMeshes;
        const bool needsIndirectCountFallback = !RHII.GetCaps().indirectCount;

        auto nextPow2 = [](uint32_t value)
        {
            uint32_t result = 1;
            while (result < value)
                result <<= 1;
            return result;
        };

        uint64_t indirectSize = static_cast<uint64_t>(m_indirectCapacity) * PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE;
        uint32_t alphaBlendSortCapacity = nextPow2(m_alphaBlendMeshCount ? m_alphaBlendMeshCount : 1u);
        uint32_t transmissionSortCapacity = nextPow2(m_transmissionMeshCount ? m_transmissionMeshCount : 1u);
        uint64_t indirectAllSize = m_indirectAll ? static_cast<uint64_t>(m_indirectAll->Size()) : 0ull;
        uint64_t alphaBlendIndirectSize = static_cast<uint64_t>(alphaBlendSortCapacity) * PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE;
        uint64_t alphaBlendSortKeySize = static_cast<uint64_t>(alphaBlendSortCapacity) * sizeof(float);
        uint64_t transmissionIndirectSize = static_cast<uint64_t>(transmissionSortCapacity) * PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE;
        uint64_t transmissionSortKeySize = static_cast<uint64_t>(transmissionSortCapacity) * sizeof(float);
        uint64_t alphaBlendIndirectAccessSize = needsIndirectCountFallback ? indirectSize : alphaBlendIndirectSize;
        uint64_t transmissionIndirectAccessSize = needsIndirectCountFallback ? indirectSize : transmissionIndirectSize;
        PE_PROFILE_COUNTER("Culling AlphaBlend Mesh Count", m_alphaBlendMeshCount);
        PE_PROFILE_COUNTER("Culling Transmission Mesh Count", m_transmissionMeshCount);
        PE_PROFILE_COUNTER("Culling AlphaBlend Sort Capacity", alphaBlendSortCapacity);
        PE_PROFILE_COUNTER("Culling Transmission Sort Capacity", transmissionSortCapacity);

        {
            PE_PROFILE_SCOPE("Culling Fill Buffers");
            cmd->FillBuffer(m_cullingCountersBuffers[frame], 0, 9 * sizeof(uint32_t), 0);
            if (needsIndirectCountFallback)
            {
                cmd->FillBuffer(m_indirectOpaqueSS[frame], 0, indirectSize, 0);
                cmd->FillBuffer(m_indirectAlphaCutSS[frame], 0, indirectSize, 0);
                cmd->FillBuffer(m_indirectOpaqueDS[frame], 0, indirectSize, 0);
                cmd->FillBuffer(m_indirectAlphaCutDS[frame], 0, indirectSize, 0);
                cmd->FillBuffer(m_indirectSelected[frame], 0, indirectSize, 0);
                cmd->FillBuffer(m_indirectVoxels[frame], 0, indirectSize, 0);
                cmd->FillBuffer(m_indirectTerrain[frame], 0, indirectSize, 0);
            }
            if (hasAlphaBlendMeshes)
            {
                cmd->FillBuffer(m_indirectAlphaBlend[frame], 0, alphaBlendIndirectAccessSize, 0);
                cmd->FillBuffer(m_sortKeysAlphaBlend[frame], 0, alphaBlendSortKeySize, 0x7F7FFFFF);
            }
            if (hasTransmissionMeshes)
            {
                cmd->FillBuffer(m_indirectTransmission[frame], 0, transmissionIndirectAccessSize, 0);
                cmd->FillBuffer(m_sortKeysTransmission[frame], 0, transmissionSortKeySize, 0x7F7FFFFF);
            }
        }

        auto makeBufferBarrier = [](Buffer *buffer,
                                    PeBarrierSync stageMask,
                                    PeBarrierAccess accessMask,
                                    uint64_t size)
        {
            BufferBarrierInfo barrier{};
            barrier.buffer = buffer;
            barrier.stageMask = stageMask;
            barrier.accessMask = accessMask;
            barrier.size = size;
            barrier.offset = 0;
            return barrier;
        };

        const uint64_t countersSize = 9 * sizeof(uint32_t);
        {
            PE_PROFILE_SCOPE("Culling Compute Access Barriers");
            std::vector<BufferBarrierInfo> computeAccessBarriers;
            computeAccessBarriers.reserve(hasTransparentMeshes ? 12 : 8);
            if (m_indirectAll && indirectAllSize > 0)
            {
                computeAccessBarriers.push_back(makeBufferBarrier(m_indirectAll,
                                                                  PE_STAGE_COMPUTE_SHADER,
                                                                  PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_STORAGE_READ,
                                                                  indirectAllSize));
            }
            computeAccessBarriers.push_back(makeBufferBarrier(m_cullingCountersBuffers[frame],
                                                              PE_STAGE_COMPUTE_SHADER,
                                                              PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_WRITE,
                                                              countersSize));
            computeAccessBarriers.push_back(makeBufferBarrier(m_indirectOpaqueSS[frame],
                                                              PE_STAGE_COMPUTE_SHADER,
                                                              PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_WRITE,
                                                              indirectSize));
            computeAccessBarriers.push_back(makeBufferBarrier(m_indirectAlphaCutSS[frame],
                                                              PE_STAGE_COMPUTE_SHADER,
                                                              PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_WRITE,
                                                              indirectSize));
            computeAccessBarriers.push_back(makeBufferBarrier(m_indirectOpaqueDS[frame],
                                                              PE_STAGE_COMPUTE_SHADER,
                                                              PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_WRITE,
                                                              indirectSize));
            computeAccessBarriers.push_back(makeBufferBarrier(m_indirectAlphaCutDS[frame],
                                                              PE_STAGE_COMPUTE_SHADER,
                                                              PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_WRITE,
                                                              indirectSize));
            computeAccessBarriers.push_back(makeBufferBarrier(m_indirectSelected[frame],
                                                              PE_STAGE_COMPUTE_SHADER,
                                                              PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_WRITE,
                                                              indirectSize));
            computeAccessBarriers.push_back(makeBufferBarrier(m_indirectVoxels[frame],
                                                              PE_STAGE_COMPUTE_SHADER,
                                                              PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_WRITE,
                                                              indirectSize));
            computeAccessBarriers.push_back(makeBufferBarrier(m_indirectTerrain[frame],
                                                              PE_STAGE_COMPUTE_SHADER,
                                                              PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_WRITE,
                                                              indirectSize));
            if (hasAlphaBlendMeshes)
            {
                computeAccessBarriers.push_back(makeBufferBarrier(m_indirectAlphaBlend[frame],
                                                                  PE_STAGE_COMPUTE_SHADER,
                                                                  PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_WRITE,
                                                                  alphaBlendIndirectAccessSize));
                computeAccessBarriers.push_back(makeBufferBarrier(m_sortKeysAlphaBlend[frame],
                                                                  PE_STAGE_COMPUTE_SHADER,
                                                                  PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_WRITE,
                                                                  alphaBlendSortKeySize));
            }
            if (hasTransmissionMeshes)
            {
                computeAccessBarriers.push_back(makeBufferBarrier(m_indirectTransmission[frame],
                                                                  PE_STAGE_COMPUTE_SHADER,
                                                                  PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_WRITE,
                                                                  transmissionIndirectAccessSize));
                computeAccessBarriers.push_back(makeBufferBarrier(m_sortKeysTransmission[frame],
                                                                  PE_STAGE_COMPUTE_SHADER,
                                                                  PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_WRITE,
                                                                  transmissionSortKeySize));
            }
            cmd->BufferBarriers(computeAccessBarriers);
        }

        {
            PE_PROFILE_SCOPE("Culling Bind Pipeline");
            cmd->BindPipeline(*passInfo);
        }

        Descriptor *set = nullptr;
        {
            PE_PROFILE_SCOPE("Culling Descriptor Update");
            const auto &sets = passInfo->GetDescriptors(frame);
            set = sets[0];
            set->SetBuffer(0, m_indirectAll);
            set->SetBuffer(1, GetMeshConstants());
            set->SetBuffer(2, m_cullingCountersBuffers[frame]);
            set->SetBuffer(3, m_indirectOpaqueSS[frame]);
            set->SetBuffer(4, m_indirectAlphaCutSS[frame]);
            set->SetBuffer(5, m_indirectAlphaBlend[frame]);
            set->SetBuffer(6, m_indirectTransmission[frame]);
            set->SetBuffer(7, m_indirectSelected[frame]);
            set->SetBuffer(8, m_indirectOpaqueDS[frame]);
            set->SetBuffer(9, m_indirectAlphaCutDS[frame]);
            set->SetBuffer(10, m_sortKeysAlphaBlend[frame]);
            set->SetBuffer(11, m_sortKeysTransmission[frame]);
            set->SetBuffer(12, GetUniforms(frame));
            if (set->HasBinding(17))
                set->SetBuffer(17, m_indirectVoxels[frame]);
            if (set->HasBinding(18))
                set->SetBuffer(18, m_indirectTerrain[frame]);
            // Occlusion variant (OcclusionCullingPass) binds the Hi-Z pyramid + params.
            // The frustum-only variant's shader has no such bindings, so this is skipped.
            if (hiZPyramid && occlusionData)
            {
                set->SetImageView(13, hiZPyramid->GetSRV());
                set->SetBuffer(14, occlusionData);
            }
            // LOD params (binding 16, present in every cull variant). Picks per-mesh LOD by camera distance.
            if (Buffer *lodUbo = UpdateLodUniforms(frame); lodUbo && set->HasBinding(16))
                set->SetBuffer(16, lodUbo);
            set->Update();
        }

        {
            PE_PROFILE_SCOPE("Culling Bind Descriptors");
            cmd->BindDescriptors(1, &set);
        }

        struct PushConstants
        {
            uint32_t maxDrawCount;
            uint32_t enableFrustumCulling;
            float cameraPositionX;
            float cameraPositionY;
            float cameraPositionZ;
            uint32_t enableVoxelOcclusion; // VOXEL_HIZ: 1 when a voxel Hi-Z pyramid is bound (13/14)
            alignas(16) vec4 frustumPlanes[6];
        } constants{};
        static_assert(offsetof(PushConstants, frustumPlanes) == 32, "PushConstants frustum planes must match std430 layout");
        static_assert(sizeof(PushConstants) == 128, "PushConstants must fill the Vulkan guaranteed push-constant budget");
        {
            PE_PROFILE_SCOPE("Culling Build Constants");
            constants.maxDrawCount = m_meshCount;

            Camera *camera = m_cameras.empty() ? nullptr : m_cameras[0];
            bool frustumCulling = Settings::Get<SceneSettings>().frustum_culling && camera;
            constants.enableFrustumCulling = frustumCulling ? 1u : 0u;
            // VOXEL_HIZ (frustum CullingPass only): occlusion-test the voxel emit when a voxel-inclusive
            // Hi-Z pyramid + its matching (previous-frame) view-proj UBO were supplied. Ignored by the
            // PHASE1/2 shader variants (the field is just a renamed pad there).
            constants.enableVoxelOcclusion = (hiZPyramid && occlusionData) ? 1u : 0u;
            if (camera)
            {
                vec3 camPos = camera->GetPosition();
                constants.cameraPositionX = camPos.x;
                constants.cameraPositionY = camPos.y;
                constants.cameraPositionZ = camPos.z;
                const auto &planes = camera->GetFrustumPlanes();
                for (int i = 0; i < 6; i++)
                    constants.frustumPlanes[i] = vec4(planes[i].normal[0], planes[i].normal[1], planes[i].normal[2], planes[i].d);
            }
        }

        {
            PE_PROFILE_SCOPE("Culling Push Constants");
            cmd->SetConstants(constants);
            cmd->PushConstants();
        }

        uint32_t groupCount = (m_meshCount + 63) / 64;
        {
            PE_PROFILE_SCOPE("Culling Dispatch");
            cmd->Dispatch(groupCount, 1, 1);
        }

        // Record the compute writes so subsequent Buffer::Barrier calls emit
        // srcStage=Compute, srcAccess=ShaderWrite. Without this, the next barrier
        // inherits whatever trackInfo was left from the prior frame (or {None,None}
        // on first use) and the dispatch write is not in the src scope — readers
        // at DrawIndirect stage race with the cull shader.
        {
            PE_PROFILE_SCOPE("Culling Record Compute Writes");
            auto recordComputeWrite = [](Buffer *buffer)
            {
                BufferTrackInfo &ti = buffer->GetTrackInfo();
                ti.stageMask = PE_STAGE_COMPUTE_SHADER;
                ti.accessMask = PE_ACCESS_SHADER_STORAGE_WRITE;
            };
            recordComputeWrite(m_cullingCountersBuffers[frame]);
            recordComputeWrite(m_indirectOpaqueSS[frame]);
            recordComputeWrite(m_indirectAlphaCutSS[frame]);
            recordComputeWrite(m_indirectOpaqueDS[frame]);
            recordComputeWrite(m_indirectAlphaCutDS[frame]);
            recordComputeWrite(m_indirectSelected[frame]);
            recordComputeWrite(m_indirectVoxels[frame]);
            recordComputeWrite(m_indirectTerrain[frame]);
            if (hasAlphaBlendMeshes)
            {
                recordComputeWrite(m_indirectAlphaBlend[frame]);
                recordComputeWrite(m_sortKeysAlphaBlend[frame]);
            }
            if (hasTransmissionMeshes)
            {
                recordComputeWrite(m_indirectTransmission[frame]);
                recordComputeWrite(m_sortKeysTransmission[frame]);
            }
        }

        auto makeComputeBarrier = [&](Buffer *buffer, uint64_t size)
        {
            return makeBufferBarrier(buffer,
                                     PE_STAGE_COMPUTE_SHADER,
                                     PE_ACCESS_SHADER_WRITE | PE_ACCESS_SHADER_READ,
                                     size);
        };

        if (hasTransparentMeshes && sortPassInfo)
        {
            {
                PE_PROFILE_SCOPE("Culling Sort Prep Barriers");
                std::vector<BufferBarrierInfo> sortPrepBarriers;
                sortPrepBarriers.reserve(4);
                if (hasAlphaBlendMeshes)
                {
                    sortPrepBarriers.push_back(makeComputeBarrier(m_indirectAlphaBlend[frame], alphaBlendIndirectAccessSize));
                    sortPrepBarriers.push_back(makeComputeBarrier(m_sortKeysAlphaBlend[frame], alphaBlendSortKeySize));
                }
                if (hasTransmissionMeshes)
                {
                    sortPrepBarriers.push_back(makeComputeBarrier(m_indirectTransmission[frame], transmissionIndirectAccessSize));
                    sortPrepBarriers.push_back(makeComputeBarrier(m_sortKeysTransmission[frame], transmissionSortKeySize));
                }
                if (!sortPrepBarriers.empty())
                    cmd->BufferBarriers(sortPrepBarriers);
            }

            auto dispatchBitonicSort = [&](Buffer *indirectBuffer,
                                           Buffer *sortKeyBuffer,
                                           uint32_t n,
                                           uint64_t indirectBarrierSize,
                                           uint64_t sortKeyBarrierSize)
            {
                if (m_meshCount < 2 || n < 2)
                    return;

                {
                    PE_PROFILE_SCOPE("Culling Sort Bind Pipeline");
                    cmd->BindPipeline(*sortPassInfo);
                }

                Descriptor *sortSet = nullptr;
                {
                    PE_PROFILE_SCOPE("Culling Sort Descriptor Update");
                    const auto &sortSets = sortPassInfo->GetDescriptors(frame);
                    sortSet = sortSets[0];
                    sortSet->SetBuffer(0, sortKeyBuffer);
                    sortSet->SetBuffer(1, indirectBuffer);
                    sortSet->Update();
                }
                {
                    PE_PROFILE_SCOPE("Culling Sort Bind Descriptors");
                    cmd->BindDescriptors(1, &sortSet);
                }

                struct SortPushConstants
                {
                    uint32_t count;
                    uint32_t blockSize;
                    uint32_t subBlockSize;
                } sortConstants{};
                sortConstants.count = n;

                uint32_t numGroups = (n / 2 + 63) / 64;
                std::vector<BufferBarrierInfo> dispatchBarriers;
                dispatchBarriers.reserve(2);
                for (uint32_t blockSize = 2; blockSize <= n; blockSize <<= 1)
                {
                    for (uint32_t subBlockSize = blockSize; subBlockSize >= 2; subBlockSize >>= 1)
                    {
                        sortConstants.blockSize = blockSize;
                        sortConstants.subBlockSize = subBlockSize;
                        {
                            PE_PROFILE_SCOPE("Culling Sort Dispatch Step");
                            cmd->SetConstants(sortConstants);
                            cmd->PushConstants();
                            cmd->Dispatch(numGroups, 1, 1);
                        }

                        {
                            PE_PROFILE_SCOPE("Culling Sort Step Barriers");
                            dispatchBarriers.clear();
                            dispatchBarriers.push_back(makeComputeBarrier(sortKeyBuffer, sortKeyBarrierSize));
                            dispatchBarriers.push_back(makeComputeBarrier(indirectBuffer, indirectBarrierSize));
                            cmd->BufferBarriers(dispatchBarriers);
                        }
                    }
                }
            };

            if (hasAlphaBlendMeshes)
            {
                PE_PROFILE_SCOPE("Culling Sort AlphaBlend");
                dispatchBitonicSort(m_indirectAlphaBlend[frame],
                                    m_sortKeysAlphaBlend[frame],
                                    alphaBlendSortCapacity,
                                    alphaBlendIndirectAccessSize,
                                    alphaBlendSortKeySize);
            }
            if (hasTransmissionMeshes)
            {
                PE_PROFILE_SCOPE("Culling Sort Transmission");
                dispatchBitonicSort(m_indirectTransmission[frame],
                                    m_sortKeysTransmission[frame],
                                    transmissionSortCapacity,
                                    transmissionIndirectAccessSize,
                                    transmissionSortKeySize);
            }
        }

        {
            PE_PROFILE_SCOPE("Culling Final Indirect Barriers");
            std::vector<BufferBarrierInfo> indirectBarriers;
            indirectBarriers.reserve(hasTransparentMeshes ? 9 : 7);
            auto addIndirectBarrier = [&](Buffer *buffer)
            {
                indirectBarriers.push_back(makeBufferBarrier(buffer,
                                                             PE_STAGE_DRAW_INDIRECT,
                                                             PE_ACCESS_INDIRECT_COMMAND_READ,
                                                             static_cast<uint64_t>(m_indirectCapacity) * PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE));
            };
            addIndirectBarrier(m_indirectOpaqueSS[frame]);
            addIndirectBarrier(m_indirectAlphaCutSS[frame]);
            addIndirectBarrier(m_indirectOpaqueDS[frame]);
            addIndirectBarrier(m_indirectAlphaCutDS[frame]);
            if (hasAlphaBlendMeshes)
            {
                addIndirectBarrier(m_indirectAlphaBlend[frame]);
            }
            if (hasTransmissionMeshes)
            {
                addIndirectBarrier(m_indirectTransmission[frame]);
            }
            addIndirectBarrier(m_indirectSelected[frame]);
            addIndirectBarrier(m_indirectVoxels[frame]);
            addIndirectBarrier(m_indirectTerrain[frame]);
            indirectBarriers.push_back(makeBufferBarrier(m_cullingCountersBuffers[frame],
                                                         PE_STAGE_DRAW_INDIRECT,
                                                         PE_ACCESS_INDIRECT_COMMAND_READ,
                                                         countersSize));
            cmd->BufferBarriers(indirectBarriers);
        }
    }

    void Scene::DispatchCullingPhase(CommandBuffer *cmd, PassInfo *passInfo, CullPhase phase,
                                     Image *hiZPyramid, Buffer *occlusionData)
    {
        const uint32_t frame = RHII.GetFrameIndex();
        const bool phase1 = (phase == CullPhase::Phase1);
        const bool needsIndirectCountFallback = !RHII.GetCaps().indirectCount;
        const uint64_t indirectSize = static_cast<uint64_t>(m_indirectCapacity) * PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE;
        const uint64_t indirectAllSize = m_indirectAll ? static_cast<uint64_t>(m_indirectAll->Size()) : 0ull;
        const uint64_t countersSize = 7 * sizeof(uint32_t);
        const uint64_t visibilitySize = static_cast<uint64_t>(m_indirectCapacity) * sizeof(uint32_t);

        // Output set: phase 1 -> A (last-frame-visible), phase 2 -> B (newly-disoccluded). Opaque only.
        Buffer *counters = phase1 ? m_occCountersA[frame] : m_occCountersB[frame];
        Buffer *opaqueSS = phase1 ? m_occOpaqueSSA[frame] : m_occOpaqueSSB[frame];
        Buffer *alphaCutSS = phase1 ? m_occAlphaCutSSA[frame] : m_occAlphaCutSSB[frame];
        Buffer *opaqueDS = phase1 ? m_occOpaqueDSA[frame] : m_occOpaqueDSB[frame];
        Buffer *alphaCutDS = phase1 ? m_occAlphaCutDSA[frame] : m_occAlphaCutDSB[frame];

        auto makeBufferBarrier = [](Buffer *buffer, PeBarrierSync stageMask, PeBarrierAccess accessMask, uint64_t size)
        {
            BufferBarrierInfo b{};
            b.buffer = buffer;
            b.stageMask = stageMask;
            b.accessMask = accessMask;
            b.size = size;
            b.offset = 0;
            return b;
        };

        {
            PE_PROFILE_SCOPE("OccCull Fill Buffers");
            cmd->FillBuffer(counters, 0, countersSize, 0);
            if (needsIndirectCountFallback)
            {
                cmd->FillBuffer(opaqueSS, 0, indirectSize, 0);
                cmd->FillBuffer(alphaCutSS, 0, indirectSize, 0);
                cmd->FillBuffer(opaqueDS, 0, indirectSize, 0);
                cmd->FillBuffer(alphaCutDS, 0, indirectSize, 0);
            }
        }

        {
            PE_PROFILE_SCOPE("OccCull Compute Access Barriers");
            const PeBarrierAccess rw = PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_WRITE;
            std::vector<BufferBarrierInfo> barriers;
            barriers.reserve(7);
            if (m_indirectAll && indirectAllSize > 0)
            {
                barriers.push_back(makeBufferBarrier(m_indirectAll,
                                                     PE_STAGE_COMPUTE_SHADER,
                                                     PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_STORAGE_READ,
                                                     indirectAllSize));
            }
            barriers.push_back(makeBufferBarrier(counters, PE_STAGE_COMPUTE_SHADER, rw, countersSize));
            barriers.push_back(makeBufferBarrier(opaqueSS, PE_STAGE_COMPUTE_SHADER, rw, indirectSize));
            barriers.push_back(makeBufferBarrier(alphaCutSS, PE_STAGE_COMPUTE_SHADER, rw, indirectSize));
            barriers.push_back(makeBufferBarrier(opaqueDS, PE_STAGE_COMPUTE_SHADER, rw, indirectSize));
            barriers.push_back(makeBufferBarrier(alphaCutDS, PE_STAGE_COMPUTE_SHADER, rw, indirectSize));
            barriers.push_back(makeBufferBarrier(m_visibility, PE_STAGE_COMPUTE_SHADER, rw, visibilitySize));
            cmd->BufferBarriers(barriers);
        }

        {
            PE_PROFILE_SCOPE("OccCull Bind Pipeline");
            cmd->BindPipeline(*passInfo);
        }

        Descriptor *set = nullptr;
        {
            PE_PROFILE_SCOPE("OccCull Descriptor Update");
            const auto &sets = passInfo->GetDescriptors(frame);
            set = sets[0];
            // The PHASE1/PHASE2 cull variants never touch the transparent/selected/sort-key buffers,
            // so reflection strips those bindings from the variant's layout (DXIL does on DX12; SPIR-V
            // may keep them). Bind only declared slots: SetBuffer on a missing binding logs a per-frame
            // "[Descriptor] Binding N not found" warning (GetBindingIndex), which on DX12 spammed the log
            // and made the frame CPU-bound. HasBinding does not warn.
            auto bind = [&](uint32_t b, Buffer *buf)
            {
                if (set->HasBinding(b))
                    set->SetBuffer(b, buf);
            };
            bind(0, m_indirectAll);
            bind(1, GetMeshConstants());
            bind(2, counters);
            bind(3, opaqueSS);
            bind(4, alphaCutSS);
            bind(5, m_indirectAlphaBlend[frame]);
            bind(6, m_indirectTransmission[frame]);
            bind(7, m_indirectSelected[frame]);
            bind(8, opaqueDS);
            bind(9, alphaCutDS);
            bind(10, m_sortKeysAlphaBlend[frame]);
            bind(11, m_sortKeysTransmission[frame]);
            bind(12, GetUniforms(frame));
            if (hiZPyramid && occlusionData) // phase 2 (HIZ_OCCLUSION variant)
            {
                if (set->HasBinding(13))
                    set->SetImageView(13, hiZPyramid->GetSRV());
                bind(14, occlusionData);
            }
            bind(15, m_visibility);
            bind(16, UpdateLodUniforms(frame)); // LOD params (present in every cull variant)
            set->Update();
        }

        {
            PE_PROFILE_SCOPE("OccCull Bind Descriptors");
            cmd->BindDescriptors(1, &set);
        }

        struct PushConstants
        {
            uint32_t maxDrawCount;
            uint32_t enableFrustumCulling;
            float cameraPositionX;
            float cameraPositionY;
            float cameraPositionZ;
            uint32_t enableVoxelOcclusion; // VOXEL_HIZ: 1 when a voxel Hi-Z pyramid is bound (13/14)
            alignas(16) vec4 frustumPlanes[6];
        } constants{};
        static_assert(sizeof(PushConstants) == 128, "PushConstants must match CullingCS.hlsl");
        {
            PE_PROFILE_SCOPE("OccCull Build Constants");
            constants.maxDrawCount = m_meshCount;
            Camera *camera = m_cameras.empty() ? nullptr : m_cameras[0];
            bool frustumCulling = Settings::Get<SceneSettings>().frustum_culling && camera;
            constants.enableFrustumCulling = frustumCulling ? 1u : 0u;
            // VOXEL_HIZ (frustum CullingPass only): occlusion-test the voxel emit when a voxel-inclusive
            // Hi-Z pyramid + its matching (previous-frame) view-proj UBO were supplied. Ignored by the
            // PHASE1/2 shader variants (the field is just a renamed pad there).
            constants.enableVoxelOcclusion = (hiZPyramid && occlusionData) ? 1u : 0u;
            if (camera)
            {
                vec3 camPos = camera->GetPosition();
                constants.cameraPositionX = camPos.x;
                constants.cameraPositionY = camPos.y;
                constants.cameraPositionZ = camPos.z;
                const auto &planes = camera->GetFrustumPlanes();
                for (int i = 0; i < 6; i++)
                    constants.frustumPlanes[i] = vec4(planes[i].normal[0], planes[i].normal[1], planes[i].normal[2], planes[i].d);
            }
        }

        {
            PE_PROFILE_SCOPE("OccCull Push Constants");
            cmd->SetConstants(constants);
            cmd->PushConstants();
        }

        {
            PE_PROFILE_SCOPE("OccCull Dispatch");
            cmd->Dispatch((m_meshCount + 63) / 64, 1, 1);
        }

        {
            PE_PROFILE_SCOPE("OccCull Record Compute Writes");
            auto recordComputeWrite = [](Buffer *buffer)
            {
                BufferTrackInfo &ti = buffer->GetTrackInfo();
                ti.stageMask = PE_STAGE_COMPUTE_SHADER;
                ti.accessMask = PE_ACCESS_SHADER_STORAGE_WRITE;
            };
            recordComputeWrite(counters);
            recordComputeWrite(opaqueSS);
            recordComputeWrite(alphaCutSS);
            recordComputeWrite(opaqueDS);
            recordComputeWrite(alphaCutDS);
            if (!phase1) // phase 2 rewrote the visibility flags for next frame's phase 1
                recordComputeWrite(m_visibility);
        }

        {
            PE_PROFILE_SCOPE("OccCull Final Indirect Barriers");
            std::vector<BufferBarrierInfo> barriers;
            barriers.reserve(5);
            auto addDrawBarrier = [&](Buffer *b)
            {
                barriers.push_back(makeBufferBarrier(b, PE_STAGE_DRAW_INDIRECT, PE_ACCESS_INDIRECT_COMMAND_READ, indirectSize));
            };
            addDrawBarrier(opaqueSS);
            addDrawBarrier(alphaCutSS);
            addDrawBarrier(opaqueDS);
            addDrawBarrier(alphaCutDS);
            barriers.push_back(makeBufferBarrier(counters, PE_STAGE_DRAW_INDIRECT, PE_ACCESS_INDIRECT_COMMAND_READ, countersSize));
            cmd->BufferBarriers(barriers);
        }
    }

    void Scene::DispatchShadowCull(CommandBuffer *cmd, PassInfo *passInfo, const vec4 frustumPlanes[6])
    {
        if (!passInfo || !m_indirectAll || m_meshCount == 0)
            return;

        const uint32_t frame = RHII.GetFrameIndex();
        const bool needsIndirectCountFallback = !RHII.GetCaps().indirectCount;
        const uint64_t indirectSize = static_cast<uint64_t>(m_indirectCapacity) * PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE;
        const uint64_t indirectAllSize = static_cast<uint64_t>(m_indirectAll->Size());
        const uint64_t countersSize = 2 * sizeof(uint32_t);

        auto makeBufferBarrier = [](Buffer *buffer, PeBarrierSync stageMask, PeBarrierAccess accessMask, uint64_t size)
        {
            BufferBarrierInfo barrier{};
            barrier.buffer = buffer;
            barrier.stageMask = stageMask;
            barrier.accessMask = accessMask;
            barrier.size = size;
            barrier.offset = 0;
            return barrier;
        };

        {
            PE_PROFILE_SCOPE("ShadowCull Fill Buffers");
            cmd->FillBuffer(m_shadowCullCounters[frame], 0, countersSize, 0);
            if (needsIndirectCountFallback)
            {
                cmd->FillBuffer(m_shadowIndirectRegular[frame], 0, indirectSize, 0);
                cmd->FillBuffer(m_shadowIndirectVoxels[frame], 0, indirectSize, 0);
            }
        }

        {
            PE_PROFILE_SCOPE("ShadowCull Compute Access Barriers");
            std::vector<BufferBarrierInfo> barriers;
            barriers.reserve(5);
            if (indirectAllSize > 0)
            {
                barriers.push_back(makeBufferBarrier(m_indirectAll,
                                                     PE_STAGE_COMPUTE_SHADER,
                                                     PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_STORAGE_READ,
                                                     indirectAllSize));
            }
            const PeBarrierAccess rw = PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_WRITE;
            barriers.push_back(makeBufferBarrier(m_shadowCullCounters[frame], PE_STAGE_COMPUTE_SHADER, rw, countersSize));
            barriers.push_back(makeBufferBarrier(m_shadowIndirectRegular[frame], PE_STAGE_COMPUTE_SHADER, rw, indirectSize));
            barriers.push_back(makeBufferBarrier(m_shadowIndirectVoxels[frame], PE_STAGE_COMPUTE_SHADER, rw, indirectSize));
            cmd->BufferBarriers(barriers);
        }

        cmd->BindPipeline(*passInfo);

        Descriptor *set = nullptr;
        {
            PE_PROFILE_SCOPE("ShadowCull Descriptor Update");
            const auto &sets = passInfo->GetDescriptors(frame);
            set = sets[0];
            set->SetBuffer(0, m_indirectAll);
            set->SetBuffer(1, GetMeshConstants());
            set->SetBuffer(2, m_shadowCullCounters[frame]);
            set->SetBuffer(3, m_shadowIndirectRegular[frame]);
            set->SetBuffer(4, m_shadowIndirectVoxels[frame]);
            set->SetBuffer(12, GetUniforms(frame));
            // LOD params (binding 16) so shadow casters pick the same distance-based LOD as the main pass.
            if (Buffer *lodUbo = UpdateLodUniforms(frame); lodUbo && set->HasBinding(16))
                set->SetBuffer(16, lodUbo);
            set->Update();
        }

        cmd->BindDescriptors(1, &set);

        struct PushConstants
        {
            uint32_t maxDrawCount;
            uint32_t arenaSlotBase;
            uint32_t pad0[2];
            alignas(16) vec4 planes[6];
            float cameraPos[3];
            float shadowLodBias;
        } constants{};
        static_assert(sizeof(PushConstants) == 128, "ShadowCull push constants must match ShadowCullCS.hlsl");
        constants.maxDrawCount = m_meshCount;
        // No arena: m_arenaSlotBase stays 0 — treat all draws as regular (idx >= meshCount is never true).
        constants.arenaSlotBase = HasArenaVoxels() ? m_arenaSlotBase : m_meshCount;
        for (int i = 0; i < 6; ++i)
            constants.planes[i] = frustumPlanes[i];
        if (Camera *camera = m_cameras.empty() ? nullptr : m_cameras[0])
        {
            vec3 camPos = camera->GetPosition();
            constants.cameraPos[0] = camPos.x;
            constants.cameraPos[1] = camPos.y;
            constants.cameraPos[2] = camPos.z;
        }
        constants.shadowLodBias = Settings::Get<SceneSettings>().shadow_lod_bias;

        cmd->SetConstants(constants);
        cmd->PushConstants();
        cmd->Dispatch((m_meshCount + 63) / 64, 1, 1);

        {
            PE_PROFILE_SCOPE("ShadowCull Record Compute Writes");
            auto recordComputeWrite = [](Buffer *buffer)
            {
                BufferTrackInfo &ti = buffer->GetTrackInfo();
                ti.stageMask = PE_STAGE_COMPUTE_SHADER;
                ti.accessMask = PE_ACCESS_SHADER_STORAGE_WRITE;
            };
            recordComputeWrite(m_shadowCullCounters[frame]);
            recordComputeWrite(m_shadowIndirectRegular[frame]);
            recordComputeWrite(m_shadowIndirectVoxels[frame]);
        }

        {
            PE_PROFILE_SCOPE("ShadowCull Final Indirect Barriers");
            std::vector<BufferBarrierInfo> barriers;
            barriers.reserve(3);
            barriers.push_back(makeBufferBarrier(m_shadowIndirectRegular[frame],
                                                 PE_STAGE_DRAW_INDIRECT,
                                                 PE_ACCESS_INDIRECT_COMMAND_READ,
                                                 indirectSize));
            barriers.push_back(makeBufferBarrier(m_shadowIndirectVoxels[frame],
                                                 PE_STAGE_DRAW_INDIRECT,
                                                 PE_ACCESS_INDIRECT_COMMAND_READ,
                                                 indirectSize));
            barriers.push_back(makeBufferBarrier(m_shadowCullCounters[frame],
                                                 PE_STAGE_DRAW_INDIRECT,
                                                 PE_ACCESS_INDIRECT_COMMAND_READ,
                                                 countersSize));
            cmd->BufferBarriers(barriers);
        }
    }

    void Scene::UpdateGeometry()
    {
        // Catch up previousWorldMatrix for motion vectors (runs every frame,
        // matching old ModelAsset::UpdateNodeMatrix behaviour).
        // Must happen BEFORE UpdateNodeMatrices so that prev holds last frame's
        // world matrix while world gets updated to the current frame's value.
        for (NodeId *node : m_nodesMoved)
        {
            if (node->index >= m_nodeIds.size() || m_nodeIds[node->index] != node)
                continue; // Safety check for deleted/recycled nodes

            NodeRuntime &rt = m_nodeRuntime[node->index];
            if (rt.gpuData.previousWorldMatrix != rt.gpuData.worldMatrix)
            {
                rt.gpuData.previousWorldMatrix = rt.gpuData.worldMatrix;
                rt.dirtyUniforms = 0xFF;
            }
        }

        // Clear the list of last frame's moved nodes now that we've caught them up.
        // This gives us a clean slate for the current frame's UpdateNodeMatrices() calls.
        m_nodesMoved.clear();

        {
            PE_PROFILE_SCOPE("Update Node Matrices");
            UpdateNodeMatrices();
        }

        {
            PE_PROFILE_SCOPE("Update Uniforms");
            UpdateUniformData();
        }
    }

    size_t Scene::GetNodeDataOffset(const NodeId *node) const
    {
        if (!IsNodeAlive(node))
            return static_cast<size_t>(-1);
        return m_nodeRuntime[node->index].dataOffset;
    }

    void Scene::FlushPendingGpuWork()
    {
        const bool rtSupport = RHII.GetCaps().rayTracing;
        if (!rtSupport)
        {
            m_blasDirty = false;
            m_tlasDirty = false;
        }

        const bool anyDirty = m_geometryDirty || m_instancesDirty || m_materialDirty ||
                              m_texturesDirty || (rtSupport && (m_blasDirty || m_tlasDirty));
        if (!anyDirty)
            return;

        if (m_geometryDirty)
        {
            // Clear GpuPending flag BEFORE upload so these nodes are included
            for (uint32_t i = 0; i < GetNodeCount(); i++)
                m_nodeRuntime[i].gpuPending = false;

            UpdateGeometryBuffers();

            m_geometryDirty = false;
            m_instancesDirty = false;
            // Geometry rebuild includes material table and image views
            m_materialDirty = false;
            m_texturesDirty = false;
            // m_blasDirty is set inside UpdateGeometryBuffers() when rtSupport is true
        }
        else if (m_instancesDirty)
        {
            // Mesh refs changed but geometry data is unchanged — rebuild raster instance data only
            for (uint32_t i = 0; i < GetNodeCount(); i++)
                m_nodeRuntime[i].gpuPending = false;

            UpdateRasterInstances();

            m_instancesDirty = false;
            if (m_texturesDirty)
            {
                UpdateTextures();
                m_texturesDirty = false;
            }
            if (m_materialDirty)
            {
                m_materialDirty = UpdateDirtyMaterials();
            }
        }
        else
        {
            if (m_texturesDirty)
            {
                UpdateTextures();
                m_texturesDirty = false;
            }
            if (m_materialDirty)
            {
                m_materialDirty = UpdateDirtyMaterials();
            }
        }

        // RT flush — independent of raster path
        if (rtSupport)
        {
            if (m_blasDirty)
            {
                // Full BLAS + TLAS rebuild (geometry buffer changed)
                CommandBuffer *cmd = RHII.GetMainQueue()->AcquireCommandBuffer();
                cmd->Begin();
                BuildAccelerationStructures(cmd);
                cmd->End();
                RHII.GetMainQueue()->Submit(1, &cmd, nullptr, nullptr);
                cmd->Wait();
                cmd->Return();
                m_blasDirty = false;
                m_tlasDirty = false;
            }
            else if (m_tlasDirty)
            {
                // Instance set changed — rebuild TLAS only, reuse existing BLAS handles
                RebuildTLASOnly();
                m_tlasDirty = false;
            }
        }
    }

    // Build a spatial digest of the active scene: every mesh-bearing node's world
    // AABB plus the aggregate play-area bounds and pairwise overlaps. Shared by the
    // MCP get_scene_digest tool and the Lua scene.digest() binding so the two stay
    // in lockstep. Cheap: reads the already-cached per-node worldAABB, no GPU work.
    SceneDigest ComputeSceneDigest(Scene &scene)
    {
        // Floors / decals are flat in Y; excluding them from overlaps avoids
        // "ground intersects everything" noise. Tuned for authoring-scale scenes.
        constexpr float kFlatY = 0.05f;

        SceneDigest digest;
        digest.totalNodeCount = scene.GetNodeCount();

        Camera *cam = scene.GetCameras().empty() ? nullptr : scene.GetActiveCamera();

        // Pass 1: gather every mesh-bearing node + the enabled&visible center heights.
        std::vector<float> liveYs;
        for (uint32_t i = 0; i < scene.GetNodeCount(); i++)
        {
            NodeId *node = scene.GetNodeId(i);
            if (!(scene.GetComponentFlags(node) & Component_Mesh))
                continue;

            SceneDigestNode dn;
            dn.id = "node:" + std::to_string(node->index) + ":" + std::to_string(node->revision);
            dn.name = scene.GetNodeName(node);
            dn.aabb = scene.GetWorldAABB(node);
            dn.enabled = scene.IsNodeHierarchyEnabled(node);
            dn.visible = scene.IsNodeRenderVisible(node);
            dn.inFrustum = cam ? cam->AABBInFrustum(dn.aabb) : true;
            if (NodeId *parent = scene.GetParent(node))
            {
                dn.parentIndex = static_cast<int>(parent->index);
                dn.parentName = scene.GetNodeName(parent);
            }
            if (dn.enabled && dn.visible)
                liveYs.push_back(dn.aabb.GetCenter().y);
            digest.nodes.push_back(std::move(dn));
        }
        digest.meshNodeCount = static_cast<uint32_t>(digest.nodes.size());

        // Robust vertical band. Pools are parked offscreen by two conventions —
        // hierarchy-disable (enabled=false) and the render-visible cull-flag
        // (visible=false, avoids TLAS churn, e.g. Warbound's members at y=-2080) — both
        // already excluded above. But a scene viewed BEFORE play can still have a pool
        // authored at y=-1000 while enabled+visible; a far vertical outlier like that
        // would stretch world_bounds to ~1000 units tall and drag ground_y to -1000.
        // Reject such outliers from bounds/ground/overlaps with a median+MAD band over
        // live node centers (generous floor: anything within kBandFloor of the median
        // Y is always kept), only when there are enough live nodes to estimate from.
        float bandLo = -std::numeric_limits<float>::infinity();
        float bandHi = std::numeric_limits<float>::infinity();
        if (liveYs.size() >= 8)
        {
            std::vector<float> tmp = liveYs;
            std::sort(tmp.begin(), tmp.end());
            const float median = tmp[tmp.size() / 2];
            for (float &v : tmp)
                v = std::abs(v - median);
            std::sort(tmp.begin(), tmp.end());
            const float mad = tmp[tmp.size() / 2];
            constexpr float kBandFloor = 50.0f; // never reject within 50 units of the median
            const float halfWidth = std::max(kBandFloor, 6.0f * 1.4826f * mad);
            bandLo = median - halfWidth;
            bandHi = median + halfWidth;
        }

        // Pass 2: flag far vertical outliers; build bounds/ground from in-band live nodes.
        for (SceneDigestNode &dn : digest.nodes)
        {
            if (!dn.enabled || !dn.visible)
                continue;
            const float cy = dn.aabb.GetCenter().y;
            if (cy < bandLo || cy > bandHi)
            {
                dn.groundOutlier = true;
                continue;
            }
            if (!digest.hasBounds)
            {
                digest.worldBounds = dn.aabb;
                digest.hasBounds = true;
            }
            else
            {
                digest.worldBounds.min = glm::min(digest.worldBounds.min, dn.aabb.min);
                digest.worldBounds.max = glm::max(digest.worldBounds.max, dn.aabb.max);
            }
        }
        if (digest.hasBounds)
            digest.groundY = digest.worldBounds.min.y;

        // Pairwise AABB overlaps among in-band, enabled, visible, non-flat nodes.
        // Sweep-and-prune on X: collect the participating nodes, sort by aabb.min.x, then
        // for each only test the window of later nodes whose min.x still falls inside its
        // [min.x, max.x] span (sorted order lets us break as soon as it doesn't). That
        // window already overlaps on X, so only Y/Z need testing. Near-linear for the
        // scattered AABBs of a real authoring scene — the old plain O(n^2) had to bail out
        // entirely above kOverlapNodeCap mesh nodes, which silently returned zero overlaps
        // (and so neutered spatial.lua resolve_overlaps) on every scene that mattered.
        // A high *pair* cap, not a low *node* cap, guards the pathological all-overlapping case.
        constexpr size_t kMaxOverlapPairs = 4096;
        std::vector<size_t> cand;
        cand.reserve(digest.nodes.size());
        for (size_t i = 0; i < digest.nodes.size(); i++)
        {
            const SceneDigestNode &n = digest.nodes[i];
            if (n.enabled && n.visible && !n.groundOutlier && n.aabb.GetSize().y >= kFlatY)
                cand.push_back(i);
        }
        std::sort(cand.begin(), cand.end(),
                  [&](size_t l, size_t r)
                  { return digest.nodes[l].aabb.min.x < digest.nodes[r].aabb.min.x; });
        // True when i RESTS ON / is supported by o: o's XZ footprint contains i's AND i sits at
        // or just above o's top surface. That is genuine support (a prop on a thick ground slab,
        // a cup on a table) — not a collision to resolve, and it floods a real scene, so such
        // pairs are dropped below. A footprint-contained prop that is EMBEDDED (its bottom well
        // below o's top) is a real interpenetration and is kept; two co-located props (neither
        // sits atop the other) likewise still report.
        auto restsOn = [](const AABB &o, const AABB &i)
        {
            constexpr float kXzEps = 1e-3f;   // footprint containment slack
            constexpr float kRestEps = 1e-2f; // vertical contact slack — tolerates authoring snap penetration
            const bool xzContained = o.min.x <= i.min.x + kXzEps && o.max.x >= i.max.x - kXzEps &&
                                     o.min.z <= i.min.z + kXzEps && o.max.z >= i.max.z - kXzEps;
            return xzContained && i.min.y >= o.max.y - kRestEps;
        };
        for (size_t ai = 0; ai < cand.size() && !digest.overlapsTruncated; ai++)
        {
            const SceneDigestNode &na = digest.nodes[cand[ai]];
            for (size_t bi = ai + 1; bi < cand.size(); bi++)
            {
                const SceneDigestNode &nb = digest.nodes[cand[bi]];
                if (nb.aabb.min.x > na.aabb.max.x)
                    break; // sorted by min.x: no later candidate can reach back to overlap na on X
                const bool overlapYZ = na.aabb.min.y <= nb.aabb.max.y && na.aabb.max.y >= nb.aabb.min.y &&
                                       na.aabb.min.z <= nb.aabb.max.z && na.aabb.max.z >= nb.aabb.min.z;
                if (overlapYZ)
                {
                    // Drop siblings of one composite object: nodes sharing an immediate parent
                    // are parts of the same authored assembly (a creature's Body/Head/Legs, a
                    // tree's Trunk/Canopy) that are MEANT to interpenetrate — not a collision to
                    // resolve. Two distinct objects each live under their own parent node, so a
                    // genuine inter-object overlap has differing parents and still reports.
                    if (na.parentIndex >= 0 && na.parentIndex == nb.parentIndex)
                        continue;
                    // Drop resting/supported pairs (prop-on-ground floods the list and can't
                    // be separated by a horizontal nudge). The flat-Y skip only catches THIN
                    // floors; a thick ground/terrain slab is caught here by the footprint+rest
                    // test. Embedded or co-located pairs fall through and are reported.
                    if (restsOn(na.aabb, nb.aabb) || restsOn(nb.aabb, na.aabb))
                        continue;
                    const size_t ia = cand[ai], ib = cand[bi];
                    digest.overlaps.emplace_back(static_cast<int>(std::min(ia, ib)),
                                                 static_cast<int>(std::max(ia, ib)));
                    if (digest.overlaps.size() >= kMaxOverlapPairs)
                    {
                        digest.overlapsTruncated = true;
                        break;
                    }
                }
            }
        }

        return digest;
    }
} // namespace pe
