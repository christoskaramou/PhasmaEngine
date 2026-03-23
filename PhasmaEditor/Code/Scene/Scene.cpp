#include "Scene/Scene.h"
#include "Scene/Primitives.h"
#include "API/AccelerationStructure.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/Vertex.h"
#include "Camera/Camera.h"
#include "Particles/ParticleManager.h"
#include "RenderPasses/AabbsPass.h"
#include "RenderPasses/DepthPass.h"
#include "RenderPasses/GbufferPass.h"
#include "RenderPasses/LightPass.h"
#include "RenderPasses/RayTracingPass.h"
#include "RenderPasses/ShadowPass.h"
#include "Scene/ModelAsset.h"
#include "Scene/SelectionManager.h"
#include "Systems/LightSystem.h"
#include "Systems/RendererSystem.h"

#define _SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING
#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/ostreamwrapper.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace pe
{
    namespace
    {
        constexpr float kInfiniteAttenuationDistanceSentinel = -1.0f;

        void EncodeMaterialFactorsForPersistence(RenderType renderType, mat4 &f0, mat4 &f1)
        {
            (void)f0;

            if (renderType == RenderType::Transmission && std::isinf(f1[0][1]))
                f1[0][1] = kInfiniteAttenuationDistanceSentinel;
        }

        void DecodeMaterialFactorsFromPersistence(RenderType renderType, mat4 &f0, mat4 &f1)
        {
            (void)f0;

            // Older scene files serialized +inf attenuation distance as 0.0 via SafeFloat(),
            // which makes transmission materials attenuate to black when restored.
            if (renderType == RenderType::Transmission && f1[0][1] <= 0.0f)
                f1[0][1] = std::numeric_limits<float>::infinity();
        }
    } // namespace

    std::vector<uint32_t> Scene::s_aabbIndices = {
        0, 1, 1, 2, 2, 3, 3, 0,
        4, 5, 5, 6, 6, 7, 7, 4,
        0, 4, 1, 5, 2, 6, 3, 7};

    Scene::Scene()
    {
        m_defaultSampler = Sampler::Create(Sampler::CreateInfoInit(), "defaultSampler");

        Camera *camera = new Camera();
        camera->SetName("Camera_" + std::to_string(ID::NextID()));
        m_cameras.push_back(camera);

        uint32_t swapchainImageCount = RHII.GetSwapchainImageCount();
        m_storages.resize(swapchainImageCount, nullptr);
        m_indirects.resize(swapchainImageCount, nullptr);

        m_particleManager = new ParticleManager();
        m_particleManager->Init(); // disable until it is done
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

    void Scene::Update()
    {
        for (auto *camera : m_cameras)
            camera->Update();

        UpdateGeometry();
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
        CreateMeshConstants(cmd);
        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        queue->ReturnCommandBuffer(cmd);
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
            mesh.textureMask = mi->textureMask;
            for (int k = 0; k < 5; k++)
            {
                mesh.images[k] = mi->images[k];
                mesh.samplers[k] = mi->samplers[k];
            }
            mesh.materialFactors[0] = mi->materialFactors[0];
            mesh.materialFactors[1] = mi->materialFactors[1];

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

    Camera *Scene::AddCamera()
    {
        Camera *camera = new Camera();
        camera->SetName("Camera_" + std::to_string(ID::NextID()));
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
            if (SelectionManager::Instance().GetSelectionType() == SelectionType::Camera)
            {
                int index = static_cast<int>(std::distance(m_cameras.begin(), it));
                if (SelectionManager::Instance().GetSelectedCameraIndex() == index)
                    SelectionManager::Instance().ClearSelection();
            }

            delete *it;
            m_cameras.erase(it);
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

    void Scene::UploadBuffers(CommandBuffer *cmd)
    {
        // Mesh offsets are already set by AssimpLoader/AddModel — no per-model fixup needed

        DestroyBuffers();
        CreateGeometryBuffer();
        CopyIndices(cmd);
        CopyVertices(cmd);
        CreateStorageBuffers();
        MarkUniformsDirty();
        CreateIndirectBuffers(cmd);
        UpdateImageViews();
        CreateMeshConstants(cmd);
        if (Settings::Get<GlobalSettings>().ray_tracing_support)
            BuildAccelerationStructures(cmd);
    }

    void Scene::CreateGeometryBuffer()
    {
        // Count drawable meshes (nodes with valid mesh refs and non-zero indices)
        m_meshCount = 0;
        for (uint32_t i = 0; i < GetNodeCount(); i++)
        {
            int meshIdx = m_meshRefs[i];
            if (meshIdx < 0)
                continue;
            if (m_meshes[meshIdx].indexCount == 0)
                continue;
            m_meshCount++;
        }

        m_indicesCount = static_cast<uint32_t>(m_indexStore.size());
        m_verticesCount = static_cast<uint32_t>(m_vertexStore.size());
        m_positionsCount = static_cast<uint32_t>(m_positionUvStore.size());
        m_aabbVerticesCount = static_cast<uint32_t>(m_aabbVertexStore.size());

        m_aabbIndicesOffset = m_indicesCount * sizeof(uint32_t);
        m_verticesOffset = m_aabbIndicesOffset + s_aabbIndices.size() * sizeof(uint32_t);
        m_positionsOffset = m_verticesOffset + m_verticesCount * sizeof(Vertex);
        m_aabbVerticesOffset = m_positionsOffset + m_positionsCount * sizeof(PositionUvVertex);

        m_buffer = Buffer::Create(
            m_aabbVerticesOffset + sizeof(AabbVertex) * m_aabbVerticesCount,
            vk::BufferUsageFlagBits2::eTransferDst | vk::BufferUsageFlagBits2::eIndexBuffer | vk::BufferUsageFlagBits2::eVertexBuffer | vk::BufferUsageFlagBits2::eStorageBuffer | vk::BufferUsageFlagBits2::eShaderDeviceAddress | vk::BufferUsageFlagBits2::eAccelerationStructureBuildInputReadOnlyKHR,
            VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
            "combined_Geometry_buffer");
    }

    void Scene::CopyIndices(CommandBuffer *cmd)
    {
        // Single contiguous copy from Scene's index store
        if (m_indicesCount > 0)
        {
            cmd->CopyBufferStaged(m_buffer, m_indexStore.data(), m_indicesCount * sizeof(uint32_t), 0);

            BufferBarrierInfo indexBarrierInfo{};
            indexBarrierInfo.buffer = m_buffer;
            indexBarrierInfo.stageMask = vk::PipelineStageFlagBits2::eVertexInput;
            indexBarrierInfo.accessMask = vk::AccessFlagBits2::eIndexRead;
            indexBarrierInfo.size = m_indicesCount * sizeof(uint32_t);
            indexBarrierInfo.offset = 0;
            cmd->BufferBarrier(indexBarrierInfo);
        }

        cmd->CopyBufferStaged(m_buffer, s_aabbIndices.data(), s_aabbIndices.size() * sizeof(uint32_t), m_aabbIndicesOffset);

        if (s_aabbIndices.size() > 0)
        {
            BufferBarrierInfo aabbIndexBarrierInfo{};
            aabbIndexBarrierInfo.buffer = m_buffer;
            aabbIndexBarrierInfo.stageMask = vk::PipelineStageFlagBits2::eVertexInput;
            aabbIndexBarrierInfo.accessMask = vk::AccessFlagBits2::eIndexRead;
            aabbIndexBarrierInfo.size = s_aabbIndices.size() * sizeof(uint32_t);
            aabbIndexBarrierInfo.offset = m_aabbIndicesOffset;
            cmd->BufferBarrier(aabbIndexBarrierInfo);
        }
    }

    void Scene::CopyVertices(CommandBuffer *cmd)
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        auto &progress = gSettings.loading.current;
        auto &total = gSettings.loading.total;

        total = m_verticesCount + m_positionsCount + m_aabbVerticesCount;
        progress = 0;
        gSettings.loading.SetName("Uploading to GPU");

        // Single contiguous copy for each vertex type
        if (m_verticesCount > 0)
        {
            cmd->CopyBufferStaged(m_buffer, m_vertexStore.data(), m_verticesCount * sizeof(Vertex), m_verticesOffset);
            progress += m_verticesCount;

            BufferBarrierInfo vertexBarrierInfo{};
            vertexBarrierInfo.buffer = m_buffer;
            vertexBarrierInfo.stageMask = vk::PipelineStageFlagBits2::eVertexInput;
            vertexBarrierInfo.accessMask = vk::AccessFlagBits2::eVertexAttributeRead;
            vertexBarrierInfo.size = m_verticesCount * sizeof(Vertex);
            vertexBarrierInfo.offset = m_verticesOffset;
            cmd->BufferBarrier(vertexBarrierInfo);
        }

        if (m_positionsCount > 0)
        {
            cmd->CopyBufferStaged(m_buffer, m_positionUvStore.data(), m_positionsCount * sizeof(PositionUvVertex), m_positionsOffset);
            progress += m_positionsCount;

            BufferBarrierInfo posVertexBarrierInfo{};
            posVertexBarrierInfo.buffer = m_buffer;
            posVertexBarrierInfo.stageMask = vk::PipelineStageFlagBits2::eVertexInput;
            posVertexBarrierInfo.accessMask = vk::AccessFlagBits2::eVertexAttributeRead;
            posVertexBarrierInfo.size = m_positionsCount * sizeof(PositionUvVertex);
            posVertexBarrierInfo.offset = m_positionsOffset;
            cmd->BufferBarrier(posVertexBarrierInfo);
        }

        if (m_aabbVerticesCount > 0)
        {
            cmd->CopyBufferStaged(m_buffer, m_aabbVertexStore.data(), m_aabbVerticesCount * sizeof(AabbVertex), m_aabbVerticesOffset);
            progress += m_aabbVerticesCount;

            BufferBarrierInfo aabbVertexBarrierInfo{};
            aabbVertexBarrierInfo.buffer = m_buffer;
            aabbVertexBarrierInfo.stageMask = vk::PipelineStageFlagBits2::eVertexInput;
            aabbVertexBarrierInfo.accessMask = vk::AccessFlagBits2::eVertexAttributeRead;
            aabbVertexBarrierInfo.size = m_aabbVerticesCount * sizeof(AabbVertex);
            aabbVertexBarrierInfo.offset = m_aabbVerticesOffset;
            cmd->BufferBarrier(aabbVertexBarrierInfo);
        }
    }

    void Scene::CreateStorageBuffers()
    {
        size_t storageSize = sizeof(PerFrameData);
        storageSize += RHII.AlignStorageAs(m_meshCount * sizeof(uint32_t), 64);

        for (uint32_t i = 0; i < GetNodeCount(); i++)
        {
            int meshIdx = m_meshRefs[i];
            if (meshIdx < 0 || m_meshes[meshIdx].indexCount == 0)
                continue;

            m_nodeRuntime[i].dataOffset = storageSize;
            storageSize += sizeof(NodeGpuData);
        }

        uint32_t i = 0;
        for (auto &storage : m_storages)
        {
            storage = Buffer::Create(
                storageSize,
                vk::BufferUsageFlagBits2::eStorageBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                "storage_Geometry_buffer_" + std::to_string(i++));
        }
    }

    void Scene::MarkUniformsDirty()
    {
        for (uint32_t i = 0; i < GetNodeCount(); i++)
        {
            for (size_t f = 0; f < m_nodeRuntime[i].dirtyUniforms.size(); f++)
                m_nodeRuntime[i].dirtyUniforms[f] = true;
        }
    }

    void Scene::CreateIndirectBuffers(CommandBuffer *cmd)
    {
        uint32_t indirectCount = 0;
        m_indirectCommands.clear();
        m_indirectCommands.reserve(m_meshCount);

        for (uint32_t i = 0; i < GetNodeCount(); i++)
        {
            int meshIdx = m_meshRefs[i];
            if (meshIdx < 0)
                continue;

            const Mesh &mesh = m_meshes[meshIdx];
            if (mesh.indexCount == 0)
                continue;

            m_nodeRuntime[i].indirectIndex = indirectCount;

            vk::DrawIndexedIndirectCommand indirectCommand{};
            indirectCommand.indexCount = mesh.indexCount;
            indirectCommand.instanceCount = 1;
            indirectCommand.firstIndex = mesh.indexOffset;
            indirectCommand.vertexOffset = mesh.vertexOffset;
            indirectCommand.firstInstance = indirectCount;
            m_indirectCommands.push_back(indirectCommand);

            indirectCount++;
        }

        PE_ERROR_IF(indirectCount != m_meshCount, "Scene::UploadBuffers: Indirect count mismatch!");

        m_indirectAll = Buffer::Create(
            indirectCount * sizeof(vk::DrawIndexedIndirectCommand),
            vk::BufferUsageFlagBits2::eIndirectBuffer | vk::BufferUsageFlagBits2::eTransferDst,
            VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
            "indirect_Geometry_buffer_all");
        cmd->CopyBufferStaged(m_indirectAll, m_indirectCommands.data(), m_indirectCommands.size() * sizeof(vk::DrawIndexedIndirectCommand), 0);

        if (indirectCount > 0)
        {
            BufferBarrierInfo indirectBarrierInfo{};
            indirectBarrierInfo.buffer = m_indirectAll;
            indirectBarrierInfo.stageMask = vk::PipelineStageFlagBits2::eDrawIndirect;
            indirectBarrierInfo.accessMask = vk::AccessFlagBits2::eIndirectCommandRead;
            indirectBarrierInfo.size = indirectCount * sizeof(vk::DrawIndexedIndirectCommand);
            indirectBarrierInfo.offset = 0;
            cmd->BufferBarrier(indirectBarrierInfo);
        }

        uint32_t i = 0;
        for (auto &indirect : m_indirects)
        {
            indirect = Buffer::Create(
                indirectCount * sizeof(vk::DrawIndexedIndirectCommand),
                vk::BufferUsageFlagBits2::eIndirectBuffer | vk::BufferUsageFlagBits2::eTransferDst,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                "indirect_Geometry_buffer_" + std::to_string(i++));
        }
    }

    void Scene::UpdateImageViews()
    {
        m_imageViews.clear();
        m_imageViews.reserve(m_imageStore.size());

        const auto &defaults = ModelAsset::GetDefaultResources();
        OrderedMap<Image *, uint32_t> imagesMap{};

        // Build unique image view list from Scene's image store
        for (const ResourceHandle<Image> &image : m_imageStore)
        {
            auto insertResult = imagesMap.insert(image.get(), static_cast<uint32_t>(m_imageViews.size()));
            if (insertResult.first)
            {
                ImageView *srv = image->GetSRV();
                PE_ERROR_IF(!srv, "UpdateImageViews: image '%s' has no SRV", image->GetName().c_str());
                m_imageViews.push_back(srv ? srv : defaults.white->GetSRV());
            }
        }

        // Map each mesh's texture slots to image view indices
        for (int meshIndex = 0; meshIndex < static_cast<int>(m_meshes.size()); meshIndex++)
        {
            const Mesh &mesh = m_meshes[meshIndex];
            MeshRuntime &rt = m_meshRuntimes[meshIndex];

            for (int k = 0; k < 5; k++)
            {
                Image *image = mesh.images[k].get();
                bool isDefault = (image == defaults.black || image == defaults.white || image == defaults.normal);

                if (image && !isDefault)
                    rt.imageViewIndices[k] = imagesMap[image];
                else
                    rt.imageViewIndices[k] = 0xFFFFFFFF;
            }
        }

        m_geometryVersion++;
    }

    void Scene::CreateMeshConstants(CommandBuffer *cmd)
    {
        Buffer::Destroy(m_meshConstants);
        m_meshConstants = Buffer::Create(
            m_meshCount * sizeof(Mesh_Constants),
            vk::BufferUsageFlagBits2::eStorageBuffer | vk::BufferUsageFlagBits2::eTransferDst,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            "Scene_meshConstants");

        size_t offset = 0;
        m_meshConstants->Map();
        for (uint32_t i = 0; i < GetNodeCount(); i++)
        {
            int meshIdx = m_meshRefs[i];
            if (meshIdx < 0)
                continue;

            const Mesh &mesh = m_meshes[meshIdx];
            if (mesh.indexCount == 0)
                continue;

            const MeshRuntime &meshRt = m_meshRuntimes[meshIdx];

            Mesh_Constants constants{};
            constants.alphaCut = (mesh.renderType == RenderType::AlphaCut) ? mesh.materialFactors[0][2][2] : 0.0f;
            constants.meshDataOffset = static_cast<uint32_t>(m_nodeRuntime[i].dataOffset);
            constants.textureMask = mesh.textureMask;
            for (int k = 0; k < 5; k++)
                constants.meshImageIndex[k] = meshRt.imageViewIndices[k];

            BufferRange range{};
            range.data = &constants;
            range.offset = offset;
            range.size = sizeof(Mesh_Constants);
            m_meshConstants->Copy(1, &range, true);

            offset += sizeof(Mesh_Constants);
        }
        m_meshConstants->Flush();
        m_meshConstants->Unmap();
    }

    Scene::DrawBatch Scene::CullNodeBatch(uint32_t beginNode, uint32_t endNode, const Camera *camera, bool frustumCulling) const
    {
        DrawBatch batch{};
        if (!camera)
            return batch;

        const vec3 cameraPosition = camera->GetPosition();
        const int batchNodeCount = std::max(1, static_cast<int>(endNode - beginNode));
        const int secondaryEstimated = std::max(1, batchNodeCount / 8);
        batch.opaque.reserve(batchNodeCount);
        batch.alphaCut.reserve(secondaryEstimated);
        batch.alphaBlend.reserve(secondaryEstimated);
        batch.transmission.reserve(secondaryEstimated);

        for (uint32_t i = beginNode; i < endNode; i++)
        {
            int meshIdx = m_meshRefs[i];
            if (meshIdx < 0)
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

            switch (mesh.renderType)
            {
            case RenderType::Opaque:
                batch.opaque.push_back(DrawInfo{m_nodeIds[i], distance});
                break;
            case RenderType::AlphaCut:
                batch.alphaCut.push_back(DrawInfo{m_nodeIds[i], distance});
                break;
            case RenderType::AlphaBlend:
                batch.alphaBlend.push_back(DrawInfo{m_nodeIds[i], distance});
                break;
            case RenderType::Transmission:
                batch.transmission.push_back(DrawInfo{m_nodeIds[i], distance});
                break;
            }
        }

        return batch;
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

        // Collect visible indirect IDs from draw infos
        m_visibleIndirectIds.clear();
        m_visibleIndirectIds.reserve(m_drawInfosOpaque.size() + m_drawInfosAlphaCut.size() + m_drawInfosAlphaBlend.size() + m_drawInfosTransmission.size());
        for (auto &drawInfo : m_drawInfosOpaque)
            m_visibleIndirectIds.push_back(m_nodeRuntime[drawInfo.node->index].indirectIndex);
        for (auto &drawInfo : m_drawInfosAlphaCut)
            m_visibleIndirectIds.push_back(m_nodeRuntime[drawInfo.node->index].indirectIndex);
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

            rt.dirtyUniforms[frame] = false; // clear regardless of mesh presence

            int meshIdx = m_meshRefs[i];
            if (meshIdx < 0)
                continue;

            const Mesh &mesh = m_meshes[meshIdx];
            if (mesh.indexCount == 0)
                continue;

            // Sync material factors from mesh to GPU data
            rt.gpuData.materialFactors[0] = mesh.materialFactors[0];
            rt.gpuData.materialFactors[1] = mesh.materialFactors[1];

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
        EmitIndirect(m_drawInfosOpaque);
        EmitIndirect(m_drawInfosAlphaCut);
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

        UpdateNodeMatrices();

        Camera *camera = m_cameras.empty() ? nullptr : m_cameras[0];
        bool frustumCulling = Settings::Get<GlobalSettings>().frustum_culling;
        static constexpr uint32_t kCullBatchSize = 128;

        const uint32_t nodeCount = GetNodeCount();
        std::vector<std::shared_future<DrawBatch>> futures;
        futures.reserve((nodeCount + kCullBatchSize - 1u) / kCullBatchSize);

        for (uint32_t beginNode = 0; beginNode < nodeCount; beginNode += kCullBatchSize)
        {
            const uint32_t endNode = std::min(beginNode + kCullBatchSize, nodeCount);
            futures.push_back(ThreadPool::Update.Enqueue(&Scene::CullNodeBatch, this, beginNode, endNode, camera, frustumCulling));
        }

        for (auto &future : futures)
        {
            const DrawBatch &batch = future.get();
            m_drawInfosOpaque.insert(m_drawInfosOpaque.end(), batch.opaque.begin(), batch.opaque.end());
            m_drawInfosAlphaCut.insert(m_drawInfosAlphaCut.end(), batch.alphaCut.begin(), batch.alphaCut.end());
            m_drawInfosAlphaBlend.insert(m_drawInfosAlphaBlend.end(), batch.alphaBlend.begin(), batch.alphaBlend.end());
            m_drawInfosTransmission.insert(m_drawInfosTransmission.end(), batch.transmission.begin(), batch.transmission.end());
        }

        SortDrawInfos();
        UpdateUniformData();
        if (HasDrawInfo())
        {
            UpdateIndirectData();
        }
    }

    void Scene::SortDrawInfos()
    {
        std::sort(m_drawInfosOpaque.begin(), m_drawInfosOpaque.end(), [](const DrawInfo &a, const DrawInfo &b)
                  { return a.distance < b.distance; });
        std::sort(m_drawInfosAlphaCut.begin(), m_drawInfosAlphaCut.end(), [](const DrawInfo &a, const DrawInfo &b)
                  { return a.distance < b.distance; });
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

    void Scene::DestroyBuffers()
    {
        if (m_buffer)
        {
            RHII.AddToDeletionQueue([b = m_buffer]()
                                    { Buffer* buf = b; Buffer::Destroy(buf); });
            m_buffer = nullptr;
        }

        for (auto &storage : m_storages)
        {
            if (storage)
            {
                RHII.AddToDeletionQueue([b = storage]()
                                        { Buffer* buf = b; Buffer::Destroy(buf); });
                storage = nullptr;
            }
        }

        for (auto &indirect : m_indirects)
        {
            if (indirect)
            {
                RHII.AddToDeletionQueue([b = indirect]()
                                        { Buffer* buf = b; Buffer::Destroy(buf); });
                indirect = nullptr;
            }
        }

        if (m_indirectAll)
        {
            RHII.AddToDeletionQueue([b = m_indirectAll]()
                                    { Buffer* buf = b; Buffer::Destroy(buf); });
            m_indirectAll = nullptr;
        }
    }

    void Scene::BuildAccelerationStructures(CommandBuffer *cmd)
    {
        // Cleanup old resources
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

        if (!GetBuffer())
            return;

        vk::MemoryBarrier2 barrier{};
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
        barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR | vk::PipelineStageFlagBits2::eTransfer;
        barrier.dstAccessMask = vk::AccessFlagBits2::eAccelerationStructureReadKHR | vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eTransferRead;
        cmd->MemoryBarrier(barrier);

        vk::DeviceAddress bufferAddress = GetBuffer()->GetDeviceAddress();

        // --- Pass 1: Calculate sizes for BLAS (one per unique mesh) ---
        vk::DeviceSize totalBlasSize = 0;
        vk::DeviceSize maxScratchSize = 0;

        struct BlasBuildReq
        {
            vk::AccelerationStructureGeometryKHR geometry;
            vk::AccelerationStructureBuildRangeInfoKHR range;
            vk::AccelerationStructureBuildSizesInfoKHR sizeInfo;
            int meshIndex;
            AccelerationStructure *createdBlas = nullptr;
        };
        std::vector<BlasBuildReq> buildReqs;
        buildReqs.reserve(m_meshes.size());

        static constexpr vk::BuildAccelerationStructureFlagsKHR kBlasFlags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;

        for (int meshIndex = 0; meshIndex < static_cast<int>(m_meshes.size()); meshIndex++)
        {
            const Mesh &mesh = m_meshes[meshIndex];
            if (mesh.indexCount == 0)
                continue;

            vk::AccelerationStructureGeometryKHR geometry{};
            geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
            geometry.geometry.triangles.vertexFormat = vk::Format::eR32G32B32Sfloat;
            geometry.geometry.triangles.vertexData.deviceAddress = bufferAddress + m_verticesOffset + mesh.vertexOffset * sizeof(Vertex);
            geometry.geometry.triangles.vertexStride = sizeof(Vertex);
            geometry.geometry.triangles.maxVertex = mesh.vertexCount ? mesh.vertexCount - 1 : 0;
            geometry.geometry.triangles.indexType = vk::IndexType::eUint32;
            geometry.geometry.triangles.indexData.deviceAddress = bufferAddress + mesh.indexOffset * sizeof(uint32_t);
            if (mesh.renderType == RenderType::AlphaCut ||
                mesh.renderType == RenderType::AlphaBlend ||
                mesh.renderType == RenderType::Transmission)
            {
                geometry.flags = vk::GeometryFlagBitsKHR::eNoDuplicateAnyHitInvocation;
            }
            else
            {
                geometry.flags = vk::GeometryFlagBitsKHR::eOpaque;
            }

            vk::AccelerationStructureBuildRangeInfoKHR rangeInfo{};
            rangeInfo.primitiveCount = mesh.indexCount / 3;
            rangeInfo.primitiveOffset = 0;
            rangeInfo.firstVertex = 0;
            rangeInfo.transformOffset = 0;

            auto sizeInfo = AccelerationStructure::GetBuildSizes(
                {geometry},
                {rangeInfo.primitiveCount},
                vk::AccelerationStructureTypeKHR::eBottomLevel,
                kBlasFlags,
                vk::AccelerationStructureBuildTypeKHR::eDevice);

            totalBlasSize = RHII.Align(totalBlasSize + sizeInfo.accelerationStructureSize, 256);
            maxScratchSize = std::max(maxScratchSize, sizeInfo.buildScratchSize);

            buildReqs.push_back({geometry, rangeInfo, sizeInfo, meshIndex});
        }

        // Collect instances (one per node with a mesh)
        struct InstanceReq
        {
            AccelerationStructure *blas;
            int meshIndex;
            uint32_t nodeIndex;
            mat4 transform;
        };
        std::vector<InstanceReq> instanceReqs;
        instanceReqs.reserve(m_meshCount);

        for (uint32_t i = 0; i < GetNodeCount(); i++)
        {
            int meshIdx = m_meshRefs[i];
            if (meshIdx < 0)
                continue;
            if (m_meshes[meshIdx].indexCount == 0)
                continue;

            instanceReqs.push_back({nullptr, meshIdx, i, m_nodeRuntime[i].gpuData.worldMatrix});
        }

        PE_ERROR_IF(instanceReqs.size() != m_meshCount, "BuildAccelerationStructures instanceCount mismatch!");

        static constexpr vk::BuildAccelerationStructureFlagsKHR kTlasFlags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;

        vk::AccelerationStructureGeometryKHR tlasGeom{};
        tlasGeom.geometryType = vk::GeometryTypeKHR::eInstances;
        vk::AccelerationStructureGeometryInstancesDataKHR instData{};
        instData.arrayOfPointers = VK_FALSE;
        instData.data.deviceAddress = 0;
        tlasGeom.geometry.instances = instData;

        auto tlasSizes = AccelerationStructure::GetBuildSizes(
            {tlasGeom},
            {m_meshCount},
            vk::AccelerationStructureTypeKHR::eTopLevel,
            kTlasFlags,
            vk::AccelerationStructureBuildTypeKHR::eDevice);

        maxScratchSize = std::max(maxScratchSize, tlasSizes.buildScratchSize);

        // --- Allocation ---
        m_blasMergedBuffer = Buffer::Create(
            totalBlasSize,
            vk::BufferUsageFlagBits2::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits2::eShaderDeviceAddress,
            VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
            "BLAS_Merged_Buffer");

        vk::PhysicalDeviceAccelerationStructurePropertiesKHR asProps{};
        asProps.pNext = nullptr;
        vk::PhysicalDeviceProperties2 props{};
        props.pNext = &asProps;
        RHII.GetGpu().getProperties2(&props);
        auto scratchAlign = asProps.minAccelerationStructureScratchOffsetAlignment;
        m_scratchBuffer = Buffer::Create(
            RHII.Align(maxScratchSize, scratchAlign),
            vk::BufferUsageFlagBits2::eStorageBuffer | vk::BufferUsageFlagBits2::eShaderDeviceAddress,
            VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
            "AS_Scratch_Buffer");

        // --- Pass 2: Build BLAS ---
        vk::DeviceSize currentOffset = 0;
        std::unordered_map<int, AccelerationStructure *> blasByMesh;
        blasByMesh.reserve(buildReqs.size());

        for (auto &req : buildReqs)
        {
            currentOffset = RHII.Align(currentOffset, 256);

            std::string name = "BLAS_mesh" + std::to_string(req.meshIndex);
            req.createdBlas = new AccelerationStructure(name, m_blasMergedBuffer, currentOffset);
            req.createdBlas->BuildBLAS(cmd, {req.geometry}, {req.range}, {req.range.primitiveCount}, kBlasFlags, m_scratchBuffer->GetDeviceAddress());
            m_blases.push_back(req.createdBlas);
            blasByMesh[req.meshIndex] = req.createdBlas;

            currentOffset += req.sizeInfo.accelerationStructureSize;
        }

        // --- Match Instances to BLAS ---
        std::vector<InstanceReq> matchedInstances;
        matchedInstances.reserve(instanceReqs.size());
        for (auto &req : instanceReqs)
        {
            auto found = blasByMesh.find(req.meshIndex);
            if (found == blasByMesh.end())
                continue;
            req.blas = found->second;
            matchedInstances.push_back(req);
        }
        instanceReqs.swap(matchedInstances);

        // --- Create Instance Buffer ---
        m_instanceBuffer = Buffer::Create(
            std::max((size_t)1, (size_t)m_meshCount) * sizeof(vk::AccelerationStructureInstanceKHR),
            vk::BufferUsageFlagBits2::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits2::eShaderDeviceAddress | vk::BufferUsageFlagBits2::eTransferDst,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            "TLAS_Instance_Buffer");

        m_instanceBuffer->Map();
        auto *gpuInstances = (vk::AccelerationStructureInstanceKHR *)m_instanceBuffer->Data();

        for (size_t i = 0; i < instanceReqs.size(); i++)
        {
            auto &req = instanceReqs[i];

            vk::TransformMatrixKHR transformMatrix;
            mat4 &t = req.transform;
            transformMatrix.matrix[0][0] = t[0][0];
            transformMatrix.matrix[0][1] = t[1][0];
            transformMatrix.matrix[0][2] = t[2][0];
            transformMatrix.matrix[0][3] = t[3][0];
            transformMatrix.matrix[1][0] = t[0][1];
            transformMatrix.matrix[1][1] = t[1][1];
            transformMatrix.matrix[1][2] = t[2][1];
            transformMatrix.matrix[1][3] = t[3][1];
            transformMatrix.matrix[2][0] = t[0][2];
            transformMatrix.matrix[2][1] = t[1][2];
            transformMatrix.matrix[2][2] = t[2][2];
            transformMatrix.matrix[2][3] = t[3][2];

            const Mesh &mesh = m_meshes[req.meshIndex];
            bool isTransparent = (mesh.renderType == RenderType::AlphaBlend ||
                                  mesh.renderType == RenderType::Transmission ||
                                  mesh.renderType == RenderType::AlphaCut);

            gpuInstances[i].transform = transformMatrix;
            gpuInstances[i].instanceCustomIndex = static_cast<uint32_t>(i);
            gpuInstances[i].mask = isTransparent ? 0x80 : 0x01;
            gpuInstances[i].instanceShaderBindingTableRecordOffset = 0;
            gpuInstances[i].flags = static_cast<VkGeometryInstanceFlagBitsKHR>(vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable);
            gpuInstances[i].accelerationStructureReference = req.blas->GetDeviceAddress();

            m_nodeRuntime[req.nodeIndex].instanceIndex = static_cast<int>(i);
        }
        m_instanceBuffer->Flush();
        m_instanceBuffer->Unmap();

        // --- TLAS Build ---
        m_tlas = AccelerationStructure::Create("TLAS", nullptr, 0);
        m_tlas->BuildTLAS(cmd, m_meshCount, m_instanceBuffer, kTlasFlags, m_scratchBuffer->GetDeviceAddress());

        // --- Create MeshInfoGPU Buffer ---
        struct MeshInfoGPU
        {
            uint32_t indexOffset;
            uint32_t vertexOffset;
            uint32_t positionsOffset;
            uint32_t renderType;
            int32_t textures[5];
        };

        Buffer::Destroy(m_meshInfoBuffer);
        m_meshInfoBuffer = Buffer::Create(
            std::max((size_t)1, (size_t)m_meshCount) * sizeof(MeshInfoGPU),
            vk::BufferUsageFlagBits2::eStorageBuffer | vk::BufferUsageFlagBits2::eTransferDst,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            "MeshInfo_Buffer");

        m_meshInfoBuffer->Map();
        auto *meshInfosGPU = (MeshInfoGPU *)m_meshInfoBuffer->Data();

        for (size_t i = 0; i < instanceReqs.size(); i++)
        {
            auto &req = instanceReqs[i];
            auto &meshInfoGPU = meshInfosGPU[i];

            const Mesh &mesh = m_meshes[req.meshIndex];
            const MeshRuntime &meshRt = m_meshRuntimes[req.meshIndex];

            meshInfoGPU.indexOffset = mesh.indexOffset * 4;
            meshInfoGPU.vertexOffset = static_cast<uint32_t>(m_verticesOffset) + mesh.vertexOffset * sizeof(Vertex);
            meshInfoGPU.renderType = static_cast<uint32_t>(mesh.renderType);

            for (int k = 0; k < 5; k++)
                meshInfoGPU.textures[k] = static_cast<int32_t>(meshRt.imageViewIndices[k]);
        }
        m_meshInfoBuffer->Flush();
        m_meshInfoBuffer->Unmap();
    }

    void Scene::UpdateTLASTransformations(CommandBuffer *cmd)
    {
        if (!m_tlas || !m_instanceBuffer)
            return;

        if (m_nodesMoved.empty())
            return;

        m_instanceBuffer->Map();
        auto *gpuInstances = (vk::AccelerationStructureInstanceKHR *)m_instanceBuffer->Data();

        for (NodeId *node : m_nodesMoved)
        {
            const NodeRuntime &rt = m_nodeRuntime[node->index];
            int instanceIndex = rt.instanceIndex;
            if (instanceIndex < 0)
                continue;

            const mat4 &t = rt.gpuData.worldMatrix;

            vk::TransformMatrixKHR transformMatrix;
            transformMatrix.matrix[0][0] = t[0][0];
            transformMatrix.matrix[0][1] = t[1][0];
            transformMatrix.matrix[0][2] = t[2][0];
            transformMatrix.matrix[0][3] = t[3][0];
            transformMatrix.matrix[1][0] = t[0][1];
            transformMatrix.matrix[1][1] = t[1][1];
            transformMatrix.matrix[1][2] = t[2][1];
            transformMatrix.matrix[1][3] = t[3][1];
            transformMatrix.matrix[2][0] = t[0][2];
            transformMatrix.matrix[2][1] = t[1][2];
            transformMatrix.matrix[2][2] = t[2][2];
            transformMatrix.matrix[2][3] = t[3][2];

            gpuInstances[instanceIndex].transform = transformMatrix;
        }

        m_instanceBuffer->Flush();
        m_instanceBuffer->Unmap();

        m_tlas->UpdateTLAS(cmd, m_meshCount, m_instanceBuffer, m_scratchBuffer->GetDeviceAddress());
    }
    std::string Scene::GetSceneName() const
    {
        if (m_scenePath.empty())
            return "Untitled";
        return m_scenePath.stem().string();
    }

    void Scene::SaveScene(const std::filesystem::path &file)
    {
        rapidjson::Document d;
        d.SetObject();
        auto &allocator = d.GetAllocator();

        auto SafeFloat = [](float f)
        {
            return std::isnan(f) || std::isinf(f) ? 0.0f : f;
        };

        auto SetVec3 = [&](rapidjson::Value &arr, const vec3 &v)
        {
            arr.SetArray();
            arr.PushBack(SafeFloat(v.x), allocator).PushBack(SafeFloat(v.y), allocator).PushBack(SafeFloat(v.z), allocator);
        };

        auto SetVec4 = [&](rapidjson::Value &arr, const vec4 &v)
        {
            arr.SetArray();
            arr.PushBack(SafeFloat(v.x), allocator).PushBack(SafeFloat(v.y), allocator).PushBack(SafeFloat(v.z), allocator).PushBack(SafeFloat(v.w), allocator);
        };

        auto SetMat4 = [&](rapidjson::Value &arr, const mat4 &m)
        {
            arr.SetArray();
            const float *p = value_ptr(m);
            for (int i = 0; i < 16; i++)
                arr.PushBack(SafeFloat(p[i]), allocator);
        };

        // Global Settings
        rapidjson::Value settings(rapidjson::kObjectType);
        auto &gSettings = Settings::Get<GlobalSettings>();
        settings.AddMember("shadows", gSettings.shadows, allocator);
        settings.AddMember("shadow_map_size", gSettings.shadow_map_size, allocator);
        settings.AddMember("num_cascades", gSettings.num_cascades, allocator);
        settings.AddMember("render_scale", gSettings.render_scale, allocator);
        settings.AddMember("ssao", gSettings.ssao, allocator);
        settings.AddMember("fxaa", gSettings.fxaa, allocator);
        settings.AddMember("taa", gSettings.taa, allocator);
        settings.AddMember("cas_sharpening", gSettings.cas_sharpening, allocator);
        settings.AddMember("cas_sharpness", gSettings.cas_sharpness, allocator);
        settings.AddMember("ssr", gSettings.ssr, allocator);
        settings.AddMember("tonemapping", gSettings.tonemapping, allocator);
        settings.AddMember("dof", gSettings.dof, allocator);
        settings.AddMember("dof_focus_scale", gSettings.dof_focus_scale, allocator);
        settings.AddMember("dof_blur_range", gSettings.dof_blur_range, allocator);
        settings.AddMember("bloom", gSettings.bloom, allocator);
        settings.AddMember("bloom_strength", gSettings.bloom_strength, allocator);
        settings.AddMember("bloom_range", gSettings.bloom_range, allocator);
        settings.AddMember("motion_blur", gSettings.motion_blur, allocator);
        settings.AddMember("motion_blur_strength", gSettings.motion_blur_strength, allocator);
        settings.AddMember("motion_blur_samples", gSettings.motion_blur_samples, allocator);
        settings.AddMember("IBL", gSettings.IBL, allocator);
        settings.AddMember("IBL_intensity", gSettings.IBL_intensity, allocator);
        settings.AddMember("lights_intensity", gSettings.lights_intensity, allocator);
        settings.AddMember("day", gSettings.day, allocator);

        rapidjson::Value depthBias(rapidjson::kArrayType);
        depthBias.PushBack(gSettings.depth_bias[0], allocator);
        depthBias.PushBack(gSettings.depth_bias[1], allocator);
        depthBias.PushBack(gSettings.depth_bias[2], allocator);
        settings.AddMember("depth_bias", depthBias.Move(), allocator);

        settings.AddMember("draw_grid", gSettings.draw_grid, allocator);
        settings.AddMember("draw_aabbs", gSettings.draw_aabbs, allocator);
        settings.AddMember("render_mode", static_cast<int>(gSettings.render_mode), allocator);

        d.AddMember("settings", settings.Move(), allocator);

        // Build live mesh/source remaps to exclude geometry orphaned by RemoveModel.
        // A mesh is live if at least one active node references it.
        // A source is live if at least one live mesh references it.
        std::vector<int> meshRemap(m_meshes.size(), -1);
        std::vector<int> srcRemap(m_sources.size(), -1);
        {
            std::vector<bool> liveMesh(m_meshes.size(), false);
            for (uint32_t ni = 0; ni < GetNodeCount(); ni++)
            {
                int mr = m_meshRefs[ni];
                if (mr >= 0 && mr < static_cast<int>(m_meshes.size()))
                    liveMesh[mr] = true;
            }
            int newMeshIdx = 0;
            for (int mi = 0; mi < static_cast<int>(m_meshes.size()); mi++)
                if (liveMesh[mi])
                    meshRemap[mi] = newMeshIdx++;

            std::vector<bool> liveSrc(m_sources.size(), false);
            for (int mi = 0; mi < static_cast<int>(m_meshes.size()); mi++)
            {
                if (meshRemap[mi] < 0)
                    continue;
                if (mi < static_cast<int>(m_meshSourceInfos.size()))
                {
                    int si = m_meshSourceInfos[mi].sourceIndex;
                    if (si >= 0 && si < static_cast<int>(m_sources.size()))
                        liveSrc[si] = true;
                }
            }
            int newSrcIdx = 0;
            for (int si = 0; si < static_cast<int>(m_sources.size()); si++)
                if (liveSrc[si])
                    srcRemap[si] = newSrcIdx++;
        }

        // Sources — geometry origins for reload (live only)
        rapidjson::Value sourcesArr(rapidjson::kArrayType);
        for (int si = 0; si < static_cast<int>(m_sources.size()); si++)
        {
            if (srcRemap[si] < 0)
                continue;
            const auto &src = m_sources[si];
            rapidjson::Value srcObj(rapidjson::kObjectType);
            if (!src.primitiveType.empty())
            {
                srcObj.AddMember("primitive_type", rapidjson::Value(src.primitiveType.c_str(), allocator).Move(), allocator);
            }
            else
            {
                auto relPath = std::filesystem::relative(src.filePath, file.parent_path());
                auto u8rp = relPath.u8string();
                std::string pathStr(u8rp.begin(), u8rp.end());
                std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
                srcObj.AddMember("path", rapidjson::Value(pathStr.c_str(), static_cast<rapidjson::SizeType>(pathStr.length()), allocator).Move(), allocator);
            }
            sourcesArr.PushBack(srcObj.Move(), allocator);
        }
        d.AddMember("sources", sourcesArr.Move(), allocator);

        // Meshes — flat array of live meshes only, with remapped source references
        rapidjson::Value meshesArr(rapidjson::kArrayType);
        for (int mi = 0; mi < static_cast<int>(m_meshes.size()); mi++)
        {
            if (meshRemap[mi] < 0)
                continue;
            const Mesh &mesh = m_meshes[mi];

            rapidjson::Value meshObj(rapidjson::kObjectType);

            // Source reference (for geometry reload)
            if (mi < static_cast<int>(m_meshSourceInfos.size()))
            {
                int rawSrc = m_meshSourceInfos[mi].sourceIndex;
                int remappedSrc = (rawSrc >= 0 && rawSrc < static_cast<int>(srcRemap.size())) ? srcRemap[rawSrc] : -1;
                meshObj.AddMember("source", remappedSrc, allocator);
                meshObj.AddMember("source_mesh", m_meshSourceInfos[mi].sourceMeshIndex, allocator);
            }

            meshObj.AddMember("render_type", static_cast<int>(mesh.renderType), allocator);
            meshObj.AddMember("texture_mask", mesh.textureMask, allocator);

            mat4 persistedF0 = mesh.materialFactors[0];
            mat4 persistedF1 = mesh.materialFactors[1];
            EncodeMaterialFactorsForPersistence(mesh.renderType, persistedF0, persistedF1);

            rapidjson::Value factorsArr(rapidjson::kArrayType);
            rapidjson::Value f0, f1;
            SetMat4(f0, persistedF0);
            SetMat4(f1, persistedF1);
            factorsArr.PushBack(f0.Move(), allocator);
            factorsArr.PushBack(f1.Move(), allocator);
            meshObj.AddMember("material_factors", factorsArr.Move(), allocator);

            rapidjson::Value texturesObj(rapidjson::kObjectType);
            const char *texSlotNames[] = {"base_color", "metallic_roughness", "normal", "occlusion", "emissive"};
            const auto &defaults = ModelAsset::GetDefaultResources();
            for (int i = 0; i < 5; i++)
            {
                Image *img = mesh.images[i].get();
                if (img && !img->GetName().empty() &&
                    img != defaults.black && img != defaults.white && img != defaults.normal)
                {
                    std::string texName = img->GetName();
                    auto relTex = std::filesystem::relative(std::filesystem::path(texName), file.parent_path());
                    auto u8tex = relTex.u8string();
                    texName = std::string(u8tex.begin(), u8tex.end());
                    std::replace(texName.begin(), texName.end(), '\\', '/');
                    if (!texName.empty())
                        texturesObj.AddMember(rapidjson::Value(texSlotNames[i], allocator).Move(),
                                              rapidjson::Value(texName.c_str(), allocator).Move(), allocator);
                }
            }
            meshObj.AddMember("textures", texturesObj.Move(), allocator);
            meshesArr.PushBack(meshObj.Move(), allocator);
        }
        d.AddMember("meshes", meshesArr.Move(), allocator);

        // Nodes — flat array of all SoA nodes with global parent indices
        rapidjson::Value nodesArr(rapidjson::kArrayType);
        for (uint32_t ni = 0; ni < GetNodeCount(); ni++)
        {
            const NodeId *node = m_nodeIds[ni];

            rapidjson::Value nodeObj(rapidjson::kObjectType);
            nodeObj.AddMember("name", rapidjson::Value(m_nodeNames[ni].c_str(), allocator).Move(), allocator);

            NodeId *parentNode = m_nodeParents[ni];
            int parentIdx = parentNode ? static_cast<int>(parentNode->index) : -1;
            nodeObj.AddMember("parent", parentIdx, allocator);

            rapidjson::Value localMat;
            SetMat4(localMat, m_localMatrices[ni]);
            nodeObj.AddMember("local_matrix", localMat.Move(), allocator);

            int meshRef = m_meshRefs[ni];
            int remappedMesh = (meshRef >= 0 && meshRef < static_cast<int>(meshRemap.size())) ? meshRemap[meshRef] : -1;
            if (remappedMesh >= 0)
                nodeObj.AddMember("mesh", remappedMesh, allocator);

            nodesArr.PushBack(nodeObj.Move(), allocator);
        }
        d.AddMember("nodes", nodesArr.Move(), allocator);

        // Lights
        auto *lightSystem = GetGlobalSystem<LightSystem>();
        if (lightSystem)
        {
            rapidjson::Value lights(rapidjson::kArrayType);

            // Directional
            for (const auto &l : lightSystem->GetDirectionalLights())
            {
                rapidjson::Value lObj(rapidjson::kObjectType);
                lObj.AddMember("type", "directional", allocator); // type identifier

                rapidjson::Value color, pos, rot;
                SetVec4(color, l.color);
                SetVec4(pos, l.position);
                SetVec4(rot, l.rotation);

                lObj.AddMember("color", color.Move(), allocator);
                lObj.AddMember("position", pos.Move(), allocator);
                lObj.AddMember("rotation", rot.Move(), allocator);
                lObj.AddMember("name", rapidjson::Value(l.name.c_str(), allocator).Move(), allocator);
                lights.PushBack(lObj.Move(), allocator);
            }

            // Point
            for (const auto &l : lightSystem->GetPointLights())
            {
                rapidjson::Value lObj(rapidjson::kObjectType);
                lObj.AddMember("type", "point", allocator);

                rapidjson::Value color, pos;
                SetVec4(color, l.color);
                SetVec4(pos, l.position);

                lObj.AddMember("color", color.Move(), allocator);
                lObj.AddMember("position", pos.Move(), allocator); // .w is radius
                lObj.AddMember("name", rapidjson::Value(l.name.c_str(), allocator).Move(), allocator);
                lights.PushBack(lObj.Move(), allocator);
            }

            // Spot
            for (const auto &l : lightSystem->GetSpotLights())
            {
                rapidjson::Value lObj(rapidjson::kObjectType);
                lObj.AddMember("type", "spot", allocator);

                rapidjson::Value color, pos, rot, params;
                SetVec4(color, l.color);
                SetVec4(pos, l.position);
                SetVec4(rot, l.rotation);
                SetVec4(params, l.params);

                lObj.AddMember("color", color.Move(), allocator);
                lObj.AddMember("position", pos.Move(), allocator); // .w is range
                lObj.AddMember("rotation", rot.Move(), allocator);
                lObj.AddMember("params", params.Move(), allocator); // angle, falloff
                lObj.AddMember("name", rapidjson::Value(l.name.c_str(), allocator).Move(), allocator);
                lights.PushBack(lObj.Move(), allocator);
            }

            // Area
            for (const auto &l : lightSystem->GetAreaLights())
            {
                rapidjson::Value lObj(rapidjson::kObjectType);
                lObj.AddMember("type", "area", allocator);

                rapidjson::Value color, pos, rot, size;
                SetVec4(color, l.color);
                SetVec4(pos, l.position);
                SetVec4(rot, l.rotation);
                SetVec4(size, l.size);

                lObj.AddMember("color", color.Move(), allocator);
                lObj.AddMember("position", pos.Move(), allocator); // .w is range
                lObj.AddMember("rotation", rot.Move(), allocator);
                lObj.AddMember("size", size.Move(), allocator);
                lObj.AddMember("name", rapidjson::Value(l.name.c_str(), allocator).Move(), allocator);
                lights.PushBack(lObj.Move(), allocator);
            }

            d.AddMember("lights", lights.Move(), allocator);
        }

        // Cameras
        rapidjson::Value cameras(rapidjson::kArrayType);
        for (auto *camera : m_cameras)
        {
            if (!camera)
                continue;
            rapidjson::Value camObj(rapidjson::kObjectType);
            camObj.AddMember("name", rapidjson::Value(camera->GetName().c_str(), allocator).Move(), allocator);

            rapidjson::Value pos, eul;
            SetVec3(pos, camera->GetPosition());
            SetVec3(eul, camera->GetEuler());

            camObj.AddMember("position", pos.Move(), allocator);
            camObj.AddMember("euler", eul.Move(), allocator);
            camObj.AddMember("fovx", camera->Fovx(), allocator);
            camObj.AddMember("near_plane", camera->GetNearPlane(), allocator);
            camObj.AddMember("far_plane", camera->GetFarPlane(), allocator);
            camObj.AddMember("speed", camera->GetSpeed(), allocator);
            cameras.PushBack(camObj.Move(), allocator);
        }
        d.AddMember("cameras", cameras.Move(), allocator);

        // Active Camera
        for (int i = 0; i < m_cameras.size(); i++)
        {
            if (m_cameras[i] == GetActiveCamera())
            {
                d.AddMember("active_camera", i, allocator);
                break;
            }
        }

        // Write to file
        std::ofstream ofs(file);
        if (!ofs.is_open())
        {
            Log::Error("Failed to open file for writing: " + file.string());
            return;
        }

        rapidjson::OStreamWrapper osw(ofs);
        rapidjson::PrettyWriter<rapidjson::OStreamWrapper> writer(osw);
        writer.SetMaxDecimalPlaces(6);

        if (d.Accept(writer))
        {
            if (ofs.bad())
            {
                Log::Error("File stream error while writing to: " + file.string());
            }
            else
            {
                m_scenePath = file;
                m_dirty = false;
                Log::Info("Scene saved to: " + file.string());
            }
        }
        else
        {
            Log::Error("Failed to write JSON content (invalid data/encoding?) to: " + file.string());
        }
    }

    void Scene::NewScene()
    {
        m_scenePath.clear();
        m_dirty = false;

        SelectionManager::Instance().ClearSelection();

        m_sources.clear();
        m_meshSourceInfos.clear();
        m_modelRootNodes.clear();

        // Free all NodeId allocations and clear SoA stores
        for (NodeId *id : m_nodeIds)
            delete id;
        for (NodeId *id : m_freeNodeIds)
            delete id;
        m_nodeIds.clear();
        m_freeNodeIds.clear();
        m_nodeNames.clear();
        m_localMatrices.clear();
        m_nodeParents.clear();
        m_nodeChildren.clear();
        m_componentFlags.clear();
        m_meshRefs.clear();
        m_nodeRuntime.clear();
        m_nodesMoved.clear();
        m_nodesDirty = false;

        m_meshes.clear();
        m_meshRuntimes.clear();
        m_vertexStore.clear();
        m_positionUvStore.clear();
        m_aabbVertexStore.clear();
        m_indexStore.clear();
        m_imageStore.clear();
        m_samplerStore.clear();

        for (auto *model : m_models)
            delete model;
        m_models.clear();

        auto *lightSystem = GetGlobalSystem<LightSystem>();
        if (lightSystem)
        {
            lightSystem->GetDirectionalLights().clear();
            lightSystem->GetPointLights().clear();
            lightSystem->GetSpotLights().clear();
            lightSystem->GetAreaLights().clear();
        }

        while (m_cameras.size() > 1)
        {
            delete m_cameras.back();
            m_cameras.pop_back();
        }

        UpdateGeometryBuffers();

        Log::Info("New scene created.");
    }

    Scene::ScenePreload::~ScenePreload()
    {
        for (auto *m : models)
            delete m;
    }

    Scene::ScenePreload Scene::PreloadScene(const std::filesystem::path &file)
    {
        ScenePreload result;
        result.filePath = file;

        std::ifstream ifs(file);
        if (!ifs.is_open())
        {
            Log::Error("Failed to open scene file: " + file.string());
            return result;
        }
        result.jsonText = std::string(std::istreambuf_iterator<char>(ifs),
                                      std::istreambuf_iterator<char>());

        rapidjson::Document d;
        d.Parse(result.jsonText.c_str());
        if (d.HasParseError())
        {
            Log::Error("Failed to parse scene file: " + file.string());
            return result;
        }

        auto loadPrimitive = [](const std::string &ptype) -> ModelAsset *
        {
            if (ptype == "cube")
                return Primitives::CreateCube();
            if (ptype == "sphere")
                return Primitives::CreateSphere();
            if (ptype == "plane")
                return Primitives::CreatePlane();
            if (ptype == "cylinder")
                return Primitives::CreateCylinder();
            if (ptype == "cone")
                return Primitives::CreateCone();
            if (ptype == "quad")
                return Primitives::CreateQuad();
            return nullptr;
        };

        if (d.HasMember("sources"))
        {
            const auto &sourcesVal = d["sources"];
            result.models.resize(sourcesVal.Size(), nullptr);
            for (rapidjson::SizeType si = 0; si < sourcesVal.Size(); si++)
            {
                const auto &sv = sourcesVal[si];
                ModelAsset *model = nullptr;
                if (sv.HasMember("primitive_type"))
                {
                    model = loadPrimitive(sv["primitive_type"].GetString());
                }
                else if (sv.HasMember("path"))
                {
                    std::filesystem::path modelPath = sv["path"].GetString();
                    if (modelPath.is_relative())
                        modelPath = file.parent_path() / modelPath;
                    modelPath = modelPath.lexically_normal();
                    if (!std::filesystem::is_directory(modelPath))
                        model = ModelAsset::Load(modelPath);
                }
                result.models[si] = model;
            }
        }
        else if (d.HasMember("models"))
        {
            const auto &modelsVal = d["models"];
            result.models.resize(modelsVal.Size(), nullptr);
            for (rapidjson::SizeType i = 0; i < modelsVal.Size(); i++)
            {
                const auto &modelVal = modelsVal[i];
                ModelAsset *model = nullptr;
                if (modelVal.HasMember("primitive_type"))
                {
                    model = loadPrimitive(modelVal["primitive_type"].GetString());
                }
                else if (modelVal.HasMember("path"))
                {
                    std::filesystem::path modelPath = modelVal["path"].GetString();
                    if (modelPath.is_relative())
                        modelPath = file.parent_path() / modelPath;
                    modelPath = modelPath.lexically_normal();
                    if (std::filesystem::is_directory(modelPath))
                        continue;
                    model = ModelAsset::Load(modelPath);
                }
                result.models[i] = model;
            }
        }

        result.valid = true;
        return result;
    }

    void Scene::LoadSceneApply(ScenePreload preload)
    {
        if (!preload.valid)
            return;

        m_scenePath = preload.filePath;
        m_dirty = false;

        rapidjson::Document d;
        d.Parse(preload.jsonText.c_str());
        if (d.HasParseError())
            return;

        // Clear existing scene: free SoA data, delete models, clear geometry stores
        SelectionManager::Instance().ClearSelection();

        m_sources.clear();
        m_meshSourceInfos.clear();
        m_modelRootNodes.clear();

        for (NodeId *id : m_nodeIds)
            delete id;
        for (NodeId *id : m_freeNodeIds)
            delete id;
        m_nodeIds.clear();
        m_freeNodeIds.clear();
        m_nodeNames.clear();
        m_localMatrices.clear();
        m_nodeParents.clear();
        m_nodeChildren.clear();
        m_componentFlags.clear();
        m_meshRefs.clear();
        m_nodeRuntime.clear();
        m_nodesMoved.clear();
        m_nodesDirty = false;

        m_meshes.clear();
        m_meshRuntimes.clear();
        m_vertexStore.clear();
        m_positionUvStore.clear();
        m_aabbVertexStore.clear();
        m_indexStore.clear();
        m_imageStore.clear();
        m_samplerStore.clear();

        for (auto *model : m_models)
            delete model;
        m_models.clear();

        auto *lightSystem = GetGlobalSystem<LightSystem>();
        if (lightSystem)
        {
            lightSystem->GetDirectionalLights().clear();
            lightSystem->GetPointLights().clear();
            lightSystem->GetSpotLights().clear();
            lightSystem->GetAreaLights().clear();
        }

        // Reuse first camera, remove others
        while (m_cameras.size() > 1)
        {
            delete m_cameras.back();
            m_cameras.pop_back();
        }

        if (d.HasMember("settings"))
        {
            const auto &settings = d["settings"];
            auto &gSettings = Settings::Get<GlobalSettings>();
            if (settings.HasMember("shadows"))
                gSettings.shadows = settings["shadows"].GetBool();
            if (settings.HasMember("shadow_map_size"))
                gSettings.shadow_map_size = settings["shadow_map_size"].GetUint();
            if (settings.HasMember("num_cascades"))
                gSettings.num_cascades = settings["num_cascades"].GetUint();
            if (settings.HasMember("render_scale"))
                gSettings.render_scale = settings["render_scale"].GetFloat();
            if (settings.HasMember("ssao"))
                gSettings.ssao = settings["ssao"].GetBool();
            if (settings.HasMember("fxaa"))
                gSettings.fxaa = settings["fxaa"].GetBool();
            if (settings.HasMember("taa"))
                gSettings.taa = settings["taa"].GetBool();
            if (settings.HasMember("cas_sharpening"))
                gSettings.cas_sharpening = settings["cas_sharpening"].GetBool();
            if (settings.HasMember("cas_sharpness"))
                gSettings.cas_sharpness = settings["cas_sharpness"].GetFloat();
            if (settings.HasMember("ssr"))
                gSettings.ssr = settings["ssr"].GetBool();
            if (settings.HasMember("tonemapping"))
                gSettings.tonemapping = settings["tonemapping"].GetBool();
            if (settings.HasMember("dof"))
                gSettings.dof = settings["dof"].GetBool();
            if (settings.HasMember("dof_focus_scale"))
                gSettings.dof_focus_scale = settings["dof_focus_scale"].GetFloat();
            if (settings.HasMember("dof_blur_range"))
                gSettings.dof_blur_range = settings["dof_blur_range"].GetFloat();
            if (settings.HasMember("bloom"))
                gSettings.bloom = settings["bloom"].GetBool();
            if (settings.HasMember("bloom_strength"))
                gSettings.bloom_strength = settings["bloom_strength"].GetFloat();
            if (settings.HasMember("bloom_range"))
                gSettings.bloom_range = settings["bloom_range"].GetFloat();
            if (settings.HasMember("motion_blur"))
                gSettings.motion_blur = settings["motion_blur"].GetBool();
            if (settings.HasMember("motion_blur_strength"))
                gSettings.motion_blur_strength = settings["motion_blur_strength"].GetFloat();
            if (settings.HasMember("motion_blur_samples"))
                gSettings.motion_blur_samples = settings["motion_blur_samples"].GetInt();
            if (settings.HasMember("IBL"))
                gSettings.IBL = settings["IBL"].GetBool();
            if (settings.HasMember("IBL_intensity"))
                gSettings.IBL_intensity = settings["IBL_intensity"].GetFloat();
            if (settings.HasMember("lights_intensity"))
                gSettings.lights_intensity = settings["lights_intensity"].GetFloat();
            if (settings.HasMember("day"))
                gSettings.day = settings["day"].GetBool();
            if (settings.HasMember("depth_bias"))
            {
                gSettings.depth_bias[0] = settings["depth_bias"][0].GetFloat();
                gSettings.depth_bias[1] = settings["depth_bias"][1].GetFloat();
                gSettings.depth_bias[2] = settings["depth_bias"][2].GetFloat();
            }
            if (settings.HasMember("draw_grid"))
                gSettings.draw_grid = settings["draw_grid"].GetBool();
            if (settings.HasMember("draw_aabbs"))
                gSettings.draw_aabbs = settings["draw_aabbs"].GetBool();
            if (settings.HasMember("render_mode"))
                gSettings.render_mode = static_cast<RenderMode>(settings["render_mode"].GetInt());

            MarkUniformsDirty();
        }

        auto ReadVec3 = [](const rapidjson::Value &arr)
        {
            return vec3(arr[0].GetFloat(), arr[1].GetFloat(), arr[2].GetFloat());
        };
        auto ReadVec4 = [](const rapidjson::Value &arr)
        {
            return vec4(arr[0].GetFloat(), arr[1].GetFloat(), arr[2].GetFloat(), arr[3].GetFloat());
        };
        auto ReadMat4 = [](const rapidjson::Value &arr)
        {
            mat4 m;
            float *p = value_ptr(m);
            for (int i = 0; i < 16; i++)
                p[i] = arr[i].GetFloat();
            return m;
        };

        if (d.HasMember("sources"))
        {
            // --- New flat format ---
            Queue *queue = RHII.GetMainQueue();
            CommandBuffer *cmd = queue->AcquireCommandBuffer();
            cmd->Begin();

            // 1. Load sources and copy geometry
            const auto &sourcesVal = d["sources"];
            std::vector<ModelAsset *> loadedModels(sourcesVal.Size(), nullptr);
            std::vector<std::vector<int>> sourceMeshMaps(sourcesVal.Size());

            for (rapidjson::SizeType si = 0; si < sourcesVal.Size(); si++)
            {
                ModelAsset *model = si < preload.models.size() ? preload.models[si] : nullptr;
                if (si < preload.models.size())
                    preload.models[si] = nullptr; // take ownership

                if (model)
                {
                    int sourceIndex = static_cast<int>(m_sources.size());
                    SceneSource source;
                    source.filePath = model->GetFilePath();
                    source.primitiveType = model->GetPrimitiveType();
                    m_sources.push_back(std::move(source));

                    sourceMeshMaps[si] = AddModelGeometry(model, sourceIndex);
                    m_models.insert(model->GetId(), model);
                    loadedModels[si] = model;
                }
            }

            // 2. Apply mesh overrides from JSON
            if (d.HasMember("meshes"))
            {
                const auto &meshesVal = d["meshes"];
                for (rapidjson::SizeType mi = 0; mi < meshesVal.Size(); mi++)
                {
                    const auto &mVal = meshesVal[mi];

                    // Resolve scene mesh index via source reference
                    int sceneMeshIdx = -1;
                    if (mVal.HasMember("source") && mVal.HasMember("source_mesh"))
                    {
                        int srcIdx = mVal["source"].GetInt();
                        int srcMesh = mVal["source_mesh"].GetInt();
                        if (srcIdx >= 0 && srcIdx < static_cast<int>(sourceMeshMaps.size()) &&
                            srcMesh >= 0 && srcMesh < static_cast<int>(sourceMeshMaps[srcIdx].size()))
                        {
                            sceneMeshIdx = sourceMeshMaps[srcIdx][srcMesh];
                        }
                    }
                    if (sceneMeshIdx < 0 || sceneMeshIdx >= static_cast<int>(m_meshes.size()))
                        continue;

                    Mesh &mesh = m_meshes[sceneMeshIdx];
                    if (mVal.HasMember("render_type"))
                        mesh.renderType = static_cast<RenderType>(mVal["render_type"].GetInt());
                    if (mVal.HasMember("texture_mask"))
                    {
                        uint32_t savedMask = mVal["texture_mask"].GetUint();
                        // Only override if the saved mask is non-zero, or if the model had no textures.
                        // This guards against corrupted scene files that saved texture_mask as 0.
                        if (savedMask != 0 || mesh.textureMask == 0)
                            mesh.textureMask = savedMask;
                    }
                    if (mVal.HasMember("material_factors"))
                    {
                        mesh.materialFactors[0] = ReadMat4(mVal["material_factors"][0]);
                        mesh.materialFactors[1] = ReadMat4(mVal["material_factors"][1]);
                        DecodeMaterialFactorsFromPersistence(mesh.renderType, mesh.materialFactors[0], mesh.materialFactors[1]);
                    }

                    if (mVal.HasMember("textures"))
                    {
                        const auto &texVal = mVal["textures"];
                        const char *texSlotNames[] = {"base_color", "metallic_roughness", "normal", "occlusion", "emissive"};
                        for (int k = 0; k < 5; k++)
                        {
                            if (texVal.HasMember(texSlotNames[k]) && texVal[texSlotNames[k]].GetStringLength() > 0)
                            {
                                std::filesystem::path texPath = texVal[texSlotNames[k]].GetString();
                                if (texPath.is_relative())
                                    texPath = (preload.filePath.parent_path() / texPath).lexically_normal();
                                if (std::filesystem::exists(texPath))
                                {
                                    // Find source model for texture loading
                                    int srcIdx = m_meshSourceInfos[sceneMeshIdx].sourceIndex;
                                    ModelAsset *srcModel = (srcIdx >= 0 && srcIdx < static_cast<int>(loadedModels.size()))
                                                               ? loadedModels[srcIdx]
                                                               : nullptr;
                                    if (srcModel)
                                    {
                                        ResourceHandle<Image> img = srcModel->LoadTexture(cmd, texPath);
                                        if (img)
                                            mesh.images[k] = img;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // 3. Create flat node hierarchy from JSON (two-pass: create, then reparent)
            if (d.HasMember("nodes"))
            {
                const auto &nodesVal = d["nodes"];
                std::vector<NodeId *> nodeMap(nodesVal.Size(), nullptr);

                // Pass 1: create all nodes as roots
                for (rapidjson::SizeType ni = 0; ni < nodesVal.Size(); ni++)
                {
                    const auto &nv = nodesVal[ni];
                    std::string name = nv.HasMember("name") ? nv["name"].GetString() : "";
                    NodeId *node = CreateNode(name);

                    if (nv.HasMember("local_matrix"))
                        SetLocalMatrix(node, ReadMat4(nv["local_matrix"]), false);
                    if (nv.HasMember("mesh"))
                    {
                        int meshIdx = nv["mesh"].GetInt();
                        if (meshIdx >= 0 && meshIdx < static_cast<int>(m_meshes.size()))
                            SetMeshRef(node, meshIdx);
                    }

                    nodeMap[ni] = node;
                }

                // Pass 2: set parents
                for (rapidjson::SizeType ni = 0; ni < nodesVal.Size(); ni++)
                {
                    const auto &nv = nodesVal[ni];
                    if (nv.HasMember("parent"))
                    {
                        int parentIdx = nv["parent"].GetInt();
                        if (parentIdx >= 0 && parentIdx < static_cast<int>(nodeMap.size()) && nodeMap[parentIdx])
                            ReparentNode(nodeMap[ni], nodeMap[parentIdx]);
                    }
                }

                // Mark all nodes dirty
                for (NodeId *node : nodeMap)
                {
                    if (node)
                        MarkNodeDirty(node);
                }
                UpdateNodeMatrices();
            }

            UploadBuffers(cmd);

            cmd->End();
            queue->Submit(1, &cmd, nullptr, nullptr);
            cmd->Wait();
            queue->ReturnCommandBuffer(cmd);
        }
        else if (d.HasMember("models"))
        {
            // --- Legacy per-model format ---
            Queue *queue = RHII.GetMainQueue();
            CommandBuffer *cmd = queue->AcquireCommandBuffer();
            cmd->Begin();

            const auto &models = d["models"];
            for (rapidjson::SizeType i = 0; i < models.Size(); i++)
            {
                const auto &modelVal = models[i];
                ModelAsset *model = i < preload.models.size() ? preload.models[i] : nullptr;
                if (i < preload.models.size())
                    preload.models[i] = nullptr; // take ownership

                if (model)
                {
                    if (modelVal.HasMember("name"))
                        model->SetLabel(modelVal["name"].GetString());
                    if (modelVal.HasMember("matrix"))
                        model->SetMatrix(ReadMat4(modelVal["matrix"]), false);

                    if (modelVal.HasMember("nodes"))
                    {
                        const auto &nodesVal = modelVal["nodes"];
                        for (rapidjson::SizeType i = 0; i < nodesVal.Size() && i < static_cast<rapidjson::SizeType>(model->GetNodeCount()); i++)
                        {
                            const auto &nVal = nodesVal[i];
                            if (nVal.HasMember("name"))
                                model->SetNodeName(static_cast<int>(i), nVal["name"].GetString());
                            if (nVal.HasMember("parent"))
                                model->SetNodeParentIndex(static_cast<int>(i), nVal["parent"].GetInt());
                            if (nVal.HasMember("local_matrix"))
                                model->SetNodeLocalMatrix(static_cast<int>(i), ReadMat4(nVal["local_matrix"]), false);
                        }

                        model->RebuildNodeChildrenFromParents();
                        model->SetDirtyNodes(true);
                        model->UpdateNodeMatrices();
                    }

                    if (modelVal.HasMember("meshes"))
                    {
                        const auto &meshesVal = modelVal["meshes"];
                        for (rapidjson::SizeType i = 0; i < meshesVal.Size(); i++)
                        {
                            MeshInfo *mi = model->GetMeshInfo(static_cast<int>(i));
                            if (!mi)
                                break;

                            const auto &mVal = meshesVal[i];
                            if (mVal.HasMember("render_type"))
                                mi->renderType = static_cast<RenderType>(mVal["render_type"].GetInt());
                            if (mVal.HasMember("material_factors"))
                            {
                                mi->materialFactors[0] = ReadMat4(mVal["material_factors"][0]);
                                mi->materialFactors[1] = ReadMat4(mVal["material_factors"][1]);
                                DecodeMaterialFactorsFromPersistence(mi->renderType, mi->materialFactors[0], mi->materialFactors[1]);
                            }

                            bool hasTextureMask = mVal.HasMember("texture_mask");
                            if (hasTextureMask)
                                mi->textureMask = mVal["texture_mask"].GetUint();

                            if (mVal.HasMember("textures"))
                            {
                                const auto &texVal = mVal["textures"];
                                const char *methodNames[] = {"base_color", "metallic_roughness", "normal", "occlusion", "emissive"};
                                for (int k = 0; k < 5; k++)
                                {
                                    if (texVal.HasMember(methodNames[k]) && texVal[methodNames[k]].GetStringLength() > 0)
                                    {
                                        std::filesystem::path texPath = texVal[methodNames[k]].GetString();
                                        if (texPath.is_relative())
                                            texPath = (preload.filePath.parent_path() / texPath).lexically_normal();
                                        if (std::filesystem::exists(texPath))
                                        {
                                            ResourceHandle<Image> img = model->LoadTexture(cmd, texPath);
                                            if (img)
                                            {
                                                mi->images[k] = img;
                                                if (!hasTextureMask)
                                                    mi->textureMask |= (1 << k);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    EventSystem::PushEvent(EventType::ModelLoaded, model);
                }
            }

            cmd->End();
            queue->Submit(1, &cmd, nullptr, nullptr);
            cmd->Wait();
            queue->ReturnCommandBuffer(cmd);
        }

        if (d.HasMember("lights") && lightSystem)
        {
            const auto &lights = d["lights"];
            for (const auto &lVal : lights.GetArray())
            {
                std::string type = lVal["type"].GetString();
                if (type == "directional")
                {
                    DirectionalLightEditor l{};
                    l.color = ReadVec4(lVal["color"]);
                    l.position = ReadVec4(lVal["position"]);
                    if (lVal.HasMember("rotation"))
                        l.rotation = ReadVec4(lVal["rotation"]);
                    l.name = lVal.HasMember("name") ? lVal["name"].GetString() : "Directional Light " + std::to_string(ID::NextID());
                    lightSystem->GetDirectionalLights().push_back(l);
                }
                else if (type == "point")
                {
                    PointLightEditor l{};
                    l.color = ReadVec4(lVal["color"]);
                    l.position = ReadVec4(lVal["position"]);
                    l.name = lVal.HasMember("name") ? lVal["name"].GetString() : "Point Light " + std::to_string(ID::NextID());
                    lightSystem->GetPointLights().push_back(l);
                }
                else if (type == "spot")
                {
                    SpotLightEditor l{};
                    l.color = ReadVec4(lVal["color"]);
                    l.position = ReadVec4(lVal["position"]);
                    l.rotation = ReadVec4(lVal["rotation"]);
                    l.params = ReadVec4(lVal["params"]);
                    l.name = lVal.HasMember("name") ? lVal["name"].GetString() : "Spot Light " + std::to_string(ID::NextID());
                    lightSystem->GetSpotLights().push_back(l);
                }
                else if (type == "area")
                {
                    AreaLightEditor l{};
                    l.color = ReadVec4(lVal["color"]);
                    l.position = ReadVec4(lVal["position"]);
                    l.rotation = ReadVec4(lVal["rotation"]);
                    l.size = ReadVec4(lVal["size"]);
                    l.name = lVal.HasMember("name") ? lVal["name"].GetString() : "Area Light " + std::to_string(ID::NextID());
                    lightSystem->GetAreaLights().push_back(l);
                }
            }
        }

        if (d.HasMember("cameras"))
        {
            const auto &cams = d["cameras"];
            for (rapidjson::SizeType i = 0; i < cams.Size(); ++i)
            {
                Camera *cam = nullptr;
                if (i < m_cameras.size())
                    cam = m_cameras[i];
                else
                {
                    cam = new Camera();
                    cam->SetName("Camera_" + std::to_string(ID::NextID()));
                    m_cameras.push_back(cam);
                }

                const auto &cVal = cams[i];
                if (cVal.HasMember("name"))
                    cam->SetName(cVal["name"].GetString());
                if (cVal.HasMember("position"))
                    cam->SetPosition(ReadVec3(cVal["position"]));
                if (cVal.HasMember("euler"))
                    cam->SetEuler(ReadVec3(cVal["euler"]));
                if (cVal.HasMember("fovx"))
                    cam->SetFovx(cVal["fovx"].GetFloat());
                if (cVal.HasMember("near_plane"))
                    cam->SetNearPlane(cVal["near_plane"].GetFloat());
                if (cVal.HasMember("far_plane"))
                    cam->SetFarPlane(cVal["far_plane"].GetFloat());
                if (cVal.HasMember("speed"))
                    cam->SetSpeed(cVal["speed"].GetFloat());
            }

            if (d.HasMember("active_camera"))
            {
                uint32_t activeIndex = d["active_camera"].GetUint();
                if (activeIndex < m_cameras.size())
                    SetActiveCamera(m_cameras[activeIndex]);
            }
        }

        Log::Info("Scene loaded from: " + preload.filePath.string());
    }

    void Scene::LoadScene(const std::filesystem::path &file)
    {
        LoadSceneApply(PreloadScene(file));
    }

    std::string Scene::TakeSnapshot() const
    {
        rapidjson::Document d;
        d.SetObject();
        auto &allocator = d.GetAllocator();

        auto SafeFloat = [](float f)
        { return std::isnan(f) || std::isinf(f) ? 0.0f : f; };

        auto SetVec3 = [&](rapidjson::Value &arr, const vec3 &v)
        {
            arr.SetArray();
            arr.PushBack(SafeFloat(v.x), allocator).PushBack(SafeFloat(v.y), allocator).PushBack(SafeFloat(v.z), allocator);
        };

        auto SetVec4 = [&](rapidjson::Value &arr, const vec4 &v)
        {
            arr.SetArray();
            arr.PushBack(SafeFloat(v.x), allocator).PushBack(SafeFloat(v.y), allocator).PushBack(SafeFloat(v.z), allocator).PushBack(SafeFloat(v.w), allocator);
        };

        auto SetMat4 = [&](rapidjson::Value &arr, const mat4 &m)
        {
            arr.SetArray();
            const float *p = value_ptr(m);
            for (int i = 0; i < 16; i++)
                arr.PushBack(SafeFloat(p[i]), allocator);
        };

        // Settings
        rapidjson::Value settings(rapidjson::kObjectType);
        auto &gSettings = Settings::Get<GlobalSettings>();
        settings.AddMember("shadows", gSettings.shadows, allocator);
        settings.AddMember("shadow_map_size", gSettings.shadow_map_size, allocator);
        settings.AddMember("num_cascades", gSettings.num_cascades, allocator);
        settings.AddMember("render_scale", gSettings.render_scale, allocator);
        settings.AddMember("ssao", gSettings.ssao, allocator);
        settings.AddMember("fxaa", gSettings.fxaa, allocator);
        settings.AddMember("taa", gSettings.taa, allocator);
        settings.AddMember("cas_sharpening", gSettings.cas_sharpening, allocator);
        settings.AddMember("cas_sharpness", gSettings.cas_sharpness, allocator);
        settings.AddMember("ssr", gSettings.ssr, allocator);
        settings.AddMember("tonemapping", gSettings.tonemapping, allocator);
        settings.AddMember("dof", gSettings.dof, allocator);
        settings.AddMember("dof_focus_scale", gSettings.dof_focus_scale, allocator);
        settings.AddMember("dof_blur_range", gSettings.dof_blur_range, allocator);
        settings.AddMember("bloom", gSettings.bloom, allocator);
        settings.AddMember("bloom_strength", gSettings.bloom_strength, allocator);
        settings.AddMember("bloom_range", gSettings.bloom_range, allocator);
        settings.AddMember("motion_blur", gSettings.motion_blur, allocator);
        settings.AddMember("motion_blur_strength", gSettings.motion_blur_strength, allocator);
        settings.AddMember("motion_blur_samples", gSettings.motion_blur_samples, allocator);
        settings.AddMember("IBL", gSettings.IBL, allocator);
        settings.AddMember("IBL_intensity", gSettings.IBL_intensity, allocator);
        settings.AddMember("lights_intensity", gSettings.lights_intensity, allocator);
        settings.AddMember("day", gSettings.day, allocator);
        rapidjson::Value depthBias(rapidjson::kArrayType);
        depthBias.PushBack(gSettings.depth_bias[0], allocator);
        depthBias.PushBack(gSettings.depth_bias[1], allocator);
        depthBias.PushBack(gSettings.depth_bias[2], allocator);
        settings.AddMember("depth_bias", depthBias.Move(), allocator);
        settings.AddMember("draw_grid", gSettings.draw_grid, allocator);
        settings.AddMember("draw_aabbs", gSettings.draw_aabbs, allocator);
        settings.AddMember("render_mode", static_cast<int>(gSettings.render_mode), allocator);
        settings.AddMember("use_Disney_PBR", gSettings.use_Disney_PBR, allocator);
        d.AddMember("settings", settings.Move(), allocator);

        // Build live mesh/source remaps to exclude geometry orphaned by RemoveModel.
        std::vector<int> meshRemap(m_meshes.size(), -1);
        std::vector<int> srcRemap(m_sources.size(), -1);
        {
            std::vector<bool> liveMesh(m_meshes.size(), false);
            for (uint32_t ni = 0; ni < GetNodeCount(); ni++)
            {
                int mr = m_meshRefs[ni];
                if (mr >= 0 && mr < static_cast<int>(m_meshes.size()))
                    liveMesh[mr] = true;
            }
            int newMeshIdx = 0;
            for (int mi = 0; mi < static_cast<int>(m_meshes.size()); mi++)
                if (liveMesh[mi])
                    meshRemap[mi] = newMeshIdx++;

            std::vector<bool> liveSrc(m_sources.size(), false);
            for (int mi = 0; mi < static_cast<int>(m_meshes.size()); mi++)
            {
                if (meshRemap[mi] < 0)
                    continue;
                if (mi < static_cast<int>(m_meshSourceInfos.size()))
                {
                    int si = m_meshSourceInfos[mi].sourceIndex;
                    if (si >= 0 && si < static_cast<int>(m_sources.size()))
                        liveSrc[si] = true;
                }
            }
            int newSrcIdx = 0;
            for (int si = 0; si < static_cast<int>(m_sources.size()); si++)
                if (liveSrc[si])
                    srcRemap[si] = newSrcIdx++;
        }

        // Sources (absolute paths for snapshots, live only)
        rapidjson::Value sourcesArr(rapidjson::kArrayType);
        for (int si = 0; si < static_cast<int>(m_sources.size()); si++)
        {
            if (srcRemap[si] < 0)
                continue;
            const auto &src = m_sources[si];
            rapidjson::Value srcObj(rapidjson::kObjectType);
            if (!src.primitiveType.empty())
            {
                srcObj.AddMember("primitive_type", rapidjson::Value(src.primitiveType.c_str(), allocator).Move(), allocator);
            }
            else
            {
                auto u8p = src.filePath.u8string();
                std::string absPath(u8p.begin(), u8p.end());
                std::replace(absPath.begin(), absPath.end(), '\\', '/');
                srcObj.AddMember("path", rapidjson::Value(absPath.c_str(), static_cast<rapidjson::SizeType>(absPath.length()), allocator).Move(), allocator);
            }
            sourcesArr.PushBack(srcObj.Move(), allocator);
        }
        d.AddMember("sources", sourcesArr.Move(), allocator);

        // Meshes — flat array of live meshes only, with remapped source references
        rapidjson::Value meshesArr(rapidjson::kArrayType);
        for (int mi = 0; mi < static_cast<int>(m_meshes.size()); mi++)
        {
            if (meshRemap[mi] < 0)
                continue;
            const Mesh &mesh = m_meshes[mi];
            rapidjson::Value meshObj(rapidjson::kObjectType);

            if (mi < static_cast<int>(m_meshSourceInfos.size()))
            {
                int rawSrc = m_meshSourceInfos[mi].sourceIndex;
                int remappedSrc = (rawSrc >= 0 && rawSrc < static_cast<int>(srcRemap.size())) ? srcRemap[rawSrc] : -1;
                meshObj.AddMember("source", remappedSrc, allocator);
                meshObj.AddMember("source_mesh", m_meshSourceInfos[mi].sourceMeshIndex, allocator);
            }

            meshObj.AddMember("render_type", static_cast<int>(mesh.renderType), allocator);
            meshObj.AddMember("texture_mask", mesh.textureMask, allocator);

            mat4 persistedF0 = mesh.materialFactors[0];
            mat4 persistedF1 = mesh.materialFactors[1];
            EncodeMaterialFactorsForPersistence(mesh.renderType, persistedF0, persistedF1);

            rapidjson::Value factorsArr(rapidjson::kArrayType);
            rapidjson::Value f0, f1;
            SetMat4(f0, persistedF0);
            SetMat4(f1, persistedF1);
            factorsArr.PushBack(f0.Move(), allocator);
            factorsArr.PushBack(f1.Move(), allocator);
            meshObj.AddMember("material_factors", factorsArr.Move(), allocator);

            rapidjson::Value texturesObj(rapidjson::kObjectType);
            const char *texSlotNames[] = {"base_color", "metallic_roughness", "normal", "occlusion", "emissive"};
            const auto &defaults = ModelAsset::GetDefaultResources();
            for (int i = 0; i < 5; i++)
            {
                Image *img = mesh.images[i].get();
                if (img && !img->GetName().empty() &&
                    img != defaults.black && img != defaults.white && img != defaults.normal)
                {
                    std::string texName = img->GetName();
                    texturesObj.AddMember(rapidjson::Value(texSlotNames[i], allocator).Move(),
                                          rapidjson::Value(texName.c_str(), allocator).Move(), allocator);
                }
            }
            meshObj.AddMember("textures", texturesObj.Move(), allocator);
            meshesArr.PushBack(meshObj.Move(), allocator);
        }
        d.AddMember("meshes", meshesArr.Move(), allocator);

        // Nodes — flat array with global parent indices
        rapidjson::Value nodesArr(rapidjson::kArrayType);
        for (uint32_t ni = 0; ni < GetNodeCount(); ni++)
        {
            rapidjson::Value nodeObj(rapidjson::kObjectType);
            nodeObj.AddMember("name", rapidjson::Value(m_nodeNames[ni].c_str(), allocator).Move(), allocator);

            NodeId *parentNode = m_nodeParents[ni];
            int parentIdx = parentNode ? static_cast<int>(parentNode->index) : -1;
            nodeObj.AddMember("parent", parentIdx, allocator);

            rapidjson::Value localMat;
            SetMat4(localMat, m_localMatrices[ni]);
            nodeObj.AddMember("local_matrix", localMat.Move(), allocator);

            int meshRef = m_meshRefs[ni];
            int remappedMesh = (meshRef >= 0 && meshRef < static_cast<int>(meshRemap.size())) ? meshRemap[meshRef] : -1;
            if (remappedMesh >= 0)
                nodeObj.AddMember("mesh", remappedMesh, allocator);

            nodesArr.PushBack(nodeObj.Move(), allocator);
        }
        d.AddMember("nodes", nodesArr.Move(), allocator);

        // Lights
        auto *lightSystem = GetGlobalSystem<LightSystem>();
        if (lightSystem)
        {
            rapidjson::Value lights(rapidjson::kArrayType);

            for (const auto &l : lightSystem->GetDirectionalLights())
            {
                rapidjson::Value lObj(rapidjson::kObjectType);
                lObj.AddMember("type", "directional", allocator);
                rapidjson::Value color, pos, rot;
                SetVec4(color, l.color);
                SetVec4(pos, l.position);
                SetVec4(rot, l.rotation);
                lObj.AddMember("color", color.Move(), allocator);
                lObj.AddMember("position", pos.Move(), allocator);
                lObj.AddMember("rotation", rot.Move(), allocator);
                lObj.AddMember("name", rapidjson::Value(l.name.c_str(), allocator).Move(), allocator);
                lights.PushBack(lObj.Move(), allocator);
            }
            for (const auto &l : lightSystem->GetPointLights())
            {
                rapidjson::Value lObj(rapidjson::kObjectType);
                lObj.AddMember("type", "point", allocator);
                rapidjson::Value color, pos;
                SetVec4(color, l.color);
                SetVec4(pos, l.position);
                lObj.AddMember("color", color.Move(), allocator);
                lObj.AddMember("position", pos.Move(), allocator);
                lObj.AddMember("name", rapidjson::Value(l.name.c_str(), allocator).Move(), allocator);
                lights.PushBack(lObj.Move(), allocator);
            }
            for (const auto &l : lightSystem->GetSpotLights())
            {
                rapidjson::Value lObj(rapidjson::kObjectType);
                lObj.AddMember("type", "spot", allocator);
                rapidjson::Value color, pos, rot, params;
                SetVec4(color, l.color);
                SetVec4(pos, l.position);
                SetVec4(rot, l.rotation);
                SetVec4(params, l.params);
                lObj.AddMember("color", color.Move(), allocator);
                lObj.AddMember("position", pos.Move(), allocator);
                lObj.AddMember("rotation", rot.Move(), allocator);
                lObj.AddMember("params", params.Move(), allocator);
                lObj.AddMember("name", rapidjson::Value(l.name.c_str(), allocator).Move(), allocator);
                lights.PushBack(lObj.Move(), allocator);
            }
            for (const auto &l : lightSystem->GetAreaLights())
            {
                rapidjson::Value lObj(rapidjson::kObjectType);
                lObj.AddMember("type", "area", allocator);
                rapidjson::Value color, pos, rot, size;
                SetVec4(color, l.color);
                SetVec4(pos, l.position);
                SetVec4(rot, l.rotation);
                SetVec4(size, l.size);
                lObj.AddMember("color", color.Move(), allocator);
                lObj.AddMember("position", pos.Move(), allocator);
                lObj.AddMember("rotation", rot.Move(), allocator);
                lObj.AddMember("size", size.Move(), allocator);
                lObj.AddMember("name", rapidjson::Value(l.name.c_str(), allocator).Move(), allocator);
                lights.PushBack(lObj.Move(), allocator);
            }
            d.AddMember("lights", lights.Move(), allocator);
        }

        // Cameras - always skip active camera position/euler (WASD navigation is not undoable)
        Camera *activeCamera = m_cameras.empty() ? nullptr : GetActiveCamera();
        rapidjson::Value cameras(rapidjson::kArrayType);
        for (auto *camera : m_cameras)
        {
            if (!camera)
                continue;
            rapidjson::Value camObj(rapidjson::kObjectType);
            camObj.AddMember("name", rapidjson::Value(camera->GetName().c_str(), allocator).Move(), allocator);
            if (camera != activeCamera)
            {
                rapidjson::Value pos, eul;
                SetVec3(pos, camera->GetPosition());
                SetVec3(eul, camera->GetEuler());
                camObj.AddMember("position", pos.Move(), allocator);
                camObj.AddMember("euler", eul.Move(), allocator);
            }
            camObj.AddMember("fovx", camera->Fovx(), allocator);
            camObj.AddMember("near_plane", camera->GetNearPlane(), allocator);
            camObj.AddMember("far_plane", camera->GetFarPlane(), allocator);
            camObj.AddMember("speed", camera->GetSpeed(), allocator);
            cameras.PushBack(camObj.Move(), allocator);
        }
        d.AddMember("cameras", cameras.Move(), allocator);

        for (int i = 0; i < static_cast<int>(m_cameras.size()); i++)
        {
            if (m_cameras[i] == GetActiveCamera())
            {
                d.AddMember("active_camera", i, allocator);
                break;
            }
        }

        // Particle Emitters
        if (m_particleManager)
        {
            rapidjson::Value emitters(rapidjson::kArrayType);
            const auto &emitterList = m_particleManager->GetEmitters();
            const auto &emitterNames = m_particleManager->GetEmitterNames();
            for (size_t i = 0; i < emitterList.size(); i++)
            {
                const auto &e = emitterList[i];
                rapidjson::Value eObj(rapidjson::kObjectType);

                std::string name = (i < emitterNames.size()) ? emitterNames[i] : ("Emitter " + std::to_string(i));
                eObj.AddMember("name", rapidjson::Value(name.c_str(), allocator).Move(), allocator);

                rapidjson::Value pos, vel, cs, ce, sl, ph, gr, an;
                SetVec4(pos, e.position);
                SetVec4(vel, e.velocity);
                SetVec4(cs, e.colorStart);
                SetVec4(ce, e.colorEnd);
                SetVec4(sl, e.sizeLife);
                SetVec4(ph, e.physics);
                SetVec4(gr, e.gravity);
                SetVec4(an, e.animation);
                eObj.AddMember("position", pos.Move(), allocator);
                eObj.AddMember("velocity", vel.Move(), allocator);
                eObj.AddMember("color_start", cs.Move(), allocator);
                eObj.AddMember("color_end", ce.Move(), allocator);
                eObj.AddMember("size_life", sl.Move(), allocator);
                eObj.AddMember("physics", ph.Move(), allocator);
                eObj.AddMember("gravity", gr.Move(), allocator);
                eObj.AddMember("animation", an.Move(), allocator);
                eObj.AddMember("texture_index", e.textureIndex, allocator);
                eObj.AddMember("count", e.count, allocator);
                eObj.AddMember("orientation", e.orientation, allocator);

                emitters.PushBack(eObj.Move(), allocator);
            }
            d.AddMember("emitters", emitters.Move(), allocator);
        }

        // Compact JSON for minimal memory usage
        rapidjson::StringBuffer sb;
        rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
        writer.SetMaxDecimalPlaces(6);
        d.Accept(writer);
        return std::string(sb.GetString(), sb.GetSize());
    }

    void Scene::RestoreSnapshot(const std::string &json)
    {
        rapidjson::Document d;
        d.Parse(json.c_str(), json.size());
        if (d.HasParseError())
            return;

        auto ReadVec3 = [](const rapidjson::Value &arr)
        { return vec3(arr[0].GetFloat(), arr[1].GetFloat(), arr[2].GetFloat()); };
        auto ReadVec4 = [](const rapidjson::Value &arr)
        { return vec4(arr[0].GetFloat(), arr[1].GetFloat(), arr[2].GetFloat(), arr[3].GetFloat()); };
        auto ReadMat4 = [](const rapidjson::Value &arr)
        {
            mat4 m;
            float *p = value_ptr(m);
            for (int i = 0; i < 16; i++)
                p[i] = arr[i].GetFloat();
            return m;
        };

        // 1. Restore settings (cheap, always overwrite)
        if (d.HasMember("settings"))
        {
            const auto &s = d["settings"];
            auto &g = Settings::Get<GlobalSettings>();
            if (s.HasMember("shadows"))
                g.shadows = s["shadows"].GetBool();
            if (s.HasMember("shadow_map_size"))
                g.shadow_map_size = s["shadow_map_size"].GetUint();
            if (s.HasMember("num_cascades"))
                g.num_cascades = s["num_cascades"].GetUint();
            if (s.HasMember("render_scale"))
                g.render_scale = s["render_scale"].GetFloat();
            if (s.HasMember("ssao"))
                g.ssao = s["ssao"].GetBool();
            if (s.HasMember("fxaa"))
                g.fxaa = s["fxaa"].GetBool();
            if (s.HasMember("taa"))
                g.taa = s["taa"].GetBool();
            if (s.HasMember("cas_sharpening"))
                g.cas_sharpening = s["cas_sharpening"].GetBool();
            if (s.HasMember("cas_sharpness"))
                g.cas_sharpness = s["cas_sharpness"].GetFloat();
            if (s.HasMember("ssr"))
                g.ssr = s["ssr"].GetBool();
            if (s.HasMember("tonemapping"))
                g.tonemapping = s["tonemapping"].GetBool();
            if (s.HasMember("dof"))
                g.dof = s["dof"].GetBool();
            if (s.HasMember("dof_focus_scale"))
                g.dof_focus_scale = s["dof_focus_scale"].GetFloat();
            if (s.HasMember("dof_blur_range"))
                g.dof_blur_range = s["dof_blur_range"].GetFloat();
            if (s.HasMember("bloom"))
                g.bloom = s["bloom"].GetBool();
            if (s.HasMember("bloom_strength"))
                g.bloom_strength = s["bloom_strength"].GetFloat();
            if (s.HasMember("bloom_range"))
                g.bloom_range = s["bloom_range"].GetFloat();
            if (s.HasMember("motion_blur"))
                g.motion_blur = s["motion_blur"].GetBool();
            if (s.HasMember("motion_blur_strength"))
                g.motion_blur_strength = s["motion_blur_strength"].GetFloat();
            if (s.HasMember("motion_blur_samples"))
                g.motion_blur_samples = s["motion_blur_samples"].GetInt();
            if (s.HasMember("IBL"))
                g.IBL = s["IBL"].GetBool();
            if (s.HasMember("IBL_intensity"))
                g.IBL_intensity = s["IBL_intensity"].GetFloat();
            if (s.HasMember("lights_intensity"))
                g.lights_intensity = s["lights_intensity"].GetFloat();
            if (s.HasMember("day"))
                g.day = s["day"].GetBool();
            if (s.HasMember("depth_bias"))
            {
                g.depth_bias[0] = s["depth_bias"][0].GetFloat();
                g.depth_bias[1] = s["depth_bias"][1].GetFloat();
                g.depth_bias[2] = s["depth_bias"][2].GetFloat();
            }
            if (s.HasMember("draw_grid"))
                g.draw_grid = s["draw_grid"].GetBool();
            if (s.HasMember("draw_aabbs"))
                g.draw_aabbs = s["draw_aabbs"].GetBool();
            if (s.HasMember("render_mode"))
                g.render_mode = static_cast<RenderMode>(s["render_mode"].GetInt());
            if (s.HasMember("use_Disney_PBR"))
                g.use_Disney_PBR = s["use_Disney_PBR"].GetBool();

            MarkUniformsDirty();
        }

        // 2. Restore scene graph (flat format from TakeSnapshot)
        if (d.HasMember("sources") && d.HasMember("nodes"))
        {
            const auto &snapshotSources = d["sources"];
            const auto &snapshotNodes = d["nodes"];
            uint32_t snapshotNodeCount = snapshotNodes.Size();
            uint32_t snapshotMeshCount = d.HasMember("meshes") ? d["meshes"].Size() : 0;

            // Check if geometry is unchanged (same sources in same order, same counts)
            bool sourcesMatch = (snapshotSources.Size() == static_cast<rapidjson::SizeType>(m_sources.size()));
            if (sourcesMatch)
            {
                auto GetSourceId = [](const SceneSource &s) -> std::string
                {
                    if (!s.primitiveType.empty())
                        return "prim:" + s.primitiveType;
                    auto u8 = s.filePath.u8string();
                    return "file:" + std::string(u8.begin(), u8.end());
                };
                auto GetSnapshotSourceId = [](const rapidjson::Value &sv) -> std::string
                {
                    if (sv.HasMember("primitive_type"))
                        return "prim:" + std::string(sv["primitive_type"].GetString());
                    if (sv.HasMember("path"))
                        return "file:" + std::string(sv["path"].GetString());
                    return "";
                };
                for (rapidjson::SizeType i = 0; i < snapshotSources.Size(); i++)
                {
                    if (GetSourceId(m_sources[i]) != GetSnapshotSourceId(snapshotSources[i]))
                    {
                        sourcesMatch = false;
                        break;
                    }
                }
            }

            bool geometryMatch = sourcesMatch &&
                                 snapshotNodeCount == GetNodeCount() &&
                                 snapshotMeshCount == static_cast<uint32_t>(m_meshes.size());

            if (geometryMatch)
            {
                // Fast path: update SoA in-place (no geometry reload)
                // Update nodes
                for (uint32_t ni = 0; ni < snapshotNodeCount; ni++)
                {
                    const auto &nv = snapshotNodes[ni];
                    NodeId *node = m_nodeIds[ni];

                    if (nv.HasMember("name"))
                        SetNodeName(node, nv["name"].GetString());
                    if (nv.HasMember("parent"))
                    {
                        int parentIdx = nv["parent"].GetInt();
                        NodeId *newParent = (parentIdx >= 0 && parentIdx < static_cast<int>(snapshotNodeCount))
                                                ? m_nodeIds[parentIdx]
                                                : nullptr;
                        if (GetParent(node) != newParent)
                            ReparentNode(node, newParent);
                    }
                    if (nv.HasMember("local_matrix"))
                        SetLocalMatrix(node, ReadMat4(nv["local_matrix"]));
                }

                // Update meshes (materials only)
                if (d.HasMember("meshes"))
                {
                    const auto &meshesVal = d["meshes"];
                    for (rapidjson::SizeType mi = 0; mi < meshesVal.Size() && mi < static_cast<rapidjson::SizeType>(m_meshes.size()); mi++)
                    {
                        const auto &mVal = meshesVal[mi];
                        Mesh &mesh = m_meshes[mi];

                        if (mVal.HasMember("render_type"))
                            mesh.renderType = static_cast<RenderType>(mVal["render_type"].GetInt());
                        if (mVal.HasMember("texture_mask"))
                            mesh.textureMask = mVal["texture_mask"].GetUint();
                        if (mVal.HasMember("material_factors"))
                        {
                            mesh.materialFactors[0] = ReadMat4(mVal["material_factors"][0]);
                            mesh.materialFactors[1] = ReadMat4(mVal["material_factors"][1]);
                            DecodeMaterialFactorsFromPersistence(mesh.renderType, mesh.materialFactors[0], mesh.materialFactors[1]);
                        }
                    }
                }

                // Mark all nodes dirty
                for (uint32_t ni = 0; ni < GetNodeCount(); ni++)
                    MarkNodeDirty(m_nodeIds[ni]);
            }
            else
            {
                // Slow path: geometry changed — clear everything and reload via LoadScene-style path

                // Clear old scene data synchronously (mirrors LoadSceneApply)
                for (NodeId *id : m_nodeIds)
                    delete id;
                for (NodeId *id : m_freeNodeIds)
                    delete id;
                m_nodeIds.clear();
                m_freeNodeIds.clear();
                m_nodeNames.clear();
                m_localMatrices.clear();
                m_nodeParents.clear();
                m_nodeChildren.clear();
                m_componentFlags.clear();
                m_meshRefs.clear();
                m_nodeRuntime.clear();
                m_nodesMoved.clear();
                m_nodesDirty = false;

                m_meshes.clear();
                m_meshRuntimes.clear();
                m_vertexStore.clear();
                m_positionUvStore.clear();
                m_aabbVertexStore.clear();
                m_indexStore.clear();
                m_imageStore.clear();
                m_samplerStore.clear();

                m_sources.clear();
                m_meshSourceInfos.clear();
                m_modelRootNodes.clear();

                for (auto *model : m_models)
                    delete model;
                m_models.clear();

                // Reload sources
                Queue *queue = RHII.GetMainQueue();
                CommandBuffer *cmd = queue->AcquireCommandBuffer();
                cmd->Begin();

                std::vector<std::vector<int>> sourceMeshMaps(snapshotSources.Size());
                for (rapidjson::SizeType si = 0; si < snapshotSources.Size(); si++)
                {
                    const auto &sv = snapshotSources[si];
                    ModelAsset *model = nullptr;

                    if (sv.HasMember("primitive_type"))
                    {
                        std::string ptype = sv["primitive_type"].GetString();
                        if (ptype == "cube")
                            model = Primitives::CreateCube();
                        else if (ptype == "sphere")
                            model = Primitives::CreateSphere();
                        else if (ptype == "plane")
                            model = Primitives::CreatePlane();
                        else if (ptype == "cylinder")
                            model = Primitives::CreateCylinder();
                        else if (ptype == "cone")
                            model = Primitives::CreateCone();
                        else if (ptype == "quad")
                            model = Primitives::CreateQuad();
                    }
                    else if (sv.HasMember("path"))
                    {
                        std::filesystem::path modelPath = sv["path"].GetString();
                        if (!std::filesystem::is_directory(modelPath))
                            model = ModelAsset::Load(modelPath);
                    }

                    if (model)
                    {
                        int sourceIndex = static_cast<int>(m_sources.size());
                        SceneSource source;
                        source.filePath = model->GetFilePath();
                        source.primitiveType = model->GetPrimitiveType();
                        m_sources.push_back(std::move(source));

                        sourceMeshMaps[si] = AddModelGeometry(model, sourceIndex);
                        m_models.insert(model->GetId(), model);
                    }
                }

                // Apply mesh overrides
                if (d.HasMember("meshes"))
                {
                    const auto &meshesVal = d["meshes"];
                    for (rapidjson::SizeType mi = 0; mi < meshesVal.Size(); mi++)
                    {
                        const auto &mVal = meshesVal[mi];
                        int sceneMeshIdx = -1;
                        if (mVal.HasMember("source") && mVal.HasMember("source_mesh"))
                        {
                            int srcIdx = mVal["source"].GetInt();
                            int srcMesh = mVal["source_mesh"].GetInt();
                            if (srcIdx >= 0 && srcIdx < static_cast<int>(sourceMeshMaps.size()) &&
                                srcMesh >= 0 && srcMesh < static_cast<int>(sourceMeshMaps[srcIdx].size()))
                                sceneMeshIdx = sourceMeshMaps[srcIdx][srcMesh];
                        }
                        if (sceneMeshIdx < 0 || sceneMeshIdx >= static_cast<int>(m_meshes.size()))
                            continue;

                        Mesh &mesh = m_meshes[sceneMeshIdx];
                        if (mVal.HasMember("render_type"))
                            mesh.renderType = static_cast<RenderType>(mVal["render_type"].GetInt());
                        if (mVal.HasMember("texture_mask"))
                            mesh.textureMask = mVal["texture_mask"].GetUint();
                        if (mVal.HasMember("material_factors"))
                        {
                            mesh.materialFactors[0] = ReadMat4(mVal["material_factors"][0]);
                            mesh.materialFactors[1] = ReadMat4(mVal["material_factors"][1]);
                            DecodeMaterialFactorsFromPersistence(mesh.renderType, mesh.materialFactors[0], mesh.materialFactors[1]);
                        }
                    }
                }

                // Create nodes from flat array (two-pass)
                std::vector<NodeId *> nodeMap(snapshotNodeCount, nullptr);
                for (uint32_t ni = 0; ni < snapshotNodeCount; ni++)
                {
                    const auto &nv = snapshotNodes[ni];
                    std::string name = nv.HasMember("name") ? nv["name"].GetString() : "";
                    NodeId *node = CreateNode(name);
                    if (nv.HasMember("local_matrix"))
                        SetLocalMatrix(node, ReadMat4(nv["local_matrix"]), false);
                    if (nv.HasMember("mesh"))
                    {
                        int meshIdx = nv["mesh"].GetInt();
                        if (meshIdx >= 0 && meshIdx < static_cast<int>(m_meshes.size()))
                            SetMeshRef(node, meshIdx);
                    }
                    nodeMap[ni] = node;
                }
                for (uint32_t ni = 0; ni < snapshotNodeCount; ni++)
                {
                    const auto &nv = snapshotNodes[ni];
                    if (nv.HasMember("parent"))
                    {
                        int parentIdx = nv["parent"].GetInt();
                        if (parentIdx >= 0 && parentIdx < static_cast<int>(nodeMap.size()) && nodeMap[parentIdx])
                            ReparentNode(nodeMap[ni], nodeMap[parentIdx]);
                    }
                }
                for (NodeId *node : nodeMap)
                {
                    if (node)
                        MarkNodeDirty(node);
                }
                UpdateNodeMatrices();

                UploadBuffers(cmd);

                cmd->End();
                queue->Submit(1, &cmd, nullptr, nullptr);
                cmd->Wait();
                queue->ReturnCommandBuffer(cmd);
            }
        }

        // 3. Restore lights (clear and recreate - cheap)
        auto *lightSystem = GetGlobalSystem<LightSystem>();
        if (d.HasMember("lights") && lightSystem)
        {
            lightSystem->GetDirectionalLights().clear();
            lightSystem->GetPointLights().clear();
            lightSystem->GetSpotLights().clear();
            lightSystem->GetAreaLights().clear();

            const auto &lights = d["lights"];
            for (const auto &lVal : lights.GetArray())
            {
                std::string type = lVal["type"].GetString();
                if (type == "directional")
                {
                    DirectionalLightEditor l{};
                    l.color = ReadVec4(lVal["color"]);
                    l.position = ReadVec4(lVal["position"]);
                    if (lVal.HasMember("rotation"))
                        l.rotation = ReadVec4(lVal["rotation"]);
                    l.name = lVal.HasMember("name") ? lVal["name"].GetString() : "Directional Light";
                    lightSystem->GetDirectionalLights().push_back(l);
                }
                else if (type == "point")
                {
                    PointLightEditor l{};
                    l.color = ReadVec4(lVal["color"]);
                    l.position = ReadVec4(lVal["position"]);
                    l.name = lVal.HasMember("name") ? lVal["name"].GetString() : "Point Light";
                    lightSystem->GetPointLights().push_back(l);
                }
                else if (type == "spot")
                {
                    SpotLightEditor l{};
                    l.color = ReadVec4(lVal["color"]);
                    l.position = ReadVec4(lVal["position"]);
                    l.rotation = ReadVec4(lVal["rotation"]);
                    l.params = ReadVec4(lVal["params"]);
                    l.name = lVal.HasMember("name") ? lVal["name"].GetString() : "Spot Light";
                    lightSystem->GetSpotLights().push_back(l);
                }
                else if (type == "area")
                {
                    AreaLightEditor l{};
                    l.color = ReadVec4(lVal["color"]);
                    l.position = ReadVec4(lVal["position"]);
                    l.rotation = ReadVec4(lVal["rotation"]);
                    l.size = ReadVec4(lVal["size"]);
                    l.name = lVal.HasMember("name") ? lVal["name"].GetString() : "Area Light";
                    lightSystem->GetAreaLights().push_back(l);
                }
            }
        }

        // 4. Restore cameras (update in-place)
        if (d.HasMember("cameras"))
        {
            const auto &cams = d["cameras"];

            // Remove excess cameras
            while (m_cameras.size() > cams.Size() && m_cameras.size() > 1)
            {
                delete m_cameras.back();
                m_cameras.pop_back();
            }

            for (rapidjson::SizeType i = 0; i < cams.Size(); ++i)
            {
                Camera *cam = nullptr;
                if (i < m_cameras.size())
                    cam = m_cameras[i];
                else
                {
                    cam = new Camera();
                    cam->SetName("Camera_" + std::to_string(ID::NextID()));
                    m_cameras.push_back(cam);
                }

                const auto &cv = cams[i];
                if (cv.HasMember("name"))
                    cam->SetName(cv["name"].GetString());
                if (cv.HasMember("position"))
                    cam->SetPosition(ReadVec3(cv["position"]));
                if (cv.HasMember("euler"))
                    cam->SetEuler(ReadVec3(cv["euler"]));
                if (cv.HasMember("fovx"))
                    cam->SetFovx(cv["fovx"].GetFloat());
                if (cv.HasMember("near_plane"))
                    cam->SetNearPlane(cv["near_plane"].GetFloat());
                if (cv.HasMember("far_plane"))
                    cam->SetFarPlane(cv["far_plane"].GetFloat());
                if (cv.HasMember("speed"))
                    cam->SetSpeed(cv["speed"].GetFloat());
            }

            if (d.HasMember("active_camera"))
            {
                uint32_t idx = d["active_camera"].GetUint();
                if (idx < m_cameras.size())
                    SetActiveCamera(m_cameras[idx]);
            }
        }

        // 5. Restore particle emitters
        if (d.HasMember("emitters") && m_particleManager)
        {
            auto &emitters = m_particleManager->GetEmitters();
            auto &names = m_particleManager->GetEmitterNames();
            emitters.clear();
            names.clear();

            const auto &emArr = d["emitters"];
            for (const auto &ev : emArr.GetArray())
            {
                ParticleEmitter e{};
                if (ev.HasMember("position"))
                    e.position = ReadVec4(ev["position"]);
                if (ev.HasMember("velocity"))
                    e.velocity = ReadVec4(ev["velocity"]);
                if (ev.HasMember("color_start"))
                    e.colorStart = ReadVec4(ev["color_start"]);
                if (ev.HasMember("color_end"))
                    e.colorEnd = ReadVec4(ev["color_end"]);
                if (ev.HasMember("size_life"))
                    e.sizeLife = ReadVec4(ev["size_life"]);
                if (ev.HasMember("physics"))
                    e.physics = ReadVec4(ev["physics"]);
                if (ev.HasMember("gravity"))
                    e.gravity = ReadVec4(ev["gravity"]);
                if (ev.HasMember("animation"))
                    e.animation = ReadVec4(ev["animation"]);
                if (ev.HasMember("texture_index"))
                    e.textureIndex = ev["texture_index"].GetUint();
                if (ev.HasMember("count"))
                    e.count = ev["count"].GetUint();
                if (ev.HasMember("orientation"))
                    e.orientation = ev["orientation"].GetUint();
                emitters.push_back(e);
                names.push_back(ev.HasMember("name") ? ev["name"].GetString() : ("Emitter " + std::to_string(names.size())));
            }
            m_particleManager->UpdateEmitterBuffer();
        }

        // Update texture descriptors (needed when material texture masks change)
        UpdateTextures();

        // Update render pass descriptor sets (needed when shadows/day settings change)
        LightOpaquePass *lop = GetGlobalComponent<LightOpaquePass>();
        LightTransparentPass *ltp = GetGlobalComponent<LightTransparentPass>();
        RayTracingPass *rtp = GetGlobalComponent<RayTracingPass>();
        if (lop)
            lop->UpdateDescriptorSets();
        if (ltp)
            ltp->UpdateDescriptorSets();
        if (rtp)
            rtp->UpdateDescriptorSets();
    }
} // namespace pe
