#include "Scene/Geometry.h"
#include "Scene/ModelGltf.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Vertex.h"
#include "API/Image.h"
#include "Camera/Camera.h"
#include "API/RHI.h"
#include "RenderPasses/GbufferPass.h"

namespace pe
{
    Geometry::Geometry()
    {
        Camera *camera = GetGlobalSystem<CameraSystem>()->GetCamera(0);
        m_cameras.push_back(camera);

        // AABBs Indices
        m_aabbIndices = {
            0, 1, 1, 2, 2, 3, 3, 0, // Near face edges
            4, 5, 5, 6, 6, 7, 7, 4, // Far face edges
            0, 4, 1, 5, 2, 6, 3, 7  // Connecting edges between near and far faces
        };

        uint32_t swapchainImageCount = RHII.GetSwapchainImageCount();
        m_storages.resize(swapchainImageCount, nullptr);
        m_indirects.resize(swapchainImageCount, nullptr);
        m_dirtyDescriptorViews.resize(swapchainImageCount, false);
    }

    Geometry::~Geometry()
    {
        for (auto *model : m_models)
            delete model;
        m_models.clear();
        DestroyBuffers();
    }

    void Geometry::AddModel(ModelGltf *model)
    {
        m_models.insert(model->m_id, model);
    }

    void Geometry::RemoveModel(ModelGltf *model)
    {
        m_models.erase(model->m_id);
    }

    void Geometry::UploadBuffers(CommandBuffer *cmd)
    {
        DestroyBuffers();
        CreateGeometryBuffer();
        CopyIndices(cmd);
        CopyVertices(cmd);
        CreateStorageBuffers();
        CreateIndirectBuffers(cmd);
        UpdateImageViews();
        CopyGBufferConstants(cmd);
    }

    void Geometry::CreateGeometryBuffer()
    {
        size_t numberOfIndices = 24; // with aabb indices
        size_t numberOfVertices = 0;
        size_t numberOfAabbVertices = 0;

        // calculate offsets
        for (auto &model : m_models)
        {
            numberOfIndices += model->m_indices.size();
            numberOfVertices += model->m_vertices.size();
            numberOfAabbVertices += model->m_aabbVertices.size();
        }

        // create geometry buffer
        m_buffer = Buffer::Create(
            sizeof(uint32_t) * numberOfIndices + sizeof(Vertex) * numberOfVertices + sizeof(PositionUvVertex) * numberOfVertices + sizeof(AabbVertex) * numberOfAabbVertices,
            vk::BufferUsageFlagBits2::eTransferDst | vk::BufferUsageFlagBits2::eIndexBuffer | vk::BufferUsageFlagBits2::eVertexBuffer,
            VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
            "combined_Geometry_buffer");
    }

    void Geometry::CopyIndices(CommandBuffer *cmd)
    {
        // indices
        m_primitivesCount = 0;
        size_t indicesCount = 0;

        for (auto &modelPtr : m_models)
        {
            ModelGltf &model = *modelPtr;
            cmd->CopyBufferStaged(m_buffer, model.m_indices.data(), sizeof(uint32_t) * model.m_indices.size(), indicesCount * sizeof(uint32_t));

            // add the geometryBuffer index offset to primitive indices
            for (int i = 0; i < model.meshes.size(); i++)
            {
                for (int j = 0; j < model.meshes[i].primitives.size(); j++)
                {
                    model.m_meshesInfo[i].primitivesInfo[j].indexOffset += static_cast<uint32_t>(indicesCount);
                    m_primitivesCount++;
                }
            }
            indicesCount += model.m_indices.size();
        }

        vk::BufferMemoryBarrier2 indexBarrierInfo{};
        indexBarrierInfo.buffer = m_buffer->ApiHandle();
        indexBarrierInfo.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        indexBarrierInfo.dstAccessMask = vk::AccessFlagBits2::eIndexRead;
        indexBarrierInfo.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
        indexBarrierInfo.dstStageMask = vk::PipelineStageFlagBits2::eVertexInput;
        indexBarrierInfo.size = indicesCount * sizeof(uint32_t);
        indexBarrierInfo.offset = 0; // index data starts at the beginning of the buffer
        cmd->BufferBarrier(indexBarrierInfo);

        // Aabb indices
        m_aabbIndicesOffset = indicesCount * sizeof(uint32_t);
        cmd->CopyBufferStaged(m_buffer, m_aabbIndices.data(), m_aabbIndices.size() * sizeof(uint32_t), m_aabbIndicesOffset);

        vk::BufferMemoryBarrier2 aabbIndexBarrierInfo{};
        aabbIndexBarrierInfo.buffer = m_buffer->ApiHandle();
        aabbIndexBarrierInfo.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        aabbIndexBarrierInfo.dstAccessMask = vk::AccessFlagBits2::eIndexRead;
        aabbIndexBarrierInfo.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
        aabbIndexBarrierInfo.dstStageMask = vk::PipelineStageFlagBits2::eVertexInput;
        aabbIndexBarrierInfo.size = m_aabbIndices.size() * sizeof(uint32_t);
        aabbIndexBarrierInfo.offset = m_aabbIndicesOffset;
        cmd->BufferBarrier(aabbIndexBarrierInfo);
    }

    void Geometry::CopyVertices(CommandBuffer *cmd)
    {
        // vertices
        m_verticesOffset = m_aabbIndicesOffset + 24 * sizeof(uint32_t);
        size_t verticesCount = 0;
        for (auto &modelPtr : m_models)
        {
            ModelGltf &model = *modelPtr;
            cmd->CopyBufferStaged(m_buffer, model.m_vertices.data(), sizeof(Vertex) * model.m_vertices.size(), m_verticesOffset + verticesCount * sizeof(Vertex));
            // add the combined buffer offset to primitive vertices
            for (int i = 0; i < model.meshes.size(); i++)
            {
                for (int j = 0; j < model.meshes[i].primitives.size(); j++)
                {
                    model.m_meshesInfo[i].primitivesInfo[j].vertexOffset += static_cast<uint32_t>(verticesCount);
                }
            }
            verticesCount += model.m_vertices.size();
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

        // position vertices for depth and shadows
        m_positionsOffset = m_verticesOffset + verticesCount * sizeof(Vertex);
        size_t positionsCount = 0;
        for (auto &modelPtr : m_models)
        {
            ModelGltf &model = *modelPtr;
            cmd->CopyBufferStaged(m_buffer, model.m_positionUvs.data(), model.m_positionUvs.size() * sizeof(PositionUvVertex), m_positionsOffset + positionsCount * sizeof(PositionUvVertex));
            // add the combined buffer offset to primitive vertices
            for (int i = 0; i < model.meshes.size(); i++)
            {
                for (int j = 0; j < model.meshes[i].primitives.size(); j++)
                {
                    model.m_meshesInfo[i].primitivesInfo[j].positionsOffset += static_cast<uint32_t>(positionsCount);
                }
            }
            positionsCount += model.m_positionUvs.size();
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

        // Aabb vertices
        m_aabbVerticesOffset = m_positionsOffset + positionsCount * sizeof(PositionUvVertex);
        size_t aabbCount = 0;
        for (auto &modelPtr : m_models)
        {
            ModelGltf &model = *modelPtr;
            cmd->CopyBufferStaged(m_buffer, model.m_aabbVertices.data(), model.m_aabbVertices.size() * sizeof(AabbVertex), m_aabbVerticesOffset + aabbCount * sizeof(AabbVertex));
            // add the combined buffer offset to primitive vertices
            for (int i = 0; i < model.meshes.size(); i++)
            {
                for (int j = 0; j < model.meshes[i].primitives.size(); j++)
                {
                    model.m_meshesInfo[i].primitivesInfo[j].aabbVertexOffset += static_cast<uint32_t>(aabbCount);
                }
            }
            aabbCount += model.m_aabbVertices.size();
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

    void Geometry::CreateStorageBuffers()
    {
        // storage buffer
        size_t storageSize = sizeof(PerFrameData);
        // will store ids of constant buffer for each primitive
        storageSize += RHII.AlignStorageAs(m_primitivesCount * sizeof(uint32_t), 64);
        for (auto &modelPtr : m_models)
        {
            ModelGltf &model = *modelPtr;
            for (int i = 0; i < model.nodes.size(); i++)
            {
                int mesh = model.nodes[i].mesh;
                if (mesh < 0)
                    continue;

                model.m_meshesInfo[mesh].dataOffset = storageSize;
                storageSize += model.m_meshesInfo[mesh].dataSize;
                for (int j = 0; j < model.meshes[mesh].primitives.size(); j++)
                {
                    model.m_meshesInfo[mesh].primitivesInfo[j].dataOffset = storageSize;
                    storageSize += model.m_meshesInfo[mesh].primitivesInfo[j].dataSize;
                }
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

    void Geometry::CreateIndirectBuffers(CommandBuffer *cmd)
    {
        // indirect commands
        uint32_t indirectCount = 0;
        m_indirectCommands.clear();
        m_indirectCommands.reserve(m_primitivesCount);
        for (auto &modelPtr : m_models)
        {
            ModelGltf &model = *modelPtr;
            for (int i = 0; i < model.nodes.size(); i++)
            {
                int mesh = model.nodes[i].mesh;
                if (mesh < 0)
                    continue;

                for (int j = 0; j < model.meshes[mesh].primitives.size(); j++)
                {
                    PrimitiveInfo &primitiveInfo = model.m_meshesInfo[mesh].primitivesInfo[j];
                    primitiveInfo.indirectIndex = indirectCount;

                    vk::DrawIndexedIndirectCommand indirectCommand{};
                    indirectCommand.indexCount = primitiveInfo.indicesCount;
                    indirectCommand.instanceCount = 1;
                    indirectCommand.firstIndex = primitiveInfo.indexOffset;
                    indirectCommand.vertexOffset = primitiveInfo.vertexOffset;
                    indirectCommand.firstInstance = indirectCount;
                    m_indirectCommands.push_back(indirectCommand);

                    indirectCount++;
                }
            }
        }

        PE_ERROR_IF(indirectCount != m_primitivesCount, "Geometry::UploadBuffers: Indirect count mismatch!");

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

    void Geometry::UpdateImageViews()
    {
        m_imageViews.clear();
        OrderedMap<Image *, uint32_t> imagesMap{};
        for (auto &model : m_models)
        {
            for (auto &storage : m_storages)
                model->SetPrimitiveFactors(storage);

            // Update views and indices
            for (Image *image : model->m_images)
            {
                imagesMap.insert(image, static_cast<uint32_t>(m_imageViews.size()));
                m_imageViews.push_back(image->GetSRV());
            }

            for (int i = 0; i < model->meshes.size(); i++)
            {
                tinygltf::Mesh &mesh = model->meshes[i];
                MeshInfo &meshInfo = model->m_meshesInfo[i];
                for (int j = 0; j < mesh.primitives.size(); j++)
                {
                    PrimitiveInfo &primitiveInfo = meshInfo.primitivesInfo[j];
                    for (int k = 0; k < 5; k++)
                    {
                        primitiveInfo.viewsIndex[k] = imagesMap[primitiveInfo.images[k]];
                    }
                }
            }
        }

        for (auto &&dirtyView : m_dirtyDescriptorViews)
            dirtyView = true;
    }

    void Geometry::CopyGBufferConstants(CommandBuffer *cmd)
    {
        // GbufferPass constants initialization
        GbufferOpaquePass *gbo = GetGlobalComponent<GbufferOpaquePass>();
        GbufferTransparentPass *gbt = GetGlobalComponent<GbufferTransparentPass>();
        gbo->m_constants = Buffer::Create(
            m_primitivesCount * sizeof(Primitive_Constants),
            vk::BufferUsageFlagBits2::eStorageBuffer | vk::BufferUsageFlagBits2::eTransferDst,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            "GbufferPass_constants");
        gbt->m_constants = gbo->m_constants;

        // fill GbufferPass buffer with the constants
        size_t offset = 0;
        gbo->m_constants->Map();
        for (auto &modelPtr : m_models)
        {
            ModelGltf &model = *modelPtr;
            for (int i = 0; i < model.nodes.size(); i++)
            {
                int mesh = model.nodes[i].mesh;
                if (mesh < 0)
                    continue;

                for (int j = 0; j < model.meshes[mesh].primitives.size(); j++)
                {
                    PrimitiveInfo &primitiveInfo = model.m_meshesInfo[mesh].primitivesInfo[j];
                    auto &primitiveGltf = model.meshes[mesh].primitives[j];

                    Primitive_Constants constants{};
                    constants.alphaCut = static_cast<float>(model.materials[primitiveGltf.material].alphaCutoff);
                    constants.meshDataOffset = static_cast<uint32_t>(model.m_meshesInfo[mesh].dataOffset);
                    constants.primitiveDataOffset = static_cast<uint32_t>(primitiveInfo.dataOffset);
                    for (int k = 0; k < 5; k++)
                        constants.primitiveImageIndex[k] = primitiveInfo.viewsIndex[k];

                    BufferRange range{};
                    range.data = &constants;
                    range.offset = offset;
                    range.size = sizeof(Primitive_Constants);
                    gbo->m_constants->Copy(1, &range, true);

                    offset += sizeof(Primitive_Constants);
                }
            }
        }
        gbo->m_constants->Flush();
        gbo->m_constants->Unmap();
    }

    void Geometry::CullNodePrimitives(ModelGltf &model, int node)
    {
        const Camera &camera = *m_cameras[0];
        bool frustumCulling = Settings::Get<GlobalSettings>().frustum_culling;

        int mesh = model.nodes[node].mesh;
        auto &meshGltf = model.meshes[mesh];
        auto &meshInfo = model.m_meshesInfo[mesh];
        int primitivesCount = static_cast<int>(meshGltf.primitives.size());
        for (int i = 0; i < primitivesCount; i++)
        {
            auto &primitiveInfo = meshInfo.primitivesInfo[i];
            primitiveInfo.cull = frustumCulling ? !camera.AABBInFrustum(primitiveInfo.worldBoundingBox) : false;
            if (!primitiveInfo.cull)
            {
                vec3 center = primitiveInfo.worldBoundingBox.GetCenter();
                float distance = distance2(camera.GetPosition(), center);

                switch (primitiveInfo.renderType)
                {
                case RenderType::Opaque:
                    m_drawInfosOpaque.emplace_back(DrawInfo{&model, node, i, distance});
                    break;
                case RenderType::AlphaCut:
                    m_drawInfosAlphaCut.emplace_back(DrawInfo{&model, node, i, distance});
                    break;
                case RenderType::AlphaBlend:
                    m_drawInfosAlphaBlend.emplace_back(DrawInfo{&model, node, i, distance});
                    break;
                }
            }
        }
    }

    void Geometry::UpdateUniformData()
    {
        uint32_t frame = RHII.GetFrameIndex();

        // Map buffer
        m_storages[frame]->Map();

        // Update per frame data
        m_frameData.viewProjection = m_cameras[0]->GetViewProjection();
        m_frameData.previousViewProjection = m_cameras[0]->GetPreviousViewProjection();

        BufferRange range{};
        range.data = &m_frameData;
        range.size = sizeof(PerFrameData);
        range.offset = 0;
        m_storages[frame]->Copy(1, &range, true);

        // Update per primitive id dat
        std::vector<uint32_t> ids{};
        ids.reserve(m_drawInfosOpaque.size() + m_drawInfosAlphaCut.size() + m_drawInfosAlphaBlend.size());
        for (auto &drawInfo : m_drawInfosOpaque)
        {
            auto &model = *drawInfo.model;
            auto &primitiveInfo = model.m_meshesInfo[model.nodes[drawInfo.node].mesh].primitivesInfo[drawInfo.primitive];
            uint32_t id = primitiveInfo.indirectIndex;
            ids.push_back(id);
        }
        for (auto &drawInfo : m_drawInfosAlphaCut)
        {
            auto &model = *drawInfo.model;
            auto &primitiveInfo = model.m_meshesInfo[model.nodes[drawInfo.node].mesh].primitivesInfo[drawInfo.primitive];
            uint32_t id = primitiveInfo.indirectIndex;
            ids.push_back(id);
        }
        for (auto &drawInfo : m_drawInfosAlphaBlend)
        {
            auto &model = *drawInfo.model;
            auto &primitiveInfo = model.m_meshesInfo[model.nodes[drawInfo.node].mesh].primitivesInfo[drawInfo.primitive];
            uint32_t id = primitiveInfo.indirectIndex;
            ids.push_back(id);
        }
        range.data = ids.data();
        range.size = ids.size() * sizeof(uint32_t);
        range.offset = sizeof(PerFrameData);
        m_storages[frame]->Copy(1, &range, true);

        for (auto &modelPtr : m_models)
        {
            ModelGltf &model = *modelPtr;
            if (!model.dirtyUniforms[frame])
                continue;

            for (int node = 0; node < model.nodes.size(); node++)
            {
                NodeInfo &nodeInfo = model.m_nodesInfo[node];
                if (!nodeInfo.dirtyUniforms[frame])
                    continue;

                int mesh = model.nodes[node].mesh;
                if (mesh < 0)
                    continue;

                MeshInfo &meshInfo = model.m_meshesInfo[mesh];

                range.data = &nodeInfo.ubo;
                range.size = meshInfo.dataSize;
                range.offset = meshInfo.dataOffset;
                m_storages[frame]->Copy(1, &range, true);

                nodeInfo.dirtyUniforms[frame] = false;
            }

            model.dirtyUniforms[frame] = false;
        }

        // Unmap buffer
        m_storages[frame]->Unmap();
    }

    void Geometry::UpdateIndirectData()
    {
        uint32_t frame = RHII.GetFrameIndex();

        m_indirects[frame]->Map();

        uint32_t firstInstance = 0;
        for (auto &drawInfo : m_drawInfosOpaque)
        {
            auto &model = *drawInfo.model;
            auto &primitiveInfo = model.m_meshesInfo[model.nodes[drawInfo.node].mesh].primitivesInfo[drawInfo.primitive];
            auto &indirectCommand = m_indirectCommands[primitiveInfo.indirectIndex];
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
            auto &primitiveInfo = model.m_meshesInfo[model.nodes[drawInfo.node].mesh].primitivesInfo[drawInfo.primitive];
            auto &indirectCommand = m_indirectCommands[primitiveInfo.indirectIndex];
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
            auto &primitiveInfo = model.m_meshesInfo[model.nodes[drawInfo.node].mesh].primitivesInfo[drawInfo.primitive];
            auto &indirectCommand = m_indirectCommands[primitiveInfo.indirectIndex];
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

    void Geometry::UpdateGeometry()
    {
        ClearDrawInfos(true);

        std::vector<std::shared_future<void>> futures;
        for (auto &modelPtr : m_models)
        {
            ModelGltf &model = *modelPtr;
            if (!model.m_render)
                continue;

            // update node world matrices if needed
            model.UpdateNodeMatrices();

            // cull primitives
            for (int i = 0; i < model.nodes.size(); i++)
            {
                if (model.nodes[i].mesh > -1)
                    futures.push_back(ThreadPool::Update.Enqueue(&Geometry::CullNodePrimitives, this, std::ref(model), i));
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

    void Geometry::SortDrawInfos()
    {
        std::sort(m_drawInfosOpaque.begin(), m_drawInfosOpaque.end(), [](const DrawInfo &a, const DrawInfo &b)
                  { return a.distance < b.distance; });
        std::sort(m_drawInfosAlphaCut.begin(), m_drawInfosAlphaCut.end(), [](const DrawInfo &a, const DrawInfo &b)
                  { return a.distance > b.distance; });
        std::sort(m_drawInfosAlphaBlend.begin(), m_drawInfosAlphaBlend.end(), [](const DrawInfo &a, const DrawInfo &b)
                  { return a.distance > b.distance; });
    }

    void Geometry::ClearDrawInfos(bool reserveMax)
    {
        m_drawInfosOpaque.clear();
        m_drawInfosAlphaCut.clear();
        m_drawInfosAlphaBlend.clear();

        if (reserveMax)
        {
            uint32_t maxOpaque = 0;
            uint32_t maxAlphaCut = 0;
            uint32_t maxAlphaBlend = 0;

            // count max primitives
            for (auto &modelPtr : m_models)
            {
                ModelGltf &model = *modelPtr;
                if (!model.m_render)
                    continue;

                for (int node = 0; node < model.nodes.size(); node++)
                {
                    int mesh = model.nodes[node].mesh;
                    if (mesh < 0)
                        continue;

                    for (int primitive = 0; primitive < model.meshes[mesh].primitives.size(); primitive++)
                    {
                        PrimitiveInfo &primitiveInfo = model.m_meshesInfo[mesh].primitivesInfo[primitive];

                        switch (primitiveInfo.renderType)
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
            }

            m_drawInfosOpaque.reserve(maxOpaque);
            m_drawInfosAlphaCut.reserve(maxAlphaCut);
            m_drawInfosAlphaBlend.reserve(maxAlphaBlend);
        }
    }

    void Geometry::DestroyBuffers()
    {
        Buffer::Destroy(m_buffer);

        for (auto &storage : m_storages)
            Buffer::Destroy(storage);

        for (auto &indirect : m_indirects)
            Buffer::Destroy(indirect);

        Buffer::Destroy(m_indirectAll);
    }
}
