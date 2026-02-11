#include "Scene/Scene.h"
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
#include "Scene/Model.h"
#include "Scene/SelectionManager.h"
#include "Systems/LightSystem.h"
#include "Systems/RendererSystem.h"

#define _SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING
#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/ostreamwrapper.h"
#include "rapidjson/prettywriter.h"

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

    void Scene::AddModel(Model *model)
    {
        m_models.insert(model->GetId(), model);
    }

    void Scene::RemoveModel(Model *model)
    {
        if (SelectionManager::Instance().GetSelectedModel() == model)
            SelectionManager::Instance().ClearSelection();

        if (m_models.erase(model->GetId()))
            delete model;
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
                if (SelectionManager::Instance().GetSelectedNodeIndex() == index)
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
        // Reset offsets relative to the model
        for (auto &modelPtr : m_models)
        {
            Model &model = *modelPtr;
            uint32_t currentVertexOffset = 0;
            uint32_t currentIndexOffset = 0;
            uint32_t currentPositionsOffset = 0;
            size_t currentAabbVertexOffset = 0;

            for (auto &meshInfo : model.GetMeshInfos())
            {
                meshInfo.vertexOffset = currentVertexOffset;
                meshInfo.indexOffset = currentIndexOffset;
                meshInfo.positionsOffset = currentPositionsOffset;
                meshInfo.aabbVertexOffset = currentAabbVertexOffset;

                currentVertexOffset += meshInfo.verticesCount;
                currentIndexOffset += meshInfo.indicesCount;
                currentPositionsOffset += meshInfo.verticesCount;
                currentAabbVertexOffset += 8;
            }
        }

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
        m_meshCount = 0;
        m_indicesCount = 0;
        m_verticesCount = 0;
        m_positionsCount = 0;
        m_aabbVerticesCount = 0;

        for (auto &modelPtr : m_models)
        {
            Model &model = *modelPtr;
            m_indicesCount += static_cast<uint32_t>(model.m_indices.size());
            m_verticesCount += static_cast<uint32_t>(model.m_vertices.size());
            m_positionsCount += static_cast<uint32_t>(model.m_positionUvs.size());
            m_aabbVerticesCount += static_cast<uint32_t>(model.m_aabbVertices.size());

            const int nodeCount = model.GetNodeCount();
            const auto &meshInfos = model.GetMeshInfos();
            for (int i = 0; i < nodeCount; i++)
            {
                int meshIndex = model.GetNodeMesh(i);
                if (meshIndex < 0 || meshIndex >= static_cast<int>(meshInfos.size()))
                    continue;

                if (meshInfos[meshIndex].indicesCount == 0)
                    continue;

                m_meshCount++;
            }
        }

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
        uint32_t currentIndicesCount = 0;

        for (auto &modelPtr : m_models)
        {
            Model &model = *modelPtr;
            const auto &indices = model.GetIndices();
            cmd->CopyBufferStaged(m_buffer, const_cast<uint32_t *>(indices.data()), sizeof(uint32_t) * indices.size(), currentIndicesCount * sizeof(uint32_t));

            auto &meshesInfo = model.GetMeshInfos();
            for (auto &meshInfo : meshesInfo)
                meshInfo.indexOffset += currentIndicesCount;

            currentIndicesCount += static_cast<uint32_t>(indices.size());
        }

        if (m_indicesCount > 0)
        {
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
        auto &progress = gSettings.loading_current;
        auto &total = gSettings.loading_total;
        auto &loading = gSettings.loading_name;

        total = m_verticesCount + m_positionsCount + m_aabbVerticesCount;
        progress = 0;
        loading = "Uploading to GPU";

        uint32_t currentVerticesCount = 0;
        for (auto &modelPtr : m_models)
        {
            Model &model = *modelPtr;
            const auto &vertices = model.GetVertices();
            const uint32_t vertexCount = static_cast<uint32_t>(vertices.size());
            cmd->CopyBufferStaged(m_buffer, const_cast<Vertex *>(vertices.data()), sizeof(Vertex) * vertexCount, m_verticesOffset + currentVerticesCount * sizeof(Vertex));
            for (auto &meshInfo : model.GetMeshInfos())
                meshInfo.vertexOffset += currentVerticesCount;
            currentVerticesCount += vertexCount;

            progress += vertexCount;
        }

        if (m_verticesCount > 0)
        {
            BufferBarrierInfo vertexBarrierInfo{};
            vertexBarrierInfo.buffer = m_buffer;
            vertexBarrierInfo.stageMask = vk::PipelineStageFlagBits2::eVertexInput;
            vertexBarrierInfo.accessMask = vk::AccessFlagBits2::eVertexAttributeRead;
            vertexBarrierInfo.size = m_verticesCount * sizeof(Vertex);
            vertexBarrierInfo.offset = m_verticesOffset;
            cmd->BufferBarrier(vertexBarrierInfo);
        }

        uint32_t currentPositionsCount = 0;
        for (auto &modelPtr : m_models)
        {
            Model &model = *modelPtr;
            const auto &positionUvs = model.GetPositionUvs();
            const uint32_t positionCount = static_cast<uint32_t>(positionUvs.size());
            cmd->CopyBufferStaged(m_buffer, const_cast<PositionUvVertex *>(positionUvs.data()), positionCount * sizeof(PositionUvVertex), m_positionsOffset + currentPositionsCount * sizeof(PositionUvVertex));
            for (auto &meshInfo : model.GetMeshInfos())
                meshInfo.positionsOffset += currentPositionsCount;
            currentPositionsCount += positionCount;
            progress += positionCount;
        }

        if (m_positionsCount > 0)
        {
            BufferBarrierInfo posVertexBarrierInfo{};
            posVertexBarrierInfo.buffer = m_buffer;
            posVertexBarrierInfo.stageMask = vk::PipelineStageFlagBits2::eVertexInput;
            posVertexBarrierInfo.accessMask = vk::AccessFlagBits2::eVertexAttributeRead;
            posVertexBarrierInfo.size = m_positionsCount * sizeof(PositionUvVertex);
            posVertexBarrierInfo.offset = m_positionsOffset;
            cmd->BufferBarrier(posVertexBarrierInfo);
        }

        size_t currentAabbVertexCount = 0;
        for (auto &modelPtr : m_models)
        {
            Model &model = *modelPtr;
            const auto &aabbVertices = model.GetAabbVertices();
            const size_t aabbVertexCount = aabbVertices.size();
            cmd->CopyBufferStaged(m_buffer, const_cast<AabbVertex *>(aabbVertices.data()), aabbVertexCount * sizeof(AabbVertex), m_aabbVerticesOffset + currentAabbVertexCount * sizeof(AabbVertex));
            for (auto &meshInfo : model.GetMeshInfos())
                meshInfo.aabbVertexOffset += currentAabbVertexCount;
            currentAabbVertexCount += aabbVertexCount;
            progress += static_cast<uint32_t>(aabbVertexCount);
        }

        if (m_aabbVerticesCount > 0)
        {
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
        for (auto &modelPtr : m_models)
        {
            Model &model = *modelPtr;
            auto &meshesInfo = model.GetMeshInfos();
            const int nodeCount = model.GetNodeCount();
            for (int nodeIndex = 0; nodeIndex < nodeCount; nodeIndex++)
            {
                int meshIndex = model.GetNodeMesh(nodeIndex);
                if (meshIndex < 0 || meshIndex >= static_cast<int>(meshesInfo.size()))
                    continue;

                MeshInfo &meshInfo = meshesInfo[meshIndex];
                model.GetNodeInfos()[nodeIndex].dataOffset = storageSize;

                storageSize += meshInfo.dataSize;
                storageSize += sizeof(mat4) * 2; // material data (factors)
            }
        }

        uint32_t i = 0;
        for (auto &storage : m_storages)
        {
            storage = Buffer::Create(
                storageSize,
                vk::BufferUsageFlagBits2::eStorageBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                "storage_Geometry_buffer_" + std::to_string(i++));
        }
    }

    void Scene::MarkUniformsDirty()
    {
        const uint32_t frameCount = RHII.GetSwapchainImageCount();

        for (auto &modelPtr : m_models)
        {
            Model &model = *modelPtr;

            auto &modelDirtyUniforms = model.GetDirtyUniforms();
            modelDirtyUniforms.resize(frameCount, true);
            std::fill(modelDirtyUniforms.begin(), modelDirtyUniforms.end(), true);

            for (auto &nodeInfo : model.GetNodeInfos())
            {
                nodeInfo.dirtyUniforms.resize(frameCount, true);
                std::fill(nodeInfo.dirtyUniforms.begin(), nodeInfo.dirtyUniforms.end(), true);
            }
        }
    }

    void Scene::CreateIndirectBuffers(CommandBuffer *cmd)
    {
        uint32_t indirectCount = 0;
        m_indirectCommands.clear();
        m_indirectCommands.reserve(m_meshCount);
        for (auto &modelPtr : m_models)
        {
            Model &model = *modelPtr;
            auto &meshesInfo = model.GetMeshInfos();
            const int nodeCount = model.GetNodeCount();
            for (int nodeIndex = 0; nodeIndex < nodeCount; nodeIndex++)
            {
                int meshIndex = model.GetNodeMesh(nodeIndex);
                if (meshIndex < 0 || meshIndex >= static_cast<int>(meshesInfo.size()))
                    continue;

                MeshInfo &meshInfo = meshesInfo[meshIndex];
                model.GetNodeInfos()[nodeIndex].indirectIndex = indirectCount;

                vk::DrawIndexedIndirectCommand indirectCommand{};
                indirectCommand.indexCount = meshInfo.indicesCount;
                indirectCommand.instanceCount = 1;
                indirectCommand.firstIndex = meshInfo.indexOffset;
                indirectCommand.vertexOffset = meshInfo.vertexOffset;
                indirectCommand.firstInstance = indirectCount;
                m_indirectCommands.push_back(indirectCommand);

                indirectCount++;
            }
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
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                "indirect_Geometry_buffer_" + std::to_string(i++));
        }
    }

    void Scene::UpdateImageViews()
    {
        size_t totalImageCount = 0;
        for (auto &modelPtr : m_models)
            totalImageCount += modelPtr->GetImages().size();

        m_imageViews.clear();
        m_imageViews.reserve(totalImageCount);
        OrderedMap<Image *, uint32_t> imagesMap{};
        for (auto &modelPtr : m_models)
        {
            Model &model = *modelPtr;

            for (Image *image : model.GetImages())
            {
                auto insertResult = imagesMap.insert(image, static_cast<uint32_t>(m_imageViews.size()));
                if (insertResult.first)
                    m_imageViews.push_back(image->GetSRV());
            }

            const auto &defaults = Model::GetDefaultResources();
            for (auto &meshInfo : model.GetMeshInfos())
            {
                for (int k = 0; k < 5; k++)
                {
                    Image *image = meshInfo.images[k];
                    bool isDefault = (image == defaults.black || image == defaults.white || image == defaults.normal);

                    if (image && !isDefault)
                        meshInfo.viewsIndex[k] = imagesMap[image];
                    else
                        meshInfo.viewsIndex[k] = 0xFFFFFFFF;
                }
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
        for (auto &modelPtr : m_models)
        {
            Model &model = *modelPtr;
            auto &meshesInfo = model.GetMeshInfos();
            const int nodeCount = model.GetNodeCount();
            for (int nodeIndex = 0; nodeIndex < nodeCount; nodeIndex++)
            {
                int meshIndex = model.GetNodeMesh(nodeIndex);
                if (meshIndex < 0 || meshIndex >= static_cast<int>(meshesInfo.size()))
                    continue;

                MeshInfo &meshInfo = meshesInfo[meshIndex];

                Mesh_Constants constants{};
                constants.alphaCut = (meshInfo.renderType == RenderType::AlphaCut) ? meshInfo.materialFactors[0][2][2] : 0.0f;
                constants.meshDataOffset = static_cast<uint32_t>(model.GetNodeInfos()[nodeIndex].dataOffset);
                constants.textureMask = meshInfo.textureMask;
                for (int k = 0; k < 5; k++)
                    constants.meshImageIndex[k] = meshInfo.viewsIndex[k];

                BufferRange range{};
                range.data = &constants;
                range.offset = offset;
                range.size = sizeof(Mesh_Constants);
                m_meshConstants->Copy(1, &range, true);

                offset += sizeof(Mesh_Constants);
            }
        }
        m_meshConstants->Flush();
        m_meshConstants->Unmap();
    }

    void Scene::CullNode(Model &model, int node)
    {
        const Camera &camera = *m_cameras[0];
        bool frustumCulling = Settings::Get<GlobalSettings>().frustum_culling;

        int mesh = model.GetNodeMesh(node);
        if (mesh < 0)
            return;

        MeshInfo &meshInfo = model.GetMeshInfos()[mesh];

        std::vector<DrawInfo> localOpaque;
        std::vector<DrawInfo> localAlphaCut;
        std::vector<DrawInfo> localAlphaBlend;
        std::vector<DrawInfo> localTransmission;
        localOpaque.reserve(1);
        localAlphaCut.reserve(1);
        localAlphaBlend.reserve(1);
        localTransmission.reserve(1);

        NodeInfo &nodeInfo = model.GetNodeInfos()[node];
        bool cull = frustumCulling ? !camera.AABBInFrustum(nodeInfo.worldBoundingBox) : false;
        if (!cull)
        {
            vec3 center = nodeInfo.worldBoundingBox.GetCenter();
            float distance = distance2(camera.GetPosition(), center);

            switch (meshInfo.renderType)
            {
            case RenderType::Opaque:
                localOpaque.push_back(DrawInfo{&model, node, distance});
                break;
            case RenderType::AlphaCut:
                localAlphaCut.push_back(DrawInfo{&model, node, distance});
                break;
            case RenderType::AlphaBlend:
                localAlphaBlend.push_back(DrawInfo{&model, node, distance});
                break;
            case RenderType::Transmission:
                localTransmission.push_back(DrawInfo{&model, node, distance});
                break;
            }
        }

        std::scoped_lock lock(m_drawInfosMutex);
        m_drawInfosOpaque.insert(m_drawInfosOpaque.end(), localOpaque.begin(), localOpaque.end());
        m_drawInfosAlphaCut.insert(m_drawInfosAlphaCut.end(), localAlphaCut.begin(), localAlphaCut.end());
        m_drawInfosAlphaBlend.insert(m_drawInfosAlphaBlend.end(), localAlphaBlend.begin(), localAlphaBlend.end());
        m_drawInfosTransmission.insert(m_drawInfosTransmission.end(), localTransmission.begin(), localTransmission.end());
    }

    void Scene::UpdateUniformData()
    {
        uint32_t frame = RHII.GetFrameIndex();

        m_storages[frame]->Map();

        m_frameData.viewProjection = m_cameras[0]->GetViewProjection();
        m_frameData.previousViewProjection = m_cameras[0]->GetPreviousViewProjection();
        m_frameData.invView = m_cameras[0]->GetInvView();
        m_frameData.invProjection = m_cameras[0]->GetInvProjection();

        BufferRange range{};
        range.data = &m_frameData;
        range.size = sizeof(PerFrameData);
        range.offset = 0;
        m_storages[frame]->Copy(1, &range, true);

        std::vector<uint32_t> ids{};
        ids.reserve(m_drawInfosOpaque.size() + m_drawInfosAlphaCut.size() + m_drawInfosAlphaBlend.size() + m_drawInfosTransmission.size());
        for (auto &drawInfo : m_drawInfosOpaque)
        {
            auto &model = *drawInfo.model;
            ids.push_back(model.GetNodeInfos()[drawInfo.node].indirectIndex);
        }
        for (auto &drawInfo : m_drawInfosAlphaCut)
        {
            auto &model = *drawInfo.model;
            ids.push_back(model.GetNodeInfos()[drawInfo.node].indirectIndex);
        }
        for (auto &drawInfo : m_drawInfosTransmission)
        {
            auto &model = *drawInfo.model;
            ids.push_back(model.GetNodeInfos()[drawInfo.node].indirectIndex);
        }
        for (auto &drawInfo : m_drawInfosAlphaBlend)
        {
            auto &model = *drawInfo.model;
            ids.push_back(model.GetNodeInfos()[drawInfo.node].indirectIndex);
        }
        range.data = ids.data();
        range.size = ids.size() * sizeof(uint32_t);
        range.offset = sizeof(PerFrameData);
        m_storages[frame]->Copy(1, &range, true);

        for (auto &modelPtr : m_models)
        {
            Model &model = *modelPtr;
            if (!model.GetDirtyUniforms()[frame])
                continue;

            for (int node = 0; node < model.GetNodeCount(); node++)
            {
                NodeInfo &nodeInfo = model.GetNodeInfos()[node];
                if (!nodeInfo.dirtyUniforms[frame])
                    continue;

                int mesh = model.GetNodeMesh(node);
                if (mesh < 0)
                    continue;

                MeshInfo &meshInfo = model.GetMeshInfos()[mesh];
                nodeInfo.ubo.materialFactors[0] = meshInfo.materialFactors[0];
                nodeInfo.ubo.materialFactors[1] = meshInfo.materialFactors[1];

                // Override alphaCutoff in the UBO copy if not AlphaCut
                if (meshInfo.renderType != RenderType::AlphaCut)
                    nodeInfo.ubo.materialFactors[0][2][2] = 0.0f;

                range.data = &nodeInfo.ubo;
                range.size = meshInfo.dataSize;
                range.offset = nodeInfo.dataOffset;
                m_storages[frame]->Copy(1, &range, true);

                nodeInfo.dirtyUniforms[frame] = false;
            }

            model.GetDirtyUniforms()[frame] = false;
        }

        m_storages[frame]->Unmap();
    }

    void Scene::UpdateIndirectData()
    {
        uint32_t frame = RHII.GetFrameIndex();

        m_indirects[frame]->Map();

        uint32_t firstInstance = 0;
        for (auto &drawInfo : m_drawInfosOpaque)
        {
            auto &model = *drawInfo.model;
            auto &indirectCommand = m_indirectCommands[model.GetNodeInfos()[drawInfo.node].indirectIndex];
            indirectCommand.firstInstance = firstInstance;

            BufferRange range{};
            range.data = &indirectCommand;
            range.size = sizeof(vk::DrawIndexedIndirectCommand);
            range.offset = firstInstance * sizeof(vk::DrawIndexedIndirectCommand);
            m_indirects[frame]->Copy(1, &range, true);

            firstInstance++;
        }

        for (auto &drawInfo : m_drawInfosAlphaCut)
        {
            auto &model = *drawInfo.model;
            auto &indirectCommand = m_indirectCommands[model.GetNodeInfos()[drawInfo.node].indirectIndex];
            indirectCommand.firstInstance = firstInstance;

            BufferRange range{};
            range.data = &indirectCommand;
            range.size = sizeof(vk::DrawIndexedIndirectCommand);
            range.offset = firstInstance * sizeof(vk::DrawIndexedIndirectCommand);
            m_indirects[frame]->Copy(1, &range, true);

            firstInstance++;
        }

        for (auto &drawInfo : m_drawInfosTransmission)
        {
            auto &model = *drawInfo.model;
            auto &indirectCommand = m_indirectCommands[model.GetNodeInfos()[drawInfo.node].indirectIndex];
            indirectCommand.firstInstance = firstInstance;

            BufferRange range{};
            range.data = &indirectCommand;
            range.size = sizeof(vk::DrawIndexedIndirectCommand);
            range.offset = firstInstance * sizeof(vk::DrawIndexedIndirectCommand);
            m_indirects[frame]->Copy(1, &range, true);

            firstInstance++;
        }

        for (auto &drawInfo : m_drawInfosAlphaBlend)
        {
            auto &model = *drawInfo.model;
            auto &indirectCommand = m_indirectCommands[model.GetNodeInfos()[drawInfo.node].indirectIndex];
            indirectCommand.firstInstance = firstInstance;

            BufferRange range{};
            range.data = &indirectCommand;
            range.size = sizeof(vk::DrawIndexedIndirectCommand);
            range.offset = firstInstance * sizeof(vk::DrawIndexedIndirectCommand);
            m_indirects[frame]->Copy(1, &range, true);

            firstInstance++;
        }

        m_indirects[frame]->Flush();
        m_indirects[frame]->Unmap();
    }

    void Scene::UpdateGeometry()
    {
        ClearDrawInfos(true);

        std::vector<std::shared_future<void>> futures;
        for (auto &modelPtr : m_models)
        {
            Model &model = *modelPtr;
            if (!model.IsRenderReady())
                continue;

            model.UpdateNodeMatrices();

            for (int i = 0; i < model.GetNodeCount(); i++)
            {
                if (model.GetNodeMesh(i) > -1)
                    futures.push_back(ThreadPool::Update.Enqueue(&Scene::CullNode, this, std::ref(model), i));
            }
        }

        for (auto &future : futures)
            future.wait();

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

            for (auto &modelPtr : m_models)
            {
                Model &model = *modelPtr;
                if (!model.IsRenderReady())
                    continue;

                for (int node = 0; node < model.GetNodeCount(); node++)
                {
                    int mesh = model.GetNodeMesh(node);
                    if (mesh < 0)
                        continue;

                    const MeshInfo &meshInfo = model.GetMeshInfos()[mesh];

                    switch (meshInfo.renderType)
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

        if (!GetBuffer()) // No geometry
            return;

        // Barrier for vertex/index buffer uploads
        vk::MemoryBarrier2 barrier{};
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
        barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR | vk::PipelineStageFlagBits2::eTransfer;
        barrier.dstAccessMask = vk::AccessFlagBits2::eAccelerationStructureReadKHR | vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eTransferRead;
        cmd->MemoryBarrier(barrier);

        vk::DeviceAddress bufferAddress = GetBuffer()->GetDeviceAddress();

        // --- Pass 1: Calculate sizes for BLAS ---
        vk::DeviceSize totalBlasSize = 0;
        vk::DeviceSize maxScratchSize = 0;

        struct BlasBuildReq
        {
            vk::AccelerationStructureGeometryKHR geometry;
            vk::AccelerationStructureBuildRangeInfoKHR range;
            vk::AccelerationStructureBuildSizesInfoKHR sizeInfo;
            Model *model;
            size_t meshIndex;
            AccelerationStructure *createdBlas = nullptr;
        };
        std::vector<BlasBuildReq> buildReqs;

        static constexpr vk::BuildAccelerationStructureFlagsKHR kBlasFlags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;

        // Iterate models/meshes
        for (auto model : m_models)
        {
            auto &meshInfos = model->GetMeshInfos();
            for (size_t i = 0; i < meshInfos.size(); i++)
            {
                auto &meshInfo = meshInfos[i];
                if (meshInfo.indicesCount == 0)
                    continue;

                vk::AccelerationStructureGeometryKHR geometry{};
                geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
                geometry.geometry.triangles.vertexFormat = vk::Format::eR32G32B32Sfloat;
                geometry.geometry.triangles.vertexData.deviceAddress = bufferAddress + m_verticesOffset + meshInfo.vertexOffset * sizeof(Vertex);
                geometry.geometry.triangles.vertexStride = sizeof(Vertex);
                geometry.geometry.triangles.maxVertex = meshInfo.verticesCount ? meshInfo.verticesCount - 1 : 0;
                geometry.geometry.triangles.indexType = vk::IndexType::eUint32;
                geometry.geometry.triangles.indexData.deviceAddress = bufferAddress + meshInfo.indexOffset * sizeof(uint32_t);
                if (meshInfo.renderType == RenderType::AlphaCut ||
                    meshInfo.renderType == RenderType::AlphaBlend ||
                    meshInfo.renderType == RenderType::Transmission)
                {
                    geometry.flags = vk::GeometryFlagBitsKHR::eNoDuplicateAnyHitInvocation;
                }
                else
                {
                    geometry.flags = vk::GeometryFlagBitsKHR::eOpaque;
                }

                vk::AccelerationStructureBuildRangeInfoKHR range{};
                range.primitiveCount = meshInfo.indicesCount / 3;
                range.primitiveOffset = 0;
                range.firstVertex = 0;
                range.transformOffset = 0;

                auto sizeInfo = AccelerationStructure::GetBuildSizes(
                    {geometry},
                    {range.primitiveCount},
                    vk::AccelerationStructureTypeKHR::eBottomLevel,
                    kBlasFlags,
                    vk::AccelerationStructureBuildTypeKHR::eDevice);

                // Align up
                totalBlasSize = RHII.Align(totalBlasSize + sizeInfo.accelerationStructureSize, 256);
                maxScratchSize = std::max(maxScratchSize, sizeInfo.buildScratchSize);

                buildReqs.push_back({geometry, range, sizeInfo, model, i});
            }
        }

        struct InstanceReq
        {
            AccelerationStructure *blas;
            Model *model;
            int meshIndex;
            int nodeIndex; // Added for caching
            mat4 transform;
        };
        std::vector<InstanceReq> instanceReqs;
        instanceReqs.reserve(m_meshCount); // Approximate or better

        for (auto model : m_models)
        {
            for (int i = 0; i < model->GetNodeCount(); i++)
            {
                int meshIndex = model->GetNodeMesh(i);
                if (meshIndex < 0)
                    continue;

                // Check if valid mesh (must match BLAS build logic)
                if (model->GetMeshInfos()[meshIndex].indicesCount == 0)
                    continue;

                const auto &nodeInfo = model->GetNodeInfos()[i];
                instanceReqs.push_back({nullptr, model, meshIndex, i, nodeInfo.ubo.worldMatrix});
            }
        }

        PE_ERROR_IF(instanceReqs.size() != m_meshCount, "BuildAccelerationStructures instanceCount mismatch!");

        static constexpr vk::BuildAccelerationStructureFlagsKHR kTlasFlags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;

        // Query TLAS scratch
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
        auto align = asProps.minAccelerationStructureScratchOffsetAlignment;
        m_scratchBuffer = Buffer::Create(
            RHII.Align(maxScratchSize, align),
            vk::BufferUsageFlagBits2::eStorageBuffer | vk::BufferUsageFlagBits2::eShaderDeviceAddress,
            VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
            "AS_Scratch_Buffer");

        // --- Pass 2: Build BLAS ---
        vk::DeviceSize currentOffset = 0;

        for (size_t i = 0; i < buildReqs.size(); i++)
        {
            auto &req = buildReqs[i];

            // Re-align offset for this AS
            currentOffset = RHII.Align(currentOffset, 256);

            std::string name = req.model ? ("BLAS_" + req.model->GetLabel() + "_" + std::to_string(req.meshIndex)) : "BLAS_Dummy";
            req.createdBlas = new AccelerationStructure(name, m_blasMergedBuffer, currentOffset);
            req.createdBlas->BuildBLAS(cmd, {req.geometry}, {req.range}, {req.range.primitiveCount}, kBlasFlags, m_scratchBuffer->GetDeviceAddress());
            m_blases.push_back(req.createdBlas);

            currentOffset += req.sizeInfo.accelerationStructureSize;
        }

        // --- Match Instances to BLAS ---
        for (auto it = instanceReqs.begin(); it != instanceReqs.end();)
        {
            auto &req = *it;
            for (auto &bReq : buildReqs)
            {
                if (bReq.model == req.model && bReq.meshIndex == static_cast<size_t>(req.meshIndex))
                {
                    req.blas = bReq.createdBlas;
                    break;
                }
            }
            if (!req.blas)
            {
                it = instanceReqs.erase(it);
            }
            else
            {
                ++it;
            }
        }

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

            const auto &meshInfo = req.model ? req.model->GetMeshInfos()[req.meshIndex] : MeshInfo{};
            bool isTransparent = req.model && (meshInfo.renderType == RenderType::AlphaBlend ||
                                               meshInfo.renderType == RenderType::Transmission ||
                                               meshInfo.renderType == RenderType::AlphaCut);

            gpuInstances[i].transform = transformMatrix;
            gpuInstances[i].instanceCustomIndex = static_cast<uint32_t>(i);
            gpuInstances[i].mask = isTransparent ? 0x80 : 0x01;
            gpuInstances[i].instanceShaderBindingTableRecordOffset = 0;
            gpuInstances[i].flags = static_cast<VkGeometryInstanceFlagBitsKHR>(vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable);
            gpuInstances[i].accelerationStructureReference = req.blas->GetDeviceAddress();

            // Cache instance index
            if (req.model)
                req.model->GetNodeInfos()[req.nodeIndex].instanceIndex = static_cast<int>(i);
        }
        m_instanceBuffer->Flush();
        m_instanceBuffer->Unmap();

        // --- TLAS Build ---
        m_tlas = AccelerationStructure::Create("TLAS", nullptr, 0);
        m_tlas->BuildTLAS(cmd, m_meshCount, m_instanceBuffer, kTlasFlags, m_scratchBuffer->GetDeviceAddress());

        // --- Create MeshInfoGPU Buffer (Corresponds to Instances) ---
        struct MeshInfoGPU
        {
            uint32_t indexOffset;
            uint32_t vertexOffset;
            uint32_t positionsOffset;
            uint32_t renderType; // 1: Opaque, 2: AlphaCut, 3: AlphaBlend, 4: Transmission
            int32_t textures[5]; // BaseColor, Normal, Metallic, Occlusion, Emissive
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
            if (req.model)
            {
                const auto &meshInfo = req.model->GetMeshInfos()[req.meshIndex];

                meshInfoGPU.indexOffset = meshInfo.indexOffset * 4;
                meshInfoGPU.vertexOffset = static_cast<uint32_t>(m_verticesOffset) + meshInfo.vertexOffset * sizeof(Vertex);
                meshInfoGPU.renderType = static_cast<uint32_t>(meshInfo.renderType);

                for (int k = 0; k < 5; k++)
                    meshInfoGPU.textures[k] = meshInfo.viewsIndex[k];
            }
            else
            {
                meshInfoGPU.indexOffset = 0;
                meshInfoGPU.vertexOffset = 0;
                meshInfoGPU.renderType = 0;
                for (int k = 0; k < 5; k++)
                    meshInfoGPU.textures[k] = -1;
            }
        }
        m_meshInfoBuffer->Flush();
        m_meshInfoBuffer->Unmap();
    }

    void Scene::UpdateTLASTransformations(CommandBuffer *cmd)
    {
        if (!m_tlas || !m_instanceBuffer)
            return;

        // Check if any model has dirty transforms
        bool needsUpdate = false;
        for (auto &modelPtr : m_models)
        {
            if (modelPtr->IsMoved())
            {
                needsUpdate = true;
                break;
            }
        }

        if (!needsUpdate)
            return;

        m_instanceBuffer->Map();
        auto *gpuInstances = (vk::AccelerationStructureInstanceKHR *)m_instanceBuffer->Data();
        int updatedCount = 0;

        for (auto &modelPtr : m_models)
        {
            Model &model = *modelPtr;
            if (!model.IsMoved())
                continue;

            const auto &nodesMoved = model.GetNodesMoved();
            for (int i : nodesMoved)
            {
                const auto &nodeInfo = model.GetNodeInfos()[i];
                int instanceIndex = nodeInfo.instanceIndex;

                if (instanceIndex < 0)
                    continue;

                const mat4 &t = nodeInfo.ubo.worldMatrix;

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
                updatedCount++;
            }
            model.ClearNodesMoved(); // Clear the list for this frame
        }
        m_instanceBuffer->Flush();
        m_instanceBuffer->Unmap();

        // Update TLAS in-place using eUpdate mode
        m_tlas->UpdateTLAS(cmd, m_meshCount, m_instanceBuffer, m_scratchBuffer->GetDeviceAddress());
    }
    void Scene::SaveScene(const std::filesystem::path &file) const
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

        // Models
        rapidjson::Value models(rapidjson::kArrayType);
        for (auto *model : m_models)
        {
            if (!model)
                continue;

            rapidjson::Value modelObj(rapidjson::kObjectType);

            // Basic Info
            std::string pathObj = model->GetFilePath().generic_string();
            modelObj.AddMember("path", rapidjson::Value(pathObj.c_str(), static_cast<rapidjson::SizeType>(pathObj.length()), allocator).Move(), allocator);
            modelObj.AddMember("name", rapidjson::Value(model->GetLabel().c_str(), allocator).Move(), allocator);

            rapidjson::Value matrixVal;
            SetMat4(matrixVal, model->GetMatrix());
            modelObj.AddMember("matrix", matrixVal.Move(), allocator);

            // Nodes (Hierarchy & Transforms)
            rapidjson::Value nodesArr(rapidjson::kArrayType);
            const auto &nodeInfos = model->GetNodeInfos();
            for (const auto &node : nodeInfos)
            {
                rapidjson::Value nodeObj(rapidjson::kObjectType);
                nodeObj.AddMember("name", rapidjson::Value(node.name.c_str(), allocator).Move(), allocator);
                nodeObj.AddMember("parent", node.parent, allocator);

                rapidjson::Value localMat;
                SetMat4(localMat, node.localMatrix);
                nodeObj.AddMember("local_matrix", localMat.Move(), allocator);

                nodesArr.PushBack(nodeObj.Move(), allocator);
            }
            modelObj.AddMember("nodes", nodesArr.Move(), allocator);

            // Meshes (Materials & Textures)
            rapidjson::Value meshesArr(rapidjson::kArrayType);
            const auto &meshInfos = model->GetMeshInfos();
            for (const auto &mesh : meshInfos)
            {
                rapidjson::Value meshObj(rapidjson::kObjectType);
                meshObj.AddMember("render_type", static_cast<int>(mesh.renderType), allocator);
                meshObj.AddMember("texture_mask", mesh.textureMask, allocator);

                // Material Factors
                rapidjson::Value factorsArr(rapidjson::kArrayType);
                rapidjson::Value f0, f1;
                SetMat4(f0, mesh.materialFactors[0]);
                SetMat4(f1, mesh.materialFactors[1]);
                factorsArr.PushBack(f0.Move(), allocator);
                factorsArr.PushBack(f1.Move(), allocator);
                meshObj.AddMember("material_factors", factorsArr.Move(), allocator);

                // Textures (Save paths if not default)
                rapidjson::Value texturesObj(rapidjson::kObjectType);
                const char *methodNames[] = {"base_color", "metallic_roughness", "normal", "occlusion", "emissive"};
                for (int i = 0; i < 5; i++)
                {
                    if (mesh.images[i] && !mesh.images[i]->GetName().empty())
                    {
                        std::string texName = mesh.images[i]->GetName();
                        if (!texName.empty())
                        {
                            texturesObj.AddMember(rapidjson::Value(methodNames[i], allocator).Move(),
                                                  rapidjson::Value(texName.c_str(), allocator).Move(), allocator);
                        }
                    }
                }
                meshObj.AddMember("textures", texturesObj.Move(), allocator);
                meshesArr.PushBack(meshObj.Move(), allocator);
            }
            modelObj.AddMember("meshes", meshesArr.Move(), allocator);
            models.PushBack(modelObj.Move(), allocator);
        }
        d.AddMember("models", models.Move(), allocator);

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
                Log::Error("File stream error while writing to: " + file.string());
            else
                Log::Info("Scene saved to: " + file.string());
        }
        else
        {
            Log::Error("Failed to write JSON content (invalid data/encoding?) to: " + file.string());
        }
    }

    void Scene::LoadScene(const std::filesystem::path &file)
    {
        std::ifstream ifs(file);
        if (!ifs.is_open())
        {
            Log::Error("Failed to open scene file: " + file.string());
            return;
        }

        rapidjson::IStreamWrapper isw(ifs);
        rapidjson::Document d;
        d.ParseStream(isw);

        if (d.HasParseError())
        {
            Log::Error("Failed to parse scene file: " + file.string());
            return;
        }

        // Clear existing scene
        std::vector<Model *> modelsToRemove;
        for (auto *model : m_models)
            modelsToRemove.push_back(model);

        for (auto *model : modelsToRemove)
            RemoveModel(model);

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

        if (d.HasMember("models"))
        {
            Queue *queue = RHII.GetMainQueue();
            CommandBuffer *cmd = queue->AcquireCommandBuffer();
            cmd->Begin();

            const auto &models = d["models"];
            for (const auto &modelVal : models.GetArray())
            {
                std::string path = modelVal["path"].GetString();
                if (std::filesystem::is_directory(path))
                    continue;

                Model *model = Model::Load(path);
                if (model)
                {
                    if (modelVal.HasMember("name"))
                        model->SetLabel(modelVal["name"].GetString());
                    if (modelVal.HasMember("matrix"))
                        model->GetMatrix() = ReadMat4(modelVal["matrix"]);

                    // Nodes
                    if (modelVal.HasMember("nodes"))
                    {
                        const auto &nodesVal = modelVal["nodes"];
                        auto &nodeInfos = model->GetNodeInfos();
                        for (rapidjson::SizeType i = 0; i < nodesVal.Size() && i < nodeInfos.size(); i++)
                        {
                            const auto &nVal = nodesVal[i];
                            auto &ni = nodeInfos[i];
                            if (nVal.HasMember("name"))
                                ni.name = nVal["name"].GetString();
                            if (nVal.HasMember("parent"))
                                ni.parent = nVal["parent"].GetInt();
                            if (nVal.HasMember("local_matrix"))
                                ni.localMatrix = ReadMat4(nVal["local_matrix"]);
                        }

                        // Rebuild children
                        for (auto &ni : nodeInfos)
                            ni.children.clear();
                        for (int i = 0; i < static_cast<int>(nodeInfos.size()); i++)
                        {
                            if (nodeInfos[i].parent >= 0 && nodeInfos[i].parent < static_cast<int>(nodeInfos.size()))
                                nodeInfos[nodeInfos[i].parent].children.push_back(i);
                        }

                        model->GetDirtyNodes() = true;
                        model->UpdateNodeMatrices();
                    }

                    // Meshes
                    if (modelVal.HasMember("meshes"))
                    {
                        const auto &meshesVal = modelVal["meshes"];
                        auto &meshInfos = model->GetMeshInfos();
                        for (rapidjson::SizeType i = 0; i < meshesVal.Size() && i < meshInfos.size(); i++)
                        {
                            const auto &mVal = meshesVal[i];
                            auto &mi = meshInfos[i];
                            if (mVal.HasMember("render_type"))
                                mi.renderType = static_cast<RenderType>(mVal["render_type"].GetInt());
                            if (mVal.HasMember("material_factors"))
                            {
                                mi.materialFactors[0] = ReadMat4(mVal["material_factors"][0]);
                                mi.materialFactors[1] = ReadMat4(mVal["material_factors"][1]);
                            }

                            bool hasTextureMask = mVal.HasMember("texture_mask");
                            if (hasTextureMask)
                                mi.textureMask = mVal["texture_mask"].GetUint();

                            if (mVal.HasMember("textures"))
                            {
                                const auto &texVal = mVal["textures"];
                                const char *methodNames[] = {"base_color", "metallic_roughness", "normal", "occlusion", "emissive"};
                                for (int k = 0; k < 5; k++)
                                {
                                    if (texVal.HasMember(methodNames[k]))
                                    {
                                        std::string texPath = texVal[methodNames[k]].GetString();
                                        if (std::filesystem::exists(texPath))
                                        {
                                            Image *img = model->LoadTexture(cmd, texPath);
                                            if (img)
                                            {
                                                mi.images[k] = img;
                                                // Only auto-enable mask if no mask was present in JSON
                                                if (!hasTextureMask)
                                                    mi.textureMask |= (1 << k);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    AddModel(model);
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

        Log::Info("Scene loaded from: " + file.string());
    }
} // namespace pe
