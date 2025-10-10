#include "AabbsPass.h"
#include "ShadowPass.h"
#include "API/Shader.h"
#include "API/Command.h"
#include "API/RenderPass.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "Systems/RendererSystem.h"
#include "Systems/CameraSystem.h"
#include "Scene/Model.h"

namespace pe
{
    void AabbsPass::Init()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();

        m_viewportRT = rs->GetRenderTarget("viewport");
        m_depthRT = rs->GetDepthStencilTarget("depthStencil");
        m_geometry = nullptr;

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
        m_passInfo->ReflectDescriptors();
        m_passInfo->UpdateHash();
    }

    void AabbsPass::Draw(CommandBuffer *cmd)
    {
        PE_ERROR_IF(m_geometry == nullptr, "Geometry was not set");
        if (!m_geometry->HasDrawInfo())
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
                ModelGltf &model = *drawInfo.model;

                int node = drawInfo.node;
                int mesh = model.nodes[node].mesh;
                if (mesh < 0)
                    continue;

                int primitive = drawInfo.primitive;
                PrimitiveInfo &primitiveInfo = model.m_meshesInfo[mesh].primitivesInfo[primitive];

                constants.meshIndex = GetUboDataOffset(model.m_meshesInfo[mesh].dataOffset);
                constants.color = primitiveInfo.aabbColor;

                cmd->SetConstants(constants);
                cmd->PushConstants();
                cmd->DrawIndexed(24, 1, 0, static_cast<int>(primitiveInfo.aabbVertexOffset), 0);
            }
        };

        cmd->BeginDebugRegion("Aabbs");

        cmd->BeginPass(2, m_attachments.data(), "AabbsPass");
        cmd->BindIndexBuffer(m_geometry->GetBuffer(), m_geometry->GetAabbIndicesOffset());
        cmd->BindVertexBuffer(m_geometry->GetBuffer(), m_geometry->GetAabbVerticesOffset());
        cmd->SetViewport(0.f, 0.f, m_viewportRT->GetWidth_f(), m_viewportRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_viewportRT->GetWidth(), m_viewportRT->GetHeight());
        cmd->SetLineWidth(1.f + gSettings.render_scale * gSettings.render_scale);
        cmd->SetDepthTestEnable(gSettings.aabbs_depth_aware);
        cmd->SetDepthWriteEnable(false);
        cmd->BindPipeline(*m_passInfo);
        DrawFromInfos(m_geometry->GetDrawInfosOpaque());
        DrawFromInfos(m_geometry->GetDrawInfosAlphaCut());
        DrawFromInfos(m_geometry->GetDrawInfosAlphaBlend());
        cmd->EndPass();

        cmd->EndDebugRegion();

        m_geometry = nullptr;
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
