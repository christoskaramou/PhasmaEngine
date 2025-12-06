#include "Scene/Model.h"
#include "Scene/ModelAssimp.h"
#include "API/Buffer.h"
#include "API/RHI.h"
#include "API/Image.h"
#include "Systems/RendererSystem.h"
#include "Scene/Scene.h"

namespace pe
{
    Model::Model() : m_id{ID::NextID()}
    {
        dirtyUniforms.resize(RHII.GetSwapchainImageCount(), false);
        m_label = "Model_" + std::to_string(m_id);
    }

    Model *Model::Load(const std::filesystem::path &file)
    {
        PE_ERROR_IF(!std::filesystem::exists(file), std::string("Model file not found: " + file.string()).c_str());

        std::string ext = file.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c)
                       { return std::tolower(c); });

        return ModelAssimp::Load(file);
    }

    Model::~Model()
    {
        for (auto sampler : m_samplers)
            Sampler::Destroy(sampler);
        for (auto image : m_images)
            Image::Destroy(image);
    }

    void Model::UpdateNodeMatrices()
    {
        if (!dirtyNodes)
            return;

        for (int i = 0; i < GetNodeCount(); i++)
        {
            UpdateNodeMatrix(i);
        }

        dirtyNodes = false;
    }

    void Model::UpdateNodeMatrix(int node)
    {
        if (node < 0 || node >= static_cast<int>(m_nodeInfos.size()))
            return;

        NodeInfo &nodeInfo = m_nodeInfos[node];
        nodeInfo.ubo.previousWorldMatrix = nodeInfo.ubo.worldMatrix;
        if (nodeInfo.dirty)
        {
            mat4 trans = nodeInfo.localMatrix;
            int parent = nodeInfo.parent;
            while (parent >= 0)
            {
                const NodeInfo &parentInfo = m_nodeInfos[parent];
                trans = parentInfo.localMatrix * trans;
                parent = parentInfo.parent;
            }
            nodeInfo.ubo.worldMatrix = matrix * trans;
        }

        if (nodeInfo.dirty)
        {
            int mesh = GetNodeMesh(node);
            if (mesh >= 0)
            {
                MeshInfo &meshInfo = m_meshInfos[mesh];
                meshInfo.worldBoundingBox.min = nodeInfo.ubo.worldMatrix * vec4(meshInfo.boundingBox.min, 1.f);
                meshInfo.worldBoundingBox.max = nodeInfo.ubo.worldMatrix * vec4(meshInfo.boundingBox.max, 1.f);
            }

            nodeInfo.dirty = false;
            for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
            {
                nodeInfo.dirtyUniforms[i] = true;
                dirtyUniforms[i] = true;
            }
        }
    }

    void Model::SetMeshFactors(Buffer *uniformBuffer)
    {
        // copy factors in uniform buffer
        BufferRange range{};
        uniformBuffer->Map();

        for (int i = 0; i < GetNodeCount(); i++)
        {
            int mesh = GetNodeMesh(i);
            if (mesh < 0)
                continue;

            auto &meshInfo = m_meshInfos[mesh];
            range.data = &meshInfo.materialFactors;
            range.size = sizeof(mat4);
            range.offset = meshInfo.GetMeshDataOffset();
            uniformBuffer->Copy(1, &range, true);
        }

        uniformBuffer->Unmap();
    }

    void Model::UploadBuffers(CommandBuffer *cmd)
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        auto &progress = gSettings.loading_current;
        auto &total = gSettings.loading_total;
        auto &loading = gSettings.loading_name;

        progress = 0;
        total = 1;
        loading = "Uploading buffers";

        Scene &scene = GetGlobalSystem<RendererSystem>()->GetScene();
        scene.AddModel(this);
        scene.UpdateGeometryBuffers(cmd);

        progress++;
    }
}
