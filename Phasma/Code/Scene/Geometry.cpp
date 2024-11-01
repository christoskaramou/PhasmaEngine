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
        size_t uniformsSize = 0;

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
            AllocationCreate::None,
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

        // uniforms
        uniformsSize = 2 * sizeof(mat4); // view projection and previous view projection
        for (auto &modelPtr : m_models)
        {
            ModelGltf &model = *modelPtr;
            for (int i = 0; i < model.nodes.size(); i++)
            {
                int mesh = model.nodes[i].mesh;
                if (mesh < 0)
                    continue;

                model.m_meshesInfo[mesh].dataOffset = uniformsSize;
                uniformsSize += model.m_meshesInfo[mesh].dataSize;
                for (int j = 0; j < model.meshes[mesh].primitives.size(); j++)
                {
                    model.m_meshesInfo[mesh].primitivesInfo[j].dataOffset = uniformsSize;
                    uniformsSize += model.m_meshesInfo[mesh].primitivesInfo[j].dataSize;
                }
            }
        }

        for (int i = 0; i < SWAPCHAIN_IMAGES; i++)
        {
            m_uniforms[i] = Buffer::Create(
                uniformsSize,
                BufferUsage::StorageBufferBit,
                AllocationCreate::HostAccessSequentialWriteBit,
                "uniforms_Geometry_buffer_" + std::to_string(i));
        }

        m_imageViews.clear();
        OrderedMap<Image *, uint32_t> imagesMap{};
        for (auto &model : m_models)
        {
            for (int i = 0; i < SWAPCHAIN_IMAGES; i++)
            {
                model->SetPrimitiveFactors(m_uniforms[i]);
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

        m_mutex.lock();
        auto &opaque = m_opaque[&model][node];
        auto &alphaCut = m_alphaCut[&model][node];
        auto &alphaBlend = m_alphaBlend[&model][node];
        m_mutex.unlock();

        // TODO: pack more than one primitive in a task
        auto cullAsync = [this, &camera, &model, &meshInfo, &primitivesInfo, &opaque, &alphaCut, &alphaBlend](int node, int startPrimitive, int count)
        {
            const int endPrimitive = startPrimitive + count;
            for (int primitive = startPrimitive; primitive < endPrimitive; primitive++)
            {
                auto &primitiveInfo = primitivesInfo[primitive];

                if (Settings::Get<GlobalSettings>().frustum_culling)
                    primitiveInfo.cull = !camera.AABBInFrustum(primitiveInfo.worldBoundingBox);
                else
                    primitiveInfo.cull = false;

                if (!primitiveInfo.cull)
                {
                    glm::vec3 center = primitiveInfo.worldBoundingBox.GetCenter();
                    float distance = distance2(camera.GetPosition(), center);

                    DrawInfo drawInfo = {&model, node, primitive, distance};

                    m_mutexDrawInfos.lock();
                    switch (primitiveInfo.renderType)
                    {
                    case RenderType::Opaque:
                        m_drawInfosOpaque.insert({distance, drawInfo});
                        opaque.push_back(primitive);
                        break;
                    case RenderType::AlphaCut:
                        m_drawInfosAlphaCut.insert({distance, drawInfo});
                        alphaCut.push_back(primitive);
                        break;
                    case RenderType::AlphaBlend:
                        m_drawInfosAlphaBlend.insert({distance, drawInfo});
                        alphaBlend.push_back(primitive);
                        break;
                    }
                    m_mutexDrawInfos.unlock();
                }
            }
        };

        int primitivesCount = static_cast<int>(meshGltf.primitives.size());
        std::vector<Task<void>> tasks{};
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
            task.get();

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
        m_uniforms[frame]->Map();

        // Update camera data
        m_cameraData.viewProjection = m_cameras[0]->GetViewProjection();
        m_cameraData.previousViewProjection = m_cameras[0]->GetPreviousViewProjection();

        MemoryRange mr{};
        mr.data = &m_cameraData;
        mr.offset = 0;
        mr.size = sizeof(CameraData);
        m_uniforms[frame]->Copy(1, &mr, true);

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
                m_uniforms[frame]->Copy(1, &mr, true);

                nodeInfo.dirtyUniforms[frame] = false;
            }

            model.dirtyUniforms[frame] = false;
        }

        // Unmap buffer
        m_uniforms[frame]->Unmap();
    }

    void Geometry::UpdateGeometry()
    {
        ClearDrawInfos();

        std::vector<Task<void>> tasks{};

        auto cullNodePrimitives = [this](ModelGltf &model, int node)
        {
            CullNodePrimitives(model, node);
        };

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

        if (HasDrawInfo())
            UpdateUniformData();
    }

    void Geometry::ClearDrawInfos()
    {
        m_drawInfosOpaque.clear();
        m_drawInfosAlphaCut.clear();
        m_drawInfosAlphaBlend.clear();

        m_opaque.clear();
        m_alphaCut.clear();
        m_alphaBlend.clear();
    }

    void Geometry::DestroyBuffers()
    {
        Buffer::Destroy(m_buffer);
        m_buffer = nullptr;
        for (int i = 0; i < SWAPCHAIN_IMAGES; i++)
        {
            Buffer::Destroy(m_uniforms[i]);
            m_uniforms[i] = nullptr;
        }
    }
}
