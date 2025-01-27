#include "Scene/Geometry.h"
#include "Scene/Model.h"
#include "Renderer/Buffer.h"
#include "Renderer/Command.h"
#include "Renderer/Vertex.h"
#include "Renderer/Image.h"
#include "GUI/GUI.h"
#include "Camera/Camera.h"
#include "Systems/RendererSystem.h"
#include "Systems/LightSystem.h"
#include "Renderer/RHI.h"
#include "Renderer/Queue.h"
#include "Renderer/RenderPasses/GbufferPass.h"

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
    }

    Geometry::~Geometry()
    {
        for (auto *model : m_models)
        {
            delete model;
        }
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
        // All models geometry is combined into one buffer with this order:
        // Indices - Vertices

        // if reuploading, destroy old buffers
        DestroyBuffers();
        size_t numberOfIndices = 24; // with aabb indices
        size_t numberOfVertices = 0;
        size_t numberOfAabbVertices = 0;
        size_t storageSize = 0;
        uint32_t indirectCount = 0;
        m_primitivesCount = 0;

        // calculate offsets
        for (auto &model : m_models)
        {
            numberOfIndices += model->m_indices.size();
            numberOfVertices += model->m_vertices.size();
            numberOfAabbVertices += model->m_aabbVertices.size();
        }

        // create buffers
        m_buffer = Buffer::Create(
            sizeof(uint32_t) * numberOfIndices + sizeof(Vertex) * numberOfVertices + sizeof(PositionUvVertex) * numberOfVertices + sizeof(AabbVertex) * numberOfAabbVertices,
            BufferUsage::TransferDstBit | BufferUsage::IndexBufferBit | BufferUsage::VertexBufferBit,
            AllocationCreate::DedicatedMemoryBit,
            "combined_Geometry_buffer");

        // indices
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
        BufferBarrierInfo indexBarrierInfo{};
        indexBarrierInfo.buffer = m_buffer;
        indexBarrierInfo.srcAccess = Access::TransferWriteBit;
        indexBarrierInfo.dstAccess = Access::IndexReadBit;
        indexBarrierInfo.srcStage = PipelineStage::TransferBit;
        indexBarrierInfo.dstStage = PipelineStage::VertexInputBit;
        indexBarrierInfo.size = indicesCount * sizeof(uint32_t);
        indexBarrierInfo.offset = 0; // index data starts at the beginning of the buffer
        cmd->BufferBarrier(indexBarrierInfo);

        // Aabb indices
        m_aabbIndicesOffset = indicesCount * sizeof(uint32_t);
        cmd->CopyBufferStaged(m_buffer, m_aabbIndices.data(), m_aabbIndices.size() * sizeof(uint32_t), m_aabbIndicesOffset);

        BufferBarrierInfo aabbIndexBarrierInfo{};
        aabbIndexBarrierInfo.buffer = m_buffer;
        aabbIndexBarrierInfo.srcAccess = Access::TransferWriteBit;
        aabbIndexBarrierInfo.dstAccess = Access::IndexReadBit;
        aabbIndexBarrierInfo.srcStage = PipelineStage::TransferBit;
        aabbIndexBarrierInfo.dstStage = PipelineStage::VertexInputBit;
        aabbIndexBarrierInfo.size = m_aabbIndices.size() * sizeof(uint32_t);
        aabbIndexBarrierInfo.offset = m_aabbIndicesOffset;
        cmd->BufferBarrier(aabbIndexBarrierInfo);

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

        BufferBarrierInfo vertexBarrierInfo{};
        vertexBarrierInfo.buffer = m_buffer;
        vertexBarrierInfo.srcAccess = Access::TransferWriteBit;
        vertexBarrierInfo.dstAccess = Access::VertexAttributeReadBit;
        vertexBarrierInfo.srcStage = PipelineStage::TransferBit;
        vertexBarrierInfo.dstStage = PipelineStage::VertexInputBit;
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

        BufferBarrierInfo posVertexBarrierInfo{};
        posVertexBarrierInfo.buffer = m_buffer;
        posVertexBarrierInfo.srcAccess = Access::TransferWriteBit;
        posVertexBarrierInfo.dstAccess = Access::VertexAttributeReadBit;
        posVertexBarrierInfo.srcStage = PipelineStage::TransferBit;
        posVertexBarrierInfo.dstStage = PipelineStage::VertexInputBit;
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

        BufferBarrierInfo aabbVertexBarrierInfo{};
        aabbVertexBarrierInfo.buffer = m_buffer;
        aabbVertexBarrierInfo.srcAccess = Access::TransferWriteBit;
        aabbVertexBarrierInfo.dstAccess = Access::VertexAttributeReadBit;
        aabbVertexBarrierInfo.srcStage = PipelineStage::TransferBit;
        aabbVertexBarrierInfo.dstStage = PipelineStage::VertexInputBit;
        aabbVertexBarrierInfo.size = aabbCount * sizeof(AabbVertex);
        aabbVertexBarrierInfo.offset = m_aabbVerticesOffset;
        cmd->BufferBarrier(aabbVertexBarrierInfo);

        // storage buffer
        storageSize = sizeof(PerFrameData);
        // will store ids of constant buffer for each primitive
        storageSize += RHII.AlignStorageAs<64>(m_primitivesCount * sizeof(uint32_t));
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

        for (int i = 0; i < SWAPCHAIN_IMAGES; i++)
        {
            m_storage[i] = Buffer::Create(
                storageSize,
                BufferUsage::StorageBufferBit,
                AllocationCreate::HostAccessSequentialWriteBit,
                "storage_Geometry_buffer_" + std::to_string(i));
        }

        // indirect commands
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

                    DrawIndexedIndirectCommand indirectCommand{};
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
            indirectCount * sizeof(DrawIndexedIndirectCommand),
            BufferUsage::IndirectBufferBit | BufferUsage::TransferDstBit,
            AllocationCreate::DedicatedMemoryBit,
            "indirect_Geometry_buffer_all");
        cmd->CopyBufferStaged(m_indirectAll, m_indirectCommands.data(), m_indirectCommands.size() * sizeof(DrawIndexedIndirectCommand), 0);  

        BufferBarrierInfo indirectBarrierInfo{};
        indirectBarrierInfo.buffer = m_indirectAll;
        indirectBarrierInfo.srcAccess = Access::TransferWriteBit;
        indirectBarrierInfo.dstAccess = Access::IndirectCommandReadBit;
        indirectBarrierInfo.srcStage = PipelineStage::TransferBit;
        indirectBarrierInfo.dstStage = PipelineStage::DrawIndirectBit;
        indirectBarrierInfo.size = indirectCount * sizeof(DrawIndexedIndirectCommand);
        indirectBarrierInfo.offset = 0;
        cmd->BufferBarrier(indirectBarrierInfo);      

        for (int i = 0; i < SWAPCHAIN_IMAGES; i++)
        {
            m_indirect[i] = Buffer::Create(
                indirectCount * sizeof(DrawIndexedIndirectCommand),
                BufferUsage::IndirectBufferBit | BufferUsage::TransferDstBit,
                AllocationCreate::HostAccessSequentialWriteBit,
                "indirect_Geometry_buffer_" + std::to_string(i));
        }

        m_imageViews.clear();
        OrderedMap<Image *, uint32_t> imagesMap{};
        for (auto &model : m_models)
        {
            for (int i = 0; i < SWAPCHAIN_IMAGES; i++)
            {
                model->SetPrimitiveFactors(m_storage[i]);
            }

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

        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
            m_dirtyDescriptorViews[i] = true;

        // GbufferPass constants initialization
        GbufferOpaquePass *gbo = GetGlobalComponent<GbufferOpaquePass>();
        GbufferTransparentPass *gbt = GetGlobalComponent<GbufferTransparentPass>();
        gbo->m_constants = Buffer::Create(
            m_primitivesCount * sizeof(Primitive_Constants),
            BufferUsage::StorageBufferBit | BufferUsage::TransferDstBit,
            AllocationCreate::HostAccessSequentialWriteBit,
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

                    MemoryRange mr{};
                    mr.data = &constants;
                    mr.offset = offset;
                    mr.size = sizeof(Primitive_Constants);
                    gbo->m_constants->Copy(1, &mr, true);

                    offset += sizeof(Primitive_Constants);
                }
            }
        }
        gbo->m_constants->Flush();
        gbo->m_constants->Unmap();
    }

    void Geometry::CullNodePrimitives(ModelGltf &model, int node)
    {
        if (node < 0)
            return;

        int mesh = model.nodes[node].mesh;
        if (mesh < 0)
            return;

        const Camera &camera = *m_cameras[0];
        auto &meshGltf = model.meshes[mesh];
        auto &meshInfo = model.m_meshesInfo[mesh];
        auto &primitivesInfo = meshInfo.primitivesInfo;

        // TODO: pack more than one primitive in a task
        auto cullAsync = [&camera, &model, &primitivesInfo](int node, int startPrimitive, int count)
        {
            std::vector<DrawInfo> results{};
            results.reserve(count);

            const int endPrimitive = startPrimitive + count;
            for (int primitive = startPrimitive; primitive < endPrimitive; primitive++)
            {
                auto &primitiveInfo = primitivesInfo[primitive];

                if (Settings::Get<GlobalSettings>().frustum_culling)
                {
                    primitiveInfo.cull = !camera.AABBInFrustum(primitiveInfo.worldBoundingBox);
                }
                else
                {
                    primitiveInfo.cull = false;
                }

                if (!primitiveInfo.cull)
                {
                    vec3 center = primitiveInfo.worldBoundingBox.GetCenter();
                    float distance = distance2(camera.GetPosition(), center);

                    results.emplace_back(DrawInfo{&model, node, primitive, distance});
                }
            }

            return results;
        };

        int primitivesCount = static_cast<int>(meshGltf.primitives.size());
        std::vector<Task<std::vector<DrawInfo>>> tasks{};
        tasks.reserve(primitivesCount);
        int culls_per_task = Settings::Get<GlobalSettings>().culls_per_task;
        for (int primitive = 0; primitive < primitivesCount; primitive += culls_per_task)
        {
            if (primitive + culls_per_task > primitivesCount)
                culls_per_task = primitivesCount - primitive;

            auto task = e_Update_ThreadPool.Enqueue(cullAsync, node, primitive, culls_per_task);
            tasks.push_back(std::move(task));
        }

        for (auto &task : tasks)
        {
            auto drawInfos = task.get();

            for (auto &drawInfo : drawInfos)
            {
                PrimitiveInfo &primitiveInfo = primitivesInfo[drawInfo.primitive];

                switch (primitiveInfo.renderType)
                {
                case RenderType::Opaque:
                    m_drawInfosOpaque.push_back(drawInfo);
                    break;
                case RenderType::AlphaCut:
                    m_drawInfosAlphaCut.push_back(drawInfo);
                    break;
                case RenderType::AlphaBlend:
                    m_drawInfosAlphaBlend.push_back(drawInfo);
                    break;
                }
            }
        }

        auto &children = model.nodes[node].children;
        for (int child : children)
        {
            CullNodePrimitives(model, child);
        }
    }

    void Geometry::UpdateUniformData()
    {
        uint32_t frame = RHII.GetFrameIndex();

        // Map buffer
        m_storage[frame]->Map();

        // Update per frame data
        m_frameData.viewProjection = m_cameras[0]->GetViewProjection();
        m_frameData.previousViewProjection = m_cameras[0]->GetPreviousViewProjection();

        MemoryRange mr{};
        mr.data = &m_frameData;
        mr.size = sizeof(PerFrameData);
        mr.offset = 0;
        m_storage[frame]->Copy(1, &mr, true);

        // Update per primitive id data
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
        mr.data = ids.data();
        mr.size = ids.size() * sizeof(uint32_t);
        mr.offset = sizeof(PerFrameData);
        m_storage[frame]->Copy(1, &mr, true);

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

                tinygltf::Node &nodeGltf = model.nodes[node];
                if (nodeGltf.mesh < 0)
                    continue;

                MeshInfo &meshInfo = model.m_meshesInfo[nodeGltf.mesh];

                mr.data = &nodeInfo.ubo;
                mr.offset = meshInfo.dataOffset;
                mr.size = meshInfo.dataSize;
                m_storage[frame]->Copy(1, &mr, true);

                nodeInfo.dirtyUniforms[frame] = false;
            }

            model.dirtyUniforms[frame] = false;
        }

        // Unmap buffer
        m_storage[frame]->Unmap();
    }

    void Geometry::UpdateIndirectData()
    {
        uint32_t frame = RHII.GetFrameIndex();

        m_indirect[frame]->Map();

        uint32_t firstInstance = 0;
        for (auto &drawInfo : m_drawInfosOpaque)
        {
            auto &model = *drawInfo.model;
            auto &primitiveInfo = model.m_meshesInfo[model.nodes[drawInfo.node].mesh].primitivesInfo[drawInfo.primitive];
            auto &indirectCommand = m_indirectCommands[primitiveInfo.indirectIndex];
            indirectCommand.firstInstance = firstInstance;

            MemoryRange mr{};
            mr.data = &indirectCommand;
            mr.size = sizeof(DrawIndexedIndirectCommand);
            mr.offset = firstInstance * sizeof(DrawIndexedIndirectCommand);
            m_indirect[frame]->Copy(1, &mr, true);

            firstInstance++;
        }

        for (auto &drawInfo : m_drawInfosAlphaCut)
        {
            auto &model = *drawInfo.model;
            auto &primitiveInfo = model.m_meshesInfo[model.nodes[drawInfo.node].mesh].primitivesInfo[drawInfo.primitive];
            auto &indirectCommand = m_indirectCommands[primitiveInfo.indirectIndex];
            indirectCommand.firstInstance = firstInstance;

            MemoryRange mr{};
            mr.data = &indirectCommand;
            mr.size = sizeof(DrawIndexedIndirectCommand);
            mr.offset = firstInstance * sizeof(DrawIndexedIndirectCommand);
            m_indirect[frame]->Copy(1, &mr, true);

            firstInstance++;
        }

        for (auto &drawInfo : m_drawInfosAlphaBlend)
        {
            auto &model = *drawInfo.model;
            auto &primitiveInfo = model.m_meshesInfo[model.nodes[drawInfo.node].mesh].primitivesInfo[drawInfo.primitive];
            auto &indirectCommand = m_indirectCommands[primitiveInfo.indirectIndex];
            indirectCommand.firstInstance = firstInstance;

            MemoryRange mr{};
            mr.data = &indirectCommand;
            mr.size = sizeof(DrawIndexedIndirectCommand);
            mr.offset = firstInstance * sizeof(DrawIndexedIndirectCommand);
            m_indirect[frame]->Copy(1, &mr, true);

            firstInstance++;
        }

        m_indirect[frame]->Flush();
        m_indirect[frame]->Unmap();
    }

    void Geometry::UpdateGeometry()
    {
        ClearDrawInfos(true);

        auto cullNodePrimitives = [this](ModelGltf &model, int node)
        {
            CullNodePrimitives(model, node);
        };

        std::vector<Task<void>> tasks{};
        for (auto &modelPtr : m_models)
        {
            ModelGltf &model = *modelPtr;
            if (!model.render)
                continue;

            // update node world matrices if needed
            model.UpdateNodeMatrices();

            // cull primitives
            for (auto &scene : model.scenes)
            {
                for (int node : scene.nodes)
                {
                    tasks.push_back(e_Update_ThreadPool.Enqueue(cullNodePrimitives, std::ref(model), node));
                }
            }
        }

        for (auto &task : tasks)
            task.get();

        // front to back
        std::sort(m_drawInfosOpaque.begin(), m_drawInfosOpaque.end(), [](const DrawInfo &a, const DrawInfo &b)
                  { return a.distance < b.distance; });
        // back to front
        std::sort(m_drawInfosAlphaCut.begin(), m_drawInfosAlphaCut.end(), [](const DrawInfo &a, const DrawInfo &b)
                  { return a.distance > b.distance; });
        // back to front
        std::sort(m_drawInfosAlphaBlend.begin(), m_drawInfosAlphaBlend.end(), [](const DrawInfo &a, const DrawInfo &b)
                  { return a.distance > b.distance; });

        if (HasDrawInfo())
        {
            UpdateUniformData();
            UpdateIndirectData();
        }
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
                if (!model.render)
                    continue;

                for (auto &scene : model.scenes)
                {
                    for (int node : scene.nodes)
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
            }

            m_drawInfosOpaque.reserve(maxOpaque);
            m_drawInfosAlphaCut.reserve(maxAlphaCut);
            m_drawInfosAlphaBlend.reserve(maxAlphaBlend);
        }
    }

    void Geometry::DestroyBuffers()
    {
        Buffer::Destroy(m_buffer);
        for (int i = 0; i < SWAPCHAIN_IMAGES; i++)
        {
            Buffer::Destroy(m_storage[i]);
            Buffer::Destroy(m_indirect[i]);
        }
    }
}
