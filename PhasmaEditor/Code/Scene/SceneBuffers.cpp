#include "Scene/Scene.h"
#include "Scene/ModelAsset.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/RHI.h"
#include "API/Vertex.h"
#include "RenderPasses/GbufferPass.h"

namespace pe
{
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
} // namespace pe
