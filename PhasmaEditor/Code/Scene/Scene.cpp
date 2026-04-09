#include "Scene/Scene.h"
#include "Scene/Material.h"
#include "Scene/ModelAsset.h"
#include "Scene/SelectionManager.h"
#include "Camera/Camera.h"
#include "Systems/AnimationSystem.h"
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

namespace pe
{
    std::vector<uint32_t> Scene::s_aabbIndices = {
        0, 1, 1, 2, 2, 3, 3, 0,
        4, 5, 5, 6, 6, 7, 7, 4,
        0, 4, 1, 5, 2, 6, 3, 7};

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

        uint32_t swapchainImageCount = RHII.GetSwapchainImageCount();
        m_storages.resize(swapchainImageCount, nullptr);

        m_particleManager = new ParticleManager();
        m_particleManager->Init(); // disable until it is done

        InitLightBuffers();
    }

    Sampler *Scene::GetDefaultSampler() const
    {
        return m_defaultSampler;
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
        RHII.AddToDeletionQueue([b = m_blasMergedBuffer]()
                                { Buffer* buf = b; Buffer::Destroy(buf); });
        RHII.AddToDeletionQueue([b = m_scratchBuffer]()
                                { Buffer* buf = b; Buffer::Destroy(buf); });
        RHII.AddToDeletionQueue([b = m_meshInfoBuffer]()
                                { Buffer* buf = b; Buffer::Destroy(buf); });
        RHII.AddToDeletionQueue([b = m_meshConstants]()
                                { Buffer* buf = b; Buffer::Destroy(buf); });
    }

    Camera *Scene::GetCameraForNode(const NodeId *node) const
    {
        if (!node || node->index >= m_nodeComponentCache.size())
            return nullptr;
        const auto *tag = m_nodeComponentCache[node->index].camera;
        return tag ? tag->camera : nullptr;
    }

    void Scene::Update()
    {
        {
            PE_PROFILE_SCOPE("Camera Update");
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
                    SetLocalMatrix(camNode, localMat);
                }
            }
        }

        UpdateGeometry();
        UpdateLights();
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

        int sourceIndex = static_cast<int>(m_sources.size());
        SceneSource source;
        source.filePath = model->GetFilePath();
        source.primitiveType = model->GetPrimitiveType();
        source.primitiveParams = model->GetPrimitiveParams();
        source.primitiveParamCount = model->GetPrimitiveParamCount();
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
            if (auto *animSys = GetGlobalSystem<AnimationSystem>())
            {
                for (NodeId *node : nodeMap)
                {
                    if (node && NodeHasSkinnedMesh(node))
                        animSys->PlayAnimation(*this, node, 0, true);
                }
            }
        }
    }

    SceneNodeHandle Scene::AddModelDeferred(ModelAsset *model)
    {
        m_models.insert(model->GetId(), model);

        int sourceIndex = static_cast<int>(m_sources.size());
        SceneSource source;
        source.filePath = model->GetFilePath();
        source.primitiveType = model->GetPrimitiveType();
        source.primitiveParams = model->GetPrimitiveParams();
        source.primitiveParamCount = model->GetPrimitiveParamCount();
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

        // Delete SoA subtrees rooted at this model's root nodes
        auto rootIt = m_modelRootNodes.find(modelId);
        if (rootIt != m_modelRootNodes.end())
        {
            for (NodeId *root : rootIt->second)
            {
                // Skip nodes already deleted (sentinel from DeleteNode)
                if (root && root->index != UINT32_MAX)
                    DeleteNode(root);
            }
            m_modelRootNodes.erase(rootIt);
        }

        if (m_skeletonModel == model)
            ResetSkeletonCache();
        if (m_models.erase(modelId))
            delete model;
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
        return camera;
    }

    void Scene::RemoveCamera(Camera *camera)
    {
        if (m_cameras.size() <= 1)
            return;

        auto it = std::find(m_cameras.begin(), m_cameras.end(), camera);
        if (it != m_cameras.end())
        {
            SelectionManager::Instance().ClearSelection();

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

        int jointCount = GetSkeleton().GetBoneCount();
        if (jointCount > 0)
            allJointMatrices.reserve(GetNodeCount() * static_cast<uint32_t>(jointCount));

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

            if (jointCount > 0)
            {
                size_t base = allJointMatrices.size();
                allJointMatrices.resize(base + static_cast<size_t>(jointCount));
                if (!rt.jointMatrices.empty() && static_cast<int>(rt.jointMatrices.size()) == jointCount)
                {
                    mat4 invWorld = glm::inverse(rt.gpuData.worldMatrix);
                    for (int j = 0; j < jointCount; j++)
                        allJointMatrices[base + j] = invWorld * rt.jointMatrices[j];
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

    void Scene::DispatchCulling(CommandBuffer *cmd, PassInfo *passInfo, PassInfo *sortPassInfo)
    {
        uint32_t frame = RHII.GetFrameIndex();
        const bool hasTransparentMeshes = m_hasTransparentMeshes;

        uint64_t indirectSize = static_cast<uint64_t>(m_indirectCapacity) * sizeof(vk::DrawIndexedIndirectCommand);
        uint64_t sortKeySize = static_cast<uint64_t>(m_indirectCapacity) * sizeof(float);

        cmd->FillBuffer(m_cullingCountersBuffers[frame], 0, 7 * sizeof(uint32_t), 0);
        if (hasTransparentMeshes)
        {
            cmd->FillBuffer(m_indirectAlphaBlend[frame], 0, indirectSize, 0);
            cmd->FillBuffer(m_indirectTransmission[frame], 0, indirectSize, 0);
            cmd->FillBuffer(m_sortKeysAlphaBlend[frame], 0, sortKeySize, 0x7F7FFFFF); // float max — sentinels sort to end
            cmd->FillBuffer(m_sortKeysTransmission[frame], 0, sortKeySize, 0x7F7FFFFF);
        }

        auto addTransferToComputeBarrier = [&](Buffer *buffer, uint64_t size)
        {
            BufferBarrierInfo barrier{};
            barrier.buffer = buffer;
            barrier.stageMask = vk::PipelineStageFlagBits2::eTransfer | vk::PipelineStageFlagBits2::eComputeShader;
            barrier.accessMask = vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite;
            barrier.size = size;
            barrier.offset = 0;
            cmd->BufferBarrier(barrier);
        };

        BufferBarrierInfo countersBarrier{};
        countersBarrier.buffer = m_cullingCountersBuffers[frame];
        countersBarrier.stageMask = vk::PipelineStageFlagBits2::eTransfer | vk::PipelineStageFlagBits2::eComputeShader;
        countersBarrier.accessMask = vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite;
        countersBarrier.size = 7 * sizeof(uint32_t);
        countersBarrier.offset = 0;
        cmd->BufferBarrier(countersBarrier);
        if (hasTransparentMeshes)
        {
            addTransferToComputeBarrier(m_indirectAlphaBlend[frame], indirectSize);
            addTransferToComputeBarrier(m_indirectTransmission[frame], indirectSize);
            addTransferToComputeBarrier(m_sortKeysAlphaBlend[frame], sortKeySize);
            addTransferToComputeBarrier(m_sortKeysTransmission[frame], sortKeySize);
        }

        cmd->BindPipeline(*passInfo);

        const auto &sets = passInfo->GetDescriptors(frame);
        Descriptor *set = sets[0];
        set->SetBuffer(0, m_indirectAll);
        set->SetBuffer(1, m_meshConstants);
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
        set->SetBuffer(12, m_storages[frame]);
        set->Update();

        cmd->BindDescriptors(1, &set);

        struct PushConstants
        {
            uint32_t maxDrawCount;
            uint32_t enableFrustumCulling;
            float cameraPositionX;
            float cameraPositionY;
            float cameraPositionZ;
            float pad0;
            vec4 frustumPlanes[6];
        } constants{};
        static_assert(sizeof(PushConstants) <= 128, "PushConstants exceeds 128 bytes");
        constants.maxDrawCount = m_meshCount;

        Camera *camera = m_cameras.empty() ? nullptr : m_cameras[0];
        bool frustumCulling = Settings::Get<GlobalSettings>().frustum_culling && camera;
        constants.enableFrustumCulling = frustumCulling ? 1u : 0u;
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

        cmd->SetConstants(constants);
        cmd->PushConstants();

        uint32_t groupCount = (m_meshCount + 63) / 64;
        cmd->Dispatch(groupCount, 1, 1);

        auto addComputeBarrier = [&](Buffer *buffer, uint64_t size)
        {
            BufferBarrierInfo barrier{};
            barrier.buffer = buffer;
            barrier.stageMask = vk::PipelineStageFlagBits2::eComputeShader;
            barrier.accessMask = vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eShaderRead;
            barrier.size = size;
            barrier.offset = 0;
            cmd->BufferBarrier(barrier);
        };

        if (hasTransparentMeshes && sortPassInfo)
        {
            addComputeBarrier(m_indirectAlphaBlend[frame], indirectSize);
            addComputeBarrier(m_indirectTransmission[frame], indirectSize);
            addComputeBarrier(m_sortKeysAlphaBlend[frame], sortKeySize);
            addComputeBarrier(m_sortKeysTransmission[frame], sortKeySize);

            countersBarrier.stageMask = vk::PipelineStageFlagBits2::eComputeShader;
            countersBarrier.accessMask = vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eShaderRead;
            cmd->BufferBarrier(countersBarrier);

            auto dispatchBitonicSort = [&](Buffer *indirectBuffer, Buffer *sortKeyBuffer)
            {
                uint32_t n = m_indirectCapacity;
                if (m_meshCount < 2 || n < 2)
                    return;

                cmd->BindPipeline(*sortPassInfo);

                const auto &sortSets = sortPassInfo->GetDescriptors(frame);
                Descriptor *sortSet = sortSets[0];
                sortSet->SetBuffer(0, sortKeyBuffer);
                sortSet->SetBuffer(1, indirectBuffer);
                sortSet->Update();
                cmd->BindDescriptors(1, &sortSet);

                struct SortPushConstants
                {
                    uint32_t count;
                    uint32_t blockSize;
                    uint32_t subBlockSize;
                } sortConstants{};
                sortConstants.count = n;

                uint32_t numGroups = (n / 2 + 63) / 64;
                for (uint32_t blockSize = 2; blockSize <= n; blockSize <<= 1)
                {
                    for (uint32_t subBlockSize = blockSize; subBlockSize >= 2; subBlockSize >>= 1)
                    {
                        sortConstants.blockSize = blockSize;
                        sortConstants.subBlockSize = subBlockSize;
                        cmd->SetConstants(sortConstants);
                        cmd->PushConstants();
                        cmd->Dispatch(numGroups, 1, 1);

                        addComputeBarrier(sortKeyBuffer, sortKeySize);
                        addComputeBarrier(indirectBuffer, indirectSize);
                    }
                }
            };

            dispatchBitonicSort(m_indirectAlphaBlend[frame], m_sortKeysAlphaBlend[frame]);
            dispatchBitonicSort(m_indirectTransmission[frame], m_sortKeysTransmission[frame]);
        }

        auto addIndirectBarrier = [&](Buffer *buffer)
        {
            BufferBarrierInfo barrier{};
            barrier.buffer = buffer;
            barrier.stageMask = vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eDrawIndirect;
            barrier.accessMask = vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eIndirectCommandRead;
            barrier.size = static_cast<uint64_t>(m_indirectCapacity) * sizeof(vk::DrawIndexedIndirectCommand);
            barrier.offset = 0;
            cmd->BufferBarrier(barrier);
        };
        addIndirectBarrier(m_indirectOpaqueSS[frame]);
        addIndirectBarrier(m_indirectAlphaCutSS[frame]);
        addIndirectBarrier(m_indirectOpaqueDS[frame]);
        addIndirectBarrier(m_indirectAlphaCutDS[frame]);
        if (hasTransparentMeshes)
        {
            addIndirectBarrier(m_indirectAlphaBlend[frame]);
            addIndirectBarrier(m_indirectTransmission[frame]);
        }
        addIndirectBarrier(m_indirectSelected[frame]);

        countersBarrier.stageMask = vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eDrawIndirect;
        countersBarrier.accessMask = vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eIndirectCommandRead;
        cmd->BufferBarrier(countersBarrier);
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

    void Scene::FlushPendingGpuWork()
    {
        const bool rtSupport = Settings::Get<GlobalSettings>().ray_tracing_support;
        const bool anyDirty = m_geometryDirty || m_instancesDirty || m_materialDirty ||
                              m_texturesDirty || m_blasDirty || m_tlasDirty;
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
                UpdateDirtyMaterials();
                m_materialDirty = false;
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
                UpdateDirtyMaterials();
                m_materialDirty = false;
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
} // namespace pe
