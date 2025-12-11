#include "AabbsPass.h"
#include "ShadowPass.h"
#include "API/Shader.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "Systems/RendererSystem.h"
#include "Systems/CameraSystem.h"
#include "Scene/Model.h"
#include "Scene/Scene.h"

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
        m_attachments[0].loadOp = vk::AttachmentLoadOp::eLoad;
        m_attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
        m_attachments[1] = {};
        m_attachments[1].image = m_depthRT;
        m_attachments[1].loadOp = vk::AttachmentLoadOp::eLoad;
        m_attachments[1].storeOp = vk::AttachmentStoreOp::eStore;
    }

    void AabbsPass::UpdatePassInfo()
    {
        m_passInfo->name = "AABBs_pipeline";
        m_passInfo->pVertShader = Shader::Create(Path::Executable + "Assets/Shaders/Utilities/AABBsVS.hlsl", vk::ShaderStageFlagBits::eVertex, "mainVS", std::vector<Define>{}, ShaderCodeType::HLSL);
        m_passInfo->pFragShader = Shader::Create(Path::Executable + "Assets/Shaders/Utilities/AABBsPS.hlsl", vk::ShaderStageFlagBits::eFragment, "mainPS", std::vector<Define>{}, ShaderCodeType::HLSL);
        m_passInfo->dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor, vk::DynamicState::eLineWidth, vk::DynamicState::eDepthTestEnable, vk::DynamicState::eDepthWriteEnable};
        m_passInfo->topology = vk::PrimitiveTopology::eLineList;
        m_passInfo->colorBlendAttachments = {PipelineColorBlendAttachmentState::Default};
        m_passInfo->colorFormats = {m_viewportRT->GetFormat()};
        m_passInfo->depthFormat = m_depthRT->GetFormat();
        m_passInfo->Update();
    }

    void AabbsPass::Draw(CommandBuffer *cmd)
    {
        PE_ERROR_IF(m_scene == nullptr, "Scene was not set");
        if (!m_scene->HasDrawInfo())
            return;

        auto &gSettings = Settings::Get<GlobalSettings>();
        Camera &camera = *GetGlobalSystem<CameraSystem>()->GetCamera(0);
        ShadowPass *shadows = GetGlobalComponent<ShadowPass>();

        struct PushConstants_AABB
        {
            vec2 projJitter;
            uint32_t meshIndex;
            uint32_t color;
        } constants{};
        constants.projJitter = camera.GetProjJitter();

        // Lambda to draw from drawInfos
        auto DrawFromInfos = [&](const auto &drawInfos)
        {
            for (auto &drawInfo : drawInfos)
            {
                Model &model = *drawInfo.model;

                int node = drawInfo.node;
                int mesh = model.GetNodeMesh(node);
                if (mesh < 0)
                    continue;

                MeshInfo &meshInfo = model.GetMeshInfos()[mesh];
                constants.meshIndex = GetUboDataOffset(meshInfo.dataOffset);
                constants.color = meshInfo.aabbColor;

                cmd->SetConstants(constants);
                cmd->PushConstants();
                cmd->DrawIndexed(24, 1, 0, static_cast<int>(meshInfo.aabbVertexOffset), 0);
            }
        };

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
        DrawFromInfos(m_scene->GetDrawInfosOpaque());
        DrawFromInfos(m_scene->GetDrawInfosAlphaCut());
        DrawFromInfos(m_scene->GetDrawInfosAlphaBlend());
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
}
