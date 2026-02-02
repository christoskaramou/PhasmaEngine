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
#include "Systems/RendererSystem.h"

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
            Sampler::Destroy(m_defaultSampler);

        for (auto *blas : m_blases)
            AccelerationStructure::Destroy(blas);
        m_blases.clear();

        AccelerationStructure::Destroy(m_tlas);
        Buffer::Destroy(m_instanceBuffer);
        Buffer::Destroy(m_blasMergedBuffer);
        Buffer::Destroy(m_scratchBuffer);
        Buffer::Destroy(m_meshInfoBuffer);
        Buffer::Destroy(m_meshConstants);
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

    void Scene::AddModel(Model *model)
    {
        m_models.insert(model->GetId(), model);
    }

    void Scene::RemoveModel(Model *model)
    {
        if (SelectionManager::Instance().GetSelectedModel() == model)
            SelectionManager::Instance().ClearSelection();

        m_models.erase(model->GetId());
    }

    void Scene::UploadBuffers(CommandBuffer *cmd)
    {
        DestroyBuffers();
        CreateGeometryBuffer();
        CopyIndices(cmd);
        CopyVertices(cmd);
        CreateStorageBuffers();
        MarkUniformsDirty();
        CreateIndirectBuffers(cmd);
        UpdateImageViews();
        CreateGBufferConstants(cmd);
        if (Settings::Get<GlobalSettings>().ray_tracing_support)
            BuildAccelerationStructures(cmd);
    }

    void Scene::CreateGeometryBuffer()
    {
        size_t numberOfIndices = 24;
        size_t numberOfVertices = 0;
        size_t numberOfAabbVertices = 0;

        for (auto &model : m_models)
        {
            numberOfIndices += model->m_indices.size();
            numberOfVertices += model->m_vertices.size();
            numberOfAabbVertices += model->m_aabbVertices.size();
        }

        m_buffer = Buffer::Create(
            sizeof(uint32_t) * numberOfIndices +
                sizeof(Vertex) * numberOfVertices +
                sizeof(PositionUvVertex) * numberOfVertices +
                sizeof(AabbVertex) * numberOfAabbVertices,
            vk::BufferUsageFlagBits2::eTransferDst | vk::BufferUsageFlagBits2::eIndexBuffer | vk::BufferUsageFlagBits2::eVertexBuffer | vk::BufferUsageFlagBits2::eStorageBuffer | vk::BufferUsageFlagBits2::eShaderDeviceAddress | vk::BufferUsageFlagBits2::eAccelerationStructureBuildInputReadOnlyKHR,
            VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
            "combined_Geometry_buffer");
    }

    void Scene::CopyIndices(CommandBuffer *cmd)
    {
        m_meshCount = 0;
        size_t indicesCount = 0;

        for (auto &modelPtr : m_models)
        {
            Model &model = *modelPtr;
            const auto &indices = model.GetIndices();
            cmd->CopyBufferStaged(m_buffer, const_cast<uint32_t *>(indices.data()), sizeof(uint32_t) * indices.size(), indicesCount * sizeof(uint32_t));

            auto &meshesInfo = model.GetMeshInfos();
            for (auto &meshInfo : meshesInfo)
                meshInfo.indexOffset += static_cast<uint32_t>(indicesCount);

            const int nodeCount = model.GetNodeCount();
            for (int nodeIndex = 0; nodeIndex < nodeCount; nodeIndex++)
            {
                const int meshIndex = model.GetNodeMesh(nodeIndex);
                if (meshIndex < 0 || meshIndex >= static_cast<int>(meshesInfo.size()))
                    continue;

                m_meshCount++;
            }

            indicesCount += indices.size();
        }

        BufferBarrierInfo indexBarrierInfo{};
        indexBarrierInfo.buffer = m_buffer;
        indexBarrierInfo.stageMask = vk::PipelineStageFlagBits2::eVertexInput;
        indexBarrierInfo.accessMask = vk::AccessFlagBits2::eIndexRead;
        indexBarrierInfo.size = indicesCount * sizeof(uint32_t);
        indexBarrierInfo.offset = 0;
        cmd->BufferBarrier(indexBarrierInfo);

        m_aabbIndicesOffset = indicesCount * sizeof(uint32_t);
        cmd->CopyBufferStaged(m_buffer, s_aabbIndices.data(), s_aabbIndices.size() * sizeof(uint32_t), m_aabbIndicesOffset);

        BufferBarrierInfo aabbIndexBarrierInfo{};
        aabbIndexBarrierInfo.buffer = m_buffer;
        aabbIndexBarrierInfo.stageMask = vk::PipelineStageFlagBits2::eVertexInput;
        aabbIndexBarrierInfo.accessMask = vk::AccessFlagBits2::eIndexRead;
        aabbIndexBarrierInfo.size = s_aabbIndices.size() * sizeof(uint32_t);
        aabbIndexBarrierInfo.offset = m_aabbIndicesOffset;
        cmd->BufferBarrier(aabbIndexBarrierInfo);
    }

    void Scene::CopyVertices(CommandBuffer *cmd)
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        auto &progress = gSettings.loading_current;
        auto &total = gSettings.loading_total;
        auto &loading = gSettings.loading_name;

        // Calculate total for progress bar (vertices + positions + aabbs)
        size_t totalItems = 0;
        for (auto &modelPtr : m_models)
        {
            totalItems += modelPtr->GetVertices().size();
            totalItems += modelPtr->GetPositionUvs().size();
            totalItems += modelPtr->GetAabbVertices().size();
        }
        total = static_cast<uint32_t>(totalItems);
        progress = 0;
        loading = "Uploading to GPU";

        m_verticesOffset = m_aabbIndicesOffset + 24 * sizeof(uint32_t);
        size_t verticesCount = 0;
        for (auto &modelPtr : m_models)
        {
            Model &model = *modelPtr;
            const auto &vertices = model.GetVertices();
            const size_t vertexCount = vertices.size();
            cmd->CopyBufferStaged(m_buffer, const_cast<Vertex *>(vertices.data()), sizeof(Vertex) * vertexCount, m_verticesOffset + verticesCount * sizeof(Vertex));
            for (auto &meshInfo : model.GetMeshInfos())
                meshInfo.vertexOffset += static_cast<uint32_t>(verticesCount);
            verticesCount += vertexCount;

            progress += static_cast<uint32_t>(vertexCount);
        }

        BufferBarrierInfo vertexBarrierInfo{};
        vertexBarrierInfo.buffer = m_buffer;
        vertexBarrierInfo.stageMask = vk::PipelineStageFlagBits2::eVertexInput;
        vertexBarrierInfo.accessMask = vk::AccessFlagBits2::eVertexAttributeRead;
        vertexBarrierInfo.size = verticesCount * sizeof(Vertex);
        vertexBarrierInfo.offset = m_verticesOffset;
        cmd->BufferBarrier(vertexBarrierInfo);

        m_positionsOffset = m_verticesOffset + verticesCount * sizeof(Vertex);
        size_t positionsCount = 0;
        for (auto &modelPtr : m_models)
        {
            Model &model = *modelPtr;
            const auto &positionUvs = model.GetPositionUvs();
            const size_t positionCount = positionUvs.size();
            cmd->CopyBufferStaged(m_buffer, const_cast<PositionUvVertex *>(positionUvs.data()), positionCount * sizeof(PositionUvVertex), m_positionsOffset + positionsCount * sizeof(PositionUvVertex));
            for (auto &meshInfo : model.GetMeshInfos())
                meshInfo.positionsOffset += static_cast<uint32_t>(positionsCount);
            positionsCount += positionCount;
            progress += static_cast<uint32_t>(positionCount);
        }

        BufferBarrierInfo posVertexBarrierInfo{};
        posVertexBarrierInfo.buffer = m_buffer;
        posVertexBarrierInfo.stageMask = vk::PipelineStageFlagBits2::eVertexInput;
        posVertexBarrierInfo.accessMask = vk::AccessFlagBits2::eVertexAttributeRead;
        posVertexBarrierInfo.size = positionsCount * sizeof(PositionUvVertex);
        posVertexBarrierInfo.offset = m_positionsOffset;
        cmd->BufferBarrier(posVertexBarrierInfo);

        m_aabbVerticesOffset = m_positionsOffset + positionsCount * sizeof(PositionUvVertex);
        size_t aabbCount = 0;
        for (auto &modelPtr : m_models)
        {
            Model &model = *modelPtr;
            const auto &aabbVertices = model.GetAabbVertices();
            const size_t aabbVertexCount = aabbVertices.size();
            cmd->CopyBufferStaged(m_buffer, const_cast<AabbVertex *>(aabbVertices.data()), aabbVertexCount * sizeof(AabbVertex), m_aabbVerticesOffset + aabbCount * sizeof(AabbVertex));
            for (auto &meshInfo : model.GetMeshInfos())
                meshInfo.aabbVertexOffset += static_cast<uint32_t>(aabbCount);
            aabbCount += aabbVertexCount;
            progress += static_cast<uint32_t>(aabbVertexCount);
        }

        BufferBarrierInfo aabbVertexBarrierInfo{};
        aabbVertexBarrierInfo.buffer = m_buffer;
        aabbVertexBarrierInfo.stageMask = vk::PipelineStageFlagBits2::eVertexInput;
        aabbVertexBarrierInfo.accessMask = vk::AccessFlagBits2::eVertexAttributeRead;
        aabbVertexBarrierInfo.size = aabbCount * sizeof(AabbVertex);
        aabbVertexBarrierInfo.offset = m_aabbVerticesOffset;
        cmd->BufferBarrier(aabbVertexBarrierInfo);
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

        BufferBarrierInfo indirectBarrierInfo{};
        indirectBarrierInfo.buffer = m_indirectAll;
        indirectBarrierInfo.stageMask = vk::PipelineStageFlagBits2::eDrawIndirect;
        indirectBarrierInfo.accessMask = vk::AccessFlagBits2::eIndirectCommandRead;
        indirectBarrierInfo.size = indirectCount * sizeof(vk::DrawIndexedIndirectCommand);
        indirectBarrierInfo.offset = 0;
        cmd->BufferBarrier(indirectBarrierInfo);

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
            for (auto &storage : m_storages)
                model.SetMeshFactors(storage);

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

    void Scene::CreateGBufferConstants(CommandBuffer *cmd)
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
                constants.alphaCut = (meshInfo.renderType == RenderType::AlphaCut) ? meshInfo.alphaCutoff : 0.0f;
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
        Buffer::Destroy(m_buffer);

        for (auto &storage : m_storages)
            Buffer::Destroy(storage);

        for (auto &indirect : m_indirects)
            Buffer::Destroy(indirect);

        Buffer::Destroy(m_indirectAll);
    }

    void Scene::BuildAccelerationStructures(CommandBuffer *cmd)
    {
        // Cleanup old resources
        for (auto *blas : m_blases)
            AccelerationStructure::Destroy(blas);
        m_blases.clear();
        AccelerationStructure::Destroy(m_tlas);
        Buffer::Destroy(m_instanceBuffer);
        Buffer::Destroy(m_blasMergedBuffer);
        Buffer::Destroy(m_scratchBuffer);

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
        instanceReqs.reserve(buildReqs.size()); // Approximate or better

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

        uint32_t instanceCount = static_cast<uint32_t>(instanceReqs.size());

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
            {instanceCount},
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

        m_instanceCount = static_cast<uint32_t>(instanceReqs.size());

        // --- Create Instance Buffer ---
        m_instanceBuffer = Buffer::Create(
            std::max((size_t)1, instanceReqs.size()) * sizeof(vk::AccelerationStructureInstanceKHR),
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
        m_tlas->BuildTLAS(cmd, m_instanceCount, m_instanceBuffer, kTlasFlags, m_scratchBuffer->GetDeviceAddress());

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
            std::max((size_t)1, instanceReqs.size()) * sizeof(MeshInfoGPU),
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
                for (int k = 0; k < 5; k++) meshInfoGPU.textures[k] = -1;
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
        m_tlas->UpdateTLAS(cmd, m_instanceCount, m_instanceBuffer, m_scratchBuffer->GetDeviceAddress());
    }
} // namespace pe
