#include "AabbsPass.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/Shader.h"
#include "API/Vulkan/VulkanImageImpl.h"
#include "Camera/Camera.h"
#include "Scene/Scene.h"
#include "ShadowPass.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    void AabbsPass::Init()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();

        m_viewportRT = rs->GetRenderTarget("viewport");
        m_depthRT = rs->GetDepthStencilTarget("depthStencil");
        m_scene = nullptr;

        m_attachments.resize(2);
        m_attachments[0] = {};
        m_attachments[0].image = m_viewportRT;
        m_attachments[0].loadOp = PE_LOAD_OP_LOAD;
        m_attachments[0].storeOp = PE_STORE_OP_STORE;
        m_attachments[1] = {};
        m_attachments[1].image = m_depthRT;
        m_attachments[1].loadOp = PE_LOAD_OP_LOAD;
        m_attachments[1].storeOp = PE_STORE_OP_STORE;
    }

    void AabbsPass::UpdatePassInfo()
    {
        m_passInfo->name = "AABBs_pipeline";
        m_passInfo->pVertShader = Shader::Create({.sourcePath = Path::Assets + "Shaders/Utilities/AABBsVS.hlsl", .entryPoint = "mainVS", .stage = PE_SHADER_STAGE_VERTEX, .defines = std::vector<Define>{}});
        m_passInfo->pFragShader = Shader::Create({.sourcePath = Path::Assets + "Shaders/Utilities/AABBsPS.hlsl", .entryPoint = "mainPS", .stage = PE_SHADER_STAGE_FRAGMENT, .defines = std::vector<Define>{}});
        m_passInfo->dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor, vk::DynamicState::eLineWidth, vk::DynamicState::eDepthTestEnable, vk::DynamicState::eDepthWriteEnable};
        m_passInfo->topology = vk::PrimitiveTopology::eLineList;
        m_passInfo->colorBlendAttachments = {BlendState::Default};
        m_passInfo->colorFormats = {pe::ToVkFormat(m_viewportRT->GetFormat())};
        m_passInfo->depthFormat = pe::ToVkFormat(m_depthRT->GetFormat());
        m_passInfo->Update();
    }

    void AabbsPass::Update()
    {
        if (Settings::Get<GlobalSettings>().draw_aabbs)
        {
            Scene &scene = GetGlobalSystem<RendererSystem>()->GetScene();
            if (scene.GetMeshCount() > 0)
            {
                uint32_t frame = RHII.GetFrameIndex();
                const auto &sets = m_passInfo->GetDescriptors(frame);
                Descriptor *setUniforms = sets[0];
                setUniforms->SetBuffer(0, scene.GetUniforms(frame));
                setUniforms->Update();
            }
        }
    }

    void AabbsPass::ExecutePass(CommandBuffer *cmd)
    {
        PE_ERROR_IF(m_scene == nullptr, "Scene was not set");
        if (m_scene->GetMeshCount() == 0)
            return;

        auto &gSettings = Settings::Get<GlobalSettings>();
        Camera &camera = *GetGlobalSystem<RendererSystem>()->GetScene().GetActiveCamera();

        struct PushConstants_AABB
        {
            vec2 projJitter;
            uint32_t meshIndex;
            uint32_t color;
        } constants{};
        constants.projJitter = camera.GetProjJitter();

        cmd->BeginDebugRegion("Aabbs");

        cmd->BeginPass(2, m_attachments.data(), "AabbsPass");
        cmd->BindIndexBuffer(m_scene->GetBuffer(), m_scene->GetAabbIndicesOffset());
        cmd->BindVertexBuffer(m_scene->GetBuffer(), m_scene->GetAabbVerticesOffset());
        cmd->SetViewport(0.f, 0.f, m_viewportRT->GetWidth_f(), m_viewportRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_viewportRT->GetWidth(), m_viewportRT->GetHeight());
        cmd->SetLineWidth(1.f + gSettings.render_scale * gSettings.render_scale);
        cmd->SetDepthTestEnable(gSettings.aabbs_depth_aware);
        cmd->SetDepthWriteEnable(false);
        cmd->BindPipeline(*m_passInfo);

        for (uint32_t i = 0; i < m_scene->GetNodeCount(); i++)
        {
            NodeId *node = m_scene->GetNodeId(i);
            const NodeRuntime &rt = m_scene->GetNodeRuntime(node);
            if (rt.gpuPending)
                continue;

            const auto &refs = m_scene->GetMeshRefs(node);
            if (refs.empty())
                continue;

            for (int meshIdx : refs)
            {
                if (meshIdx < 0)
                    continue;

                const Mesh &mesh = m_scene->GetMesh(meshIdx);

                constants.meshIndex = GetUboDataOffset(rt.dataOffset);
                constants.color = mesh.aabbColor;

                cmd->SetConstants(constants);
                cmd->PushConstants();
                cmd->DrawIndexed(24, 1, 0, static_cast<int>(mesh.aabbVertexOffset), 0);
            }
        }

        cmd->EndPass();

        cmd->EndDebugRegion();

        m_scene = nullptr;
    }

    void AabbsPass::Resize(uint32_t width, uint32_t height)
    {
        Init();
        UpdateDescriptorSets();
    }

    void AabbsPass::Destroy()
    {
    }
} // namespace pe
