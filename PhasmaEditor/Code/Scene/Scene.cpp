#include "Scene/Scene.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/Vertex.h"
#include "Camera/Camera.h"
#include "RenderPasses/AabbsPass.h"
#include "RenderPasses/DepthPass.h"
#include "RenderPasses/GbufferPass.h"
#include "RenderPasses/LightPass.h"
#include "RenderPasses/ShadowPass.h"
#include "Scene/Model.h"

namespace pe
{
    namespace
    {
        Sampler *defaultSampler = nullptr;
    }

    std::vector<uint32_t> Scene::s_aabbIndices = {
        0, 1, 1, 2, 2, 3, 3, 0,
        4, 5, 5, 6, 6, 7, 7, 4,
        0, 4, 1, 5, 2, 6, 3, 7};

    Scene::Scene()
    {
        if (!defaultSampler)
            defaultSampler = Sampler::Create(Sampler::CreateInfoInit(), "defaultSampler");

        Camera *camera = new Camera();
        m_cameras.push_back(camera);

        uint32_t swapchainImageCount = RHII.GetSwapchainImageCount();
        m_storages.resize(swapchainImageCount, nullptr);
        m_indirects.resize(swapchainImageCount, nullptr);
        m_dirtyDescriptorViews.resize(swapchainImageCount, false);
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

        if (defaultSampler)
            Sampler::Destroy(defaultSampler);
    }

    void Scene::Update()
    {
        for (auto *camera : m_cameras)
            camera->Update();

        UpdateGeometry();

        if (HasDrawInfo())
        {
            uint32_t frame = RHII.GetFrameIndex();

            if (Settings::Get<GlobalSettings>().draw_aabbs)
            {
                AabbsPass *ap = GetGlobalComponent<AabbsPass>();
                const auto &sets = ap->m_passInfo->GetDescriptors(frame);
                Descriptor *setUniforms = sets[0];
                setUniforms->SetBuffer(0, GetUniforms(frame));
                setUniforms->Update();
            }

            if (HasOpaqueDrawInfo())
            {
                {
                    GbufferOpaquePass *gb = GetGlobalComponent<GbufferOpaquePass>();
                    DepthPass *dp = GetGlobalComponent<DepthPass>();
                    const auto &sets = dp->m_passInfo->GetDescriptors(frame);

                    Descriptor *setUniforms = sets[0];
                    setUniforms->SetBuffer(0, GetUniforms(frame));
                    setUniforms->SetBuffer(1, gb->m_constants);
                    setUniforms->Update();

                    Descriptor *setTextures = sets[1];
                    setTextures->SetBuffer(0, gb->m_constants);
                    if (HasDirtyDescriptorViews(frame))
                    {
                        setTextures->SetSampler(1, defaultSampler->ApiHandle());
                        setTextures->SetImageViews(2, GetImageViews(), {});
                        setTextures->Update();
                    }
                }

                {
                    GbufferOpaquePass *gb = GetGlobalComponent<GbufferOpaquePass>();
                    ShadowPass *shadows = GetGlobalComponent<ShadowPass>();
                    const auto &sets = shadows->m_passInfo->GetDescriptors(frame);

                    Descriptor *setUniforms = sets[0];
                    setUniforms->SetBuffer(0, GetUniforms(frame));
                    setUniforms->SetBuffer(1, gb->m_constants);
                    setUniforms->Update();
                }

                {
                    GbufferOpaquePass *gb = GetGlobalComponent<GbufferOpaquePass>();
                    const auto &sets = gb->m_passInfo->GetDescriptors(frame);

                    Descriptor *setUniforms = sets[0];
                    setUniforms->SetBuffer(0, GetUniforms(frame));
                    setUniforms->SetBuffer(1, gb->m_constants);
                    setUniforms->Update();

                    Descriptor *setTextures = sets[1];
                    setTextures->SetBuffer(0, gb->m_constants);
                    if (HasDirtyDescriptorViews(frame))
                    {
                        setTextures->SetSampler(1, defaultSampler->ApiHandle());
                        setTextures->SetImageViews(2, GetImageViews(), {});
                        setTextures->Update();
                    }
                }
            }

            if (HasAlphaDrawInfo())
            {
                GbufferTransparentPass *gb = GetGlobalComponent<GbufferTransparentPass>();
                const auto &sets = gb->m_passInfo->GetDescriptors(frame);

                Descriptor *setUniforms = sets[0];
                setUniforms->SetBuffer(0, GetUniforms(frame));
                setUniforms->SetBuffer(1, gb->m_constants);
                setUniforms->Update();

                Descriptor *setTextures = sets[1];
                setTextures->SetBuffer(0, gb->m_constants);
                if (HasDirtyDescriptorViews(frame))
                {
                    setTextures->SetSampler(1, defaultSampler->ApiHandle());
                    setTextures->SetImageViews(2, GetImageViews(), {});
                    setTextures->Update();
                }
            }

            ClearDirtyDescriptorViews(frame);
        }
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
        m_models.erase(model->GetId());
    }

    void Scene::UploadBuffers(CommandBuffer *cmd)
    {
        if (!m_models.size())
            return;

        DestroyBuffers();
        CreateGeometryBuffer();
        CopyIndices(cmd);
        CopyVertices(cmd);
        CreateStorageBuffers();
        MarkUniformsDirty();
        CreateIndirectBuffers(cmd);
        UpdateImageViews();
        CreateGBufferConstants(cmd);
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
            vk::BufferUsageFlagBits2::eTransferDst | vk::BufferUsageFlagBits2::eIndexBuffer | vk::BufferUsageFlagBits2::eVertexBuffer,
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

        vk::BufferMemoryBarrier2 indexBarrierInfo{};
        indexBarrierInfo.buffer = m_buffer->ApiHandle();
        indexBarrierInfo.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        indexBarrierInfo.dstAccessMask = vk::AccessFlagBits2::eIndexRead;
        indexBarrierInfo.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
        indexBarrierInfo.dstStageMask = vk::PipelineStageFlagBits2::eVertexInput;
        indexBarrierInfo.size = indicesCount * sizeof(uint32_t);
        indexBarrierInfo.offset = 0;
        cmd->BufferBarrier(indexBarrierInfo);

        m_aabbIndicesOffset = indicesCount * sizeof(uint32_t);
        cmd->CopyBufferStaged(m_buffer, s_aabbIndices.data(), s_aabbIndices.size() * sizeof(uint32_t), m_aabbIndicesOffset);

        vk::BufferMemoryBarrier2 aabbIndexBarrierInfo{};
        aabbIndexBarrierInfo.buffer = m_buffer->ApiHandle();
        aabbIndexBarrierInfo.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        aabbIndexBarrierInfo.dstAccessMask = vk::AccessFlagBits2::eIndexRead;
        aabbIndexBarrierInfo.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
        aabbIndexBarrierInfo.dstStageMask = vk::PipelineStageFlagBits2::eVertexInput;
        aabbIndexBarrierInfo.size = s_aabbIndices.size() * sizeof(uint32_t);
        aabbIndexBarrierInfo.offset = m_aabbIndicesOffset;
        cmd->BufferBarrier(aabbIndexBarrierInfo);
    }

    void Scene::CopyVertices(CommandBuffer *cmd)
    {
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
        }

        vk::BufferMemoryBarrier2 vertexBarrierInfo{};
        vertexBarrierInfo.buffer = m_buffer->ApiHandle();
        vertexBarrierInfo.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        vertexBarrierInfo.dstAccessMask = vk::AccessFlagBits2::eVertexAttributeRead;
        vertexBarrierInfo.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
        vertexBarrierInfo.dstStageMask = vk::PipelineStageFlagBits2::eVertexInput;
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
        }

        vk::BufferMemoryBarrier2 posVertexBarrierInfo{};
        posVertexBarrierInfo.buffer = m_buffer->ApiHandle();
        posVertexBarrierInfo.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        posVertexBarrierInfo.dstAccessMask = vk::AccessFlagBits2::eVertexAttributeRead;
        posVertexBarrierInfo.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
        posVertexBarrierInfo.dstStageMask = vk::PipelineStageFlagBits2::eVertexInput;
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
        }

        vk::BufferMemoryBarrier2 aabbVertexBarrierInfo{};
        aabbVertexBarrierInfo.buffer = m_buffer->ApiHandle();
        aabbVertexBarrierInfo.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        aabbVertexBarrierInfo.dstAccessMask = vk::AccessFlagBits2::eVertexAttributeRead;
        aabbVertexBarrierInfo.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
        aabbVertexBarrierInfo.dstStageMask = vk::PipelineStageFlagBits2::eVertexInput;
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
                meshInfo.dataOffset = storageSize;
                storageSize += meshInfo.dataSize;
                storageSize += sizeof(mat4);
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
                meshInfo.indirectIndex = indirectCount;

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

        vk::BufferMemoryBarrier2 indirectBarrierInfo{};
        indirectBarrierInfo.buffer = m_indirectAll->ApiHandle();
        indirectBarrierInfo.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        indirectBarrierInfo.dstAccessMask = vk::AccessFlagBits2::eIndirectCommandRead;
        indirectBarrierInfo.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
        indirectBarrierInfo.dstStageMask = vk::PipelineStageFlagBits2::eDrawIndirect;
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

            for (auto &meshInfo : model.GetMeshInfos())
            {
                for (int k = 0; k < 5; k++)
                    meshInfo.viewsIndex[k] = imagesMap[meshInfo.images[k]];
            }
        }

        for (auto &&dirtyView : m_dirtyDescriptorViews)
            dirtyView = true;
    }

    void Scene::CreateGBufferConstants(CommandBuffer *cmd)
    {
        GbufferOpaquePass *gbo = GetGlobalComponent<GbufferOpaquePass>();
        GbufferTransparentPass *gbt = GetGlobalComponent<GbufferTransparentPass>();
        gbo->m_constants = Buffer::Create(
            m_meshCount * sizeof(Mesh_Constants),
            vk::BufferUsageFlagBits2::eStorageBuffer | vk::BufferUsageFlagBits2::eTransferDst,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            "GbufferPass_constants");
        gbt->m_constants = gbo->m_constants;

        size_t offset = 0;
        gbo->m_constants->Map();
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
                constants.alphaCut = meshInfo.alphaCutoff;
                constants.meshDataOffset = static_cast<uint32_t>(meshInfo.dataOffset);
                constants.textureMask = meshInfo.textureMask;
                for (int k = 0; k < 5; k++)
                    constants.meshImageIndex[k] = meshInfo.viewsIndex[k];

                BufferRange range{};
                range.data = &constants;
                range.offset = offset;
                range.size = sizeof(Mesh_Constants);
                gbo->m_constants->Copy(1, &range, true);

                offset += sizeof(Mesh_Constants);
            }
        }
        gbo->m_constants->Flush();
        gbo->m_constants->Unmap();
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
        localOpaque.reserve(1);
        localAlphaCut.reserve(1);
        localAlphaBlend.reserve(1);

        meshInfo.cull = frustumCulling ? !camera.AABBInFrustum(meshInfo.worldBoundingBox) : false;
        if (!meshInfo.cull)
        {
            vec3 center = meshInfo.worldBoundingBox.GetCenter();
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
            }
        }

        if (!localOpaque.empty() || !localAlphaCut.empty() || !localAlphaBlend.empty())
        {
            std::scoped_lock lock(m_drawInfosMutex);
            m_drawInfosOpaque.insert(m_drawInfosOpaque.end(), localOpaque.begin(), localOpaque.end());
            m_drawInfosAlphaCut.insert(m_drawInfosAlphaCut.end(), localAlphaCut.begin(), localAlphaCut.end());
            m_drawInfosAlphaBlend.insert(m_drawInfosAlphaBlend.end(), localAlphaBlend.begin(), localAlphaBlend.end());
        }
    }

    void Scene::UpdateUniformData()
    {
        uint32_t frame = RHII.GetFrameIndex();

        m_storages[frame]->Map();

        m_frameData.viewProjection = m_cameras[0]->GetViewProjection();
        m_frameData.previousViewProjection = m_cameras[0]->GetPreviousViewProjection();

        BufferRange range{};
        range.data = &m_frameData;
        range.size = sizeof(PerFrameData);
        range.offset = 0;
        m_storages[frame]->Copy(1, &range, true);

        std::vector<uint32_t> ids{};
        ids.reserve(m_drawInfosOpaque.size() + m_drawInfosAlphaCut.size() + m_drawInfosAlphaBlend.size());
        for (auto &drawInfo : m_drawInfosOpaque)
        {
            auto &model = *drawInfo.model;
            auto &meshInfo = model.GetMeshInfos()[model.GetNodeMesh(drawInfo.node)];
            ids.push_back(meshInfo.indirectIndex);
        }
        for (auto &drawInfo : m_drawInfosAlphaCut)
        {
            auto &model = *drawInfo.model;
            auto &meshInfo = model.GetMeshInfos()[model.GetNodeMesh(drawInfo.node)];
            ids.push_back(meshInfo.indirectIndex);
        }
        for (auto &drawInfo : m_drawInfosAlphaBlend)
        {
            auto &model = *drawInfo.model;
            auto &meshInfo = model.GetMeshInfos()[model.GetNodeMesh(drawInfo.node)];
            ids.push_back(meshInfo.indirectIndex);
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

                range.data = &nodeInfo.ubo;
                range.size = meshInfo.dataSize;
                range.offset = meshInfo.dataOffset;
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
            auto &meshInfo = model.GetMeshInfos()[model.GetNodeMesh(drawInfo.node)];
            auto &indirectCommand = m_indirectCommands[meshInfo.indirectIndex];
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
            auto &meshInfo = model.GetMeshInfos()[model.GetNodeMesh(drawInfo.node)];
            auto &indirectCommand = m_indirectCommands[meshInfo.indirectIndex];
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
            auto &meshInfo = model.GetMeshInfos()[model.GetNodeMesh(drawInfo.node)];
            auto &indirectCommand = m_indirectCommands[meshInfo.indirectIndex];
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
        if (HasDrawInfo())
        {
            UpdateUniformData();
            UpdateIndirectData();
        }
    }

    void Scene::SortDrawInfos()
    {
        std::sort(m_drawInfosOpaque.begin(), m_drawInfosOpaque.end(), [](const DrawInfo &a, const DrawInfo &b)
                  { return a.distance < b.distance; });
        std::sort(m_drawInfosAlphaCut.begin(), m_drawInfosAlphaCut.end(), [](const DrawInfo &a, const DrawInfo &b)
                  { return a.distance > b.distance; });
        std::sort(m_drawInfosAlphaBlend.begin(), m_drawInfosAlphaBlend.end(), [](const DrawInfo &a, const DrawInfo &b)
                  { return a.distance > b.distance; });
    }

    void Scene::ClearDrawInfos(bool reserveMax)
    {
        m_drawInfosOpaque.clear();
        m_drawInfosAlphaCut.clear();
        m_drawInfosAlphaBlend.clear();

        if (reserveMax)
        {
            uint32_t maxOpaque = 0;
            uint32_t maxAlphaCut = 0;
            uint32_t maxAlphaBlend = 0;

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
                    }
                }
            }

            m_drawInfosOpaque.reserve(maxOpaque);
            m_drawInfosAlphaCut.reserve(maxAlphaCut);
            m_drawInfosAlphaBlend.reserve(maxAlphaBlend);
        }
    }

    void Scene::DestroyBuffers()
    {
        Buffer::Destroy(m_buffer);
        m_buffer = nullptr;

        for (auto &storage : m_storages)
        {
            Buffer::Destroy(storage);
            storage = nullptr;
        }

        for (auto &indirect : m_indirects)
        {
            Buffer::Destroy(indirect);
            indirect = nullptr;
        }

        Buffer::Destroy(m_indirectAll);
        m_indirectAll = nullptr;
    }
} // namespace pe
