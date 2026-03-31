#include "Scene/Scene.h"
#include "Scene/Material.h"
#include "Scene/ModelAsset.h"
#include "Scene/SelectionManager.h"
#include "API/AccelerationStructure.h"
#include "API/Buffer.h"
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

    Scene::Scene()
    {
        m_defaultSampler = Sampler::Create(Sampler::CreateInfoInit(), "defaultSampler");

        Camera *camera = new Camera();
        camera->SetName("Camera_" + std::to_string(ID::NextID()));
        NodeId *camNode = CreateNode(camera->GetName());
        AddComponentFlag(camNode, Component_Camera);
        camera->SetNodeId(camNode);
        m_cameras.push_back(camera);

        uint32_t swapchainImageCount = RHII.GetSwapchainImageCount();
        m_storages.resize(swapchainImageCount, nullptr);
        m_indirects.resize(swapchainImageCount, nullptr);

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
        for (Camera *cam : m_cameras)
            if (cam->GetNodeId() == node)
                return cam;
        return nullptr;
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

        // Register source
        int sourceIndex = static_cast<int>(m_sources.size());
        SceneSource source;
        source.filePath = model->GetFilePath();
        source.primitiveType = model->GetPrimitiveType();
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
    }

    SceneNodeHandle Scene::AddModelDeferred(ModelAsset *model)
    {
        m_models.insert(model->GetId(), model);

        int sourceIndex = static_cast<int>(m_sources.size());
        SceneSource source;
        source.filePath = model->GetFilePath();
        source.primitiveType = model->GetPrimitiveType();
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
            m_componentFlags[node->index] |= Component_GpuPending;
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
        m_componentFlags[synth->index] |= Component_GpuPending;
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

    void Scene::CullNodeBatch(uint32_t beginNode, uint32_t endNode, const Camera *camera, bool frustumCulling, DrawBatch &out) const
    {
        out.opaque.clear();
        out.alphaCut.clear();
        out.alphaBlend.clear();
        out.transmission.clear();

        if (!camera)
            return;

        const vec3 cameraPosition = camera->GetPosition();
        const int batchNodeCount = std::max(1, static_cast<int>(endNode - beginNode));
        const int secondaryEstimated = std::max(1, batchNodeCount / 8);
        out.opaque.reserve(batchNodeCount);
        out.alphaCut.reserve(secondaryEstimated);
        out.alphaBlend.reserve(secondaryEstimated);
        out.transmission.reserve(secondaryEstimated);

        for (uint32_t i = beginNode; i < endNode; i++)
        {
            int meshIdx = m_meshRefs[i];
            if (meshIdx < 0)
                continue;
            if (m_componentFlags[i] & Component_GpuPending)
                continue;

            const Mesh &mesh = m_meshes[meshIdx];
            if (mesh.indexCount == 0)
                continue;

            const AABB &worldBounds = m_nodeRuntime[i].worldAABB;

            bool cull = frustumCulling ? !camera->AABBInFrustum(worldBounds) : false;
            if (cull)
                continue;

            vec3 center = worldBounds.GetCenter();
            float distance = distance2(cameraPosition, center);

            bool doubleSided = mesh.material && mesh.material->doubleSided;
            switch (mesh.renderType)
            {
            case RenderType::Opaque:
                out.opaque.push_back(DrawInfo{m_nodeIds[i], distance, doubleSided});
                break;
            case RenderType::AlphaCut:
                out.alphaCut.push_back(DrawInfo{m_nodeIds[i], distance, doubleSided});
                break;
            case RenderType::AlphaBlend:
                out.alphaBlend.push_back(DrawInfo{m_nodeIds[i], distance, doubleSided});
                break;
            case RenderType::Transmission:
                out.transmission.push_back(DrawInfo{m_nodeIds[i], distance, doubleSided});
                break;
            }
        }
    }

    void Scene::UpdateUniformData()
    {
        uint32_t frame = RHII.GetFrameIndex();

        m_frameData.viewProjection = m_cameras[0]->GetViewProjection();
        m_frameData.previousViewProjection = m_cameras[0]->GetPreviousViewProjection();
        m_frameData.invView = m_cameras[0]->GetInvView();
        m_frameData.invProjection = m_cameras[0]->GetInvProjection();

        BufferRange range{};
        range.data = &m_frameData;
        range.size = sizeof(PerFrameData);
        range.offset = 0;
        m_storages[frame]->Copy(1, &range, true);

        // Collect visible indirect IDs from draw infos.
        // Order: opaque_SS | alphaCut_SS | opaque_DS | alphaCut_DS | transmission | alphaBlend
        m_visibleIndirectIds.clear();
        m_visibleIndirectIds.reserve(m_drawInfosOpaque.size() + m_drawInfosAlphaCut.size() + m_drawInfosAlphaBlend.size() + m_drawInfosTransmission.size());
        // single-sided opaque
        for (uint32_t k = 0; k < m_opaqueSingleSidedCount; k++)
            m_visibleIndirectIds.push_back(m_nodeRuntime[m_drawInfosOpaque[k].node->index].indirectIndex);
        // single-sided alphaCut
        for (uint32_t k = 0; k < m_alphaCutSingleSidedCount; k++)
            m_visibleIndirectIds.push_back(m_nodeRuntime[m_drawInfosAlphaCut[k].node->index].indirectIndex);
        // double-sided opaque
        for (uint32_t k = m_opaqueSingleSidedCount; k < static_cast<uint32_t>(m_drawInfosOpaque.size()); k++)
            m_visibleIndirectIds.push_back(m_nodeRuntime[m_drawInfosOpaque[k].node->index].indirectIndex);
        // double-sided alphaCut
        for (uint32_t k = m_alphaCutSingleSidedCount; k < static_cast<uint32_t>(m_drawInfosAlphaCut.size()); k++)
            m_visibleIndirectIds.push_back(m_nodeRuntime[m_drawInfosAlphaCut[k].node->index].indirectIndex);
        for (auto &drawInfo : m_drawInfosTransmission)
            m_visibleIndirectIds.push_back(m_nodeRuntime[drawInfo.node->index].indirectIndex);
        for (auto &drawInfo : m_drawInfosAlphaBlend)
            m_visibleIndirectIds.push_back(m_nodeRuntime[drawInfo.node->index].indirectIndex);

        range.data = m_visibleIndirectIds.data();
        range.size = m_visibleIndirectIds.size() * sizeof(uint32_t);
        range.offset = sizeof(PerFrameData);
        m_storages[frame]->Copy(1, &range, true);

        // Upload per-node GPU data for dirty uniforms
        for (uint32_t i = 0; i < GetNodeCount(); i++)
        {
            NodeRuntime &rt = m_nodeRuntime[i];
            if (!rt.dirtyUniforms[frame])
                continue;

            // Skip nodes pending GPU upload — their storage offsets aren't assigned yet
            if (m_componentFlags[i] & Component_GpuPending)
                continue;

            rt.dirtyUniforms[frame] = false; // clear regardless of mesh presence

            int meshIdx = m_meshRefs[i];
            if (meshIdx < 0)
                continue;

            const Mesh &mesh = m_meshes[meshIdx];
            if (mesh.indexCount == 0)
                continue;

            range.data = &rt.gpuData;
            range.size = sizeof(NodeGpuData);
            range.offset = rt.dataOffset;
            m_storages[frame]->Copy(1, &range, true);
        }
    }

    void Scene::UpdateIndirectData()
    {
        uint32_t frame = RHII.GetFrameIndex();

        uint32_t firstInstance = 0;
        auto EmitIndirect = [&](const std::vector<DrawInfo> &drawInfos)
        {
            for (auto &drawInfo : drawInfos)
            {
                auto &indirectCommand = m_indirectCommands[m_nodeRuntime[drawInfo.node->index].indirectIndex];
                indirectCommand.firstInstance = firstInstance;

                BufferRange range{};
                range.data = &indirectCommand;
                range.size = sizeof(vk::DrawIndexedIndirectCommand);
                range.offset = firstInstance * sizeof(vk::DrawIndexedIndirectCommand);
                m_indirects[frame]->Copy(1, &range, true);

                firstInstance++;
            }
        };
        // Order: opaque_SS | alphaCut_SS | opaque_DS | alphaCut_DS | transmission | alphaBlend
        auto EmitIndirectRange = [&](const std::vector<DrawInfo> &drawInfos, uint32_t begin, uint32_t end)
        {
            for (uint32_t k = begin; k < end; k++)
            {
                auto &indirectCommand = m_indirectCommands[m_nodeRuntime[drawInfos[k].node->index].indirectIndex];
                indirectCommand.firstInstance = firstInstance;

                BufferRange range{};
                range.data = &indirectCommand;
                range.size = sizeof(vk::DrawIndexedIndirectCommand);
                range.offset = firstInstance * sizeof(vk::DrawIndexedIndirectCommand);
                m_indirects[frame]->Copy(1, &range, true);

                firstInstance++;
            }
        };
        EmitIndirectRange(m_drawInfosOpaque, 0, m_opaqueSingleSidedCount);
        EmitIndirectRange(m_drawInfosAlphaCut, 0, m_alphaCutSingleSidedCount);
        EmitIndirectRange(m_drawInfosOpaque, m_opaqueSingleSidedCount, static_cast<uint32_t>(m_drawInfosOpaque.size()));
        EmitIndirectRange(m_drawInfosAlphaCut, m_alphaCutSingleSidedCount, static_cast<uint32_t>(m_drawInfosAlphaCut.size()));
        EmitIndirect(m_drawInfosTransmission);
        EmitIndirect(m_drawInfosAlphaBlend);
    }

    void Scene::UpdateGeometry()
    {
        const bool reserveMax = !m_hasDrawInfosReservation || m_drawInfosReservedForGeometryVersion != m_geometryVersion;
        ClearDrawInfos(reserveMax);
        if (reserveMax)
        {
            m_drawInfosReservedForGeometryVersion = m_geometryVersion;
            m_hasDrawInfosReservation = true;
        }

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
                for (size_t f = 0; f < rt.dirtyUniforms.size(); f++)
                    rt.dirtyUniforms[f] = true;
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
            PE_PROFILE_SCOPE("Frustum Culling");

            Camera *camera = m_cameras.empty() ? nullptr : m_cameras[0];
            bool frustumCulling = Settings::Get<GlobalSettings>().frustum_culling;
            static constexpr uint32_t kCullBatchSize = 128;

            const uint32_t nodeCount = GetNodeCount();
            const uint32_t numBatches = (nodeCount + kCullBatchSize - 1u) / kCullBatchSize;
            m_cullBatches.resize(numBatches);

            std::vector<std::shared_future<void>> futures;
            futures.reserve(numBatches);

            for (uint32_t batchIdx = 0; batchIdx < numBatches; batchIdx++)
            {
                const uint32_t beginNode = batchIdx * kCullBatchSize;
                const uint32_t endNode = std::min(beginNode + kCullBatchSize, nodeCount);
                DrawBatch &batchRef = m_cullBatches[batchIdx];
                futures.push_back(ThreadPool::Update.Enqueue(
                    [this, beginNode, endNode, camera, frustumCulling, &batchRef]()
                    {
                        CullNodeBatch(beginNode, endNode, camera, frustumCulling, batchRef);
                    }));
            }

            for (auto &future : futures)
                future.get();

            for (const DrawBatch &batch : m_cullBatches)
            {
                m_drawInfosOpaque.insert(m_drawInfosOpaque.end(), batch.opaque.begin(), batch.opaque.end());
                m_drawInfosAlphaCut.insert(m_drawInfosAlphaCut.end(), batch.alphaCut.begin(), batch.alphaCut.end());
                m_drawInfosAlphaBlend.insert(m_drawInfosAlphaBlend.end(), batch.alphaBlend.begin(), batch.alphaBlend.end());
                m_drawInfosTransmission.insert(m_drawInfosTransmission.end(), batch.transmission.begin(), batch.transmission.end());
            }
        }

        {
            PE_PROFILE_SCOPE("Sort Draw Infos");
            SortDrawInfos();
        }
        {
            PE_PROFILE_SCOPE("Update Uniforms");
            UpdateUniformData();
        }
        if (HasDrawInfo())
        {
            PE_PROFILE_SCOPE("Update Indirect");
            UpdateIndirectData();
        }
    }

    void Scene::SortDrawInfos()
    {
        auto partitionDS = [](std::vector<DrawInfo> &list) -> uint32_t
        {
            auto mid = std::stable_partition(list.begin(), list.end(), [](const DrawInfo &d)
                                             { return !d.doubleSided; });
            return static_cast<uint32_t>(mid - list.begin());
        };
        auto sortRange = [](std::vector<DrawInfo>::iterator begin, std::vector<DrawInfo>::iterator end, bool frontToBack)
        {
            if (frontToBack)
                std::sort(begin, end, [](const DrawInfo &a, const DrawInfo &b)
                          { return a.distance < b.distance; });
            else
                std::sort(begin, end, [](const DrawInfo &a, const DrawInfo &b)
                          { return a.distance > b.distance; });
        };

        m_opaqueSingleSidedCount = partitionDS(m_drawInfosOpaque);
        sortRange(m_drawInfosOpaque.begin(), m_drawInfosOpaque.begin() + m_opaqueSingleSidedCount, true);
        sortRange(m_drawInfosOpaque.begin() + m_opaqueSingleSidedCount, m_drawInfosOpaque.end(), true);

        m_alphaCutSingleSidedCount = partitionDS(m_drawInfosAlphaCut);
        sortRange(m_drawInfosAlphaCut.begin(), m_drawInfosAlphaCut.begin() + m_alphaCutSingleSidedCount, true);
        sortRange(m_drawInfosAlphaCut.begin() + m_alphaCutSingleSidedCount, m_drawInfosAlphaCut.end(), true);

        std::sort(m_drawInfosAlphaBlend.begin(), m_drawInfosAlphaBlend.end(), [](const DrawInfo &a, const DrawInfo &b)
                  { return a.distance > b.distance; });
        std::sort(m_drawInfosTransmission.begin(), m_drawInfosTransmission.end(), [](const DrawInfo &a, const DrawInfo &b)
                  { return a.distance > b.distance; });
    }

    void Scene::ClearDrawInfos(bool reserveMax)
    {
        m_drawInfosOpaque.clear();
        m_drawInfosAlphaCut.clear();
        m_drawInfosAlphaBlend.clear();
        m_drawInfosTransmission.clear();
        m_opaqueSingleSidedCount = 0;
        m_alphaCutSingleSidedCount = 0;

        if (reserveMax)
        {
            uint32_t maxOpaque = 0;
            uint32_t maxAlphaCut = 0;
            uint32_t maxAlphaBlend = 0;
            uint32_t maxTransmission = 0;

            for (uint32_t i = 0; i < GetNodeCount(); i++)
            {
                int meshIdx = m_meshRefs[i];
                if (meshIdx < 0)
                    continue;

                const Mesh &mesh = m_meshes[meshIdx];
                if (mesh.indexCount == 0)
                    continue;

                switch (mesh.renderType)
                {
                case RenderType::Opaque:
                    maxOpaque++;
                    break;
                case RenderType::AlphaCut:
                    maxAlphaCut++;
                    break;
                case RenderType::AlphaBlend:
                    maxAlphaBlend++;
                    break;
                case RenderType::Transmission:
                    maxTransmission++;
                    break;
                }
            }

            m_drawInfosOpaque.reserve(maxOpaque);
            m_drawInfosAlphaCut.reserve(maxAlphaCut);
            m_drawInfosAlphaBlend.reserve(maxAlphaBlend);
            m_drawInfosTransmission.reserve(maxTransmission);
        }
    }

    void Scene::FlushPendingGpuWork()
    {
        if (!m_geometryDirty && !m_materialDirty && !m_texturesDirty)
            return;

        if (m_geometryDirty)
        {
            // Clear GpuPending flag BEFORE upload so these nodes are included
            for (uint32_t i = 0; i < GetNodeCount(); i++)
                m_componentFlags[i] &= ~Component_GpuPending;

            UpdateGeometryBuffers();

            m_geometryDirty = false;
            // Geometry rebuild includes material table and image views
            m_materialDirty = false;
            m_texturesDirty = false;
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
    }
} // namespace pe
