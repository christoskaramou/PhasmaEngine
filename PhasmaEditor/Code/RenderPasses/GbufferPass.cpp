#include "GbufferPass.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/Shader.h"
#include "Camera/Camera.h"
#include "Scene/Scene.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    void GbufferOpaquePass::Init()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();

        m_srmRT = rs->GetRenderTarget("srm"); // Specular Roughness Metallic
        m_normalRT = rs->GetRenderTarget("normal");
        m_albedoRT = rs->GetRenderTarget("albedo");
        m_velocityRT = rs->GetRenderTarget("velocity");
        m_emissiveRT = rs->GetRenderTarget("emissive");
        m_viewportRT = rs->GetRenderTarget("viewport");
        m_transparencyRT = rs->GetRenderTarget("transparency");
        m_depthStencilRT = rs->GetDepthStencilTarget("depthStencil");

        m_attachments.resize(7);
        for (int i = 0; i < 7; i++)
        {
            m_attachments[i] = {};
        }
        m_attachments[0].image = m_normalRT;
        m_attachments[1].image = m_albedoRT;
        m_attachments[2].image = m_srmRT;
        m_attachments[3].image = m_velocityRT;
        m_attachments[4].image = m_emissiveRT;
        m_attachments[5].image = m_transparencyRT;

        m_attachments[6].image = m_depthStencilRT;
        m_attachments[6].loadOp = vk::AttachmentLoadOp::eLoad;

        m_scene = nullptr;
    }

    void GbufferOpaquePass::UpdatePassInfo()
    {
        std::vector<vk::Format> colorformats{
            m_normalRT->GetFormat(),
            m_albedoRT->GetFormat(),
            m_srmRT->GetFormat(),
            m_velocityRT->GetFormat(),
            m_emissiveRT->GetFormat(),
            m_transparencyRT->GetFormat()};

        vk::Format depthFormat = RHII.GetDepthFormat();

        m_passInfo->name = "gbuffer_opaque_pipeline";
        m_passInfo->pVertShader = Shader::Create(Path::Assets + "Shaders/Gbuffer/GBufferVS.hlsl", vk::ShaderStageFlagBits::eVertex, "mainVS", std::vector<Define>{}, ShaderCodeType::HLSL);
        m_passInfo->pFragShader = Shader::Create(Path::Assets + "Shaders/Gbuffer/GBufferPS.hlsl", vk::ShaderStageFlagBits::eFragment, "mainPS", std::vector<Define>{}, ShaderCodeType::HLSL);
        m_passInfo->dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        m_passInfo->cullMode = vk::CullModeFlagBits::eFront;
        m_passInfo->blendEnable = false;
        m_passInfo->colorBlendAttachments = {
            PipelineColorBlendAttachmentState::Default,
            PipelineColorBlendAttachmentState::Default,
            PipelineColorBlendAttachmentState::Default,
            PipelineColorBlendAttachmentState::Default,
            PipelineColorBlendAttachmentState::Default,
            PipelineColorBlendAttachmentState::Default};
        m_passInfo->colorFormats = colorformats;
        m_passInfo->depthFormat = depthFormat;
        m_passInfo->depthCompareOp = vk::CompareOp::eEqual; // we use depth prepass for opaque
        m_passInfo->depthTestEnable = true;
        m_passInfo->depthWriteEnable = false;
        m_passInfo->Update();
    }

    void GbufferOpaquePass::PassBarriers(CommandBuffer *cmd)
    {
    }

    void GbufferOpaquePass::ExecutePass(CommandBuffer *cmd)
    {
        PE_ERROR_IF(m_scene == nullptr, "Scene was not set");

        Camera *camera = GetGlobalSystem<RendererSystem>()->GetScene().GetCamera(0);

        if (!m_scene->HasDrawInfo())
        {
            // Just clear the render targets
            ClearRenderTargets(cmd);
        }
        else
        {
            if (m_scene->HasOpaqueDrawInfo())
            {
                PushConstants_GBuffer pushConstants{};
                pushConstants.jointsCount = 0u;
                pushConstants.projJitter = camera->GetProjJitter();
                pushConstants.prevProjJitter = camera->GetPrevProjJitter();
                pushConstants.transparentPass = 0u;

                uint32_t frame = RHII.GetFrameIndex();
                size_t offset = 0;
                uint32_t count = static_cast<uint32_t>(m_scene->GetDrawInfosOpaque().size());

                cmd->BeginPass(7, m_attachments.data(), "GbufferOpaquePass");
                cmd->BindIndexBuffer(m_scene->GetBuffer(), 0);
                cmd->BindVertexBuffer(m_scene->GetBuffer(), m_scene->GetVerticesOffset());
                cmd->SetViewport(0.f, 0.f, m_depthStencilRT->GetWidth_f(), m_depthStencilRT->GetHeight_f());
                cmd->SetScissor(0, 0, m_depthStencilRT->GetWidth(), m_depthStencilRT->GetHeight());
                cmd->BindPipeline(*m_passInfo);
                cmd->SetConstants(pushConstants);
                cmd->PushConstants();
                cmd->DrawIndexedIndirect(m_scene->GetIndirect(frame), offset, count);
                cmd->EndPass();
            }
        }

        m_scene = nullptr;
    }

    void GbufferOpaquePass::Resize(uint32_t width, uint32_t height)
    {
        Init();
    }

    void GbufferOpaquePass::ClearRenderTargets(CommandBuffer *cmd)
    {
        std::vector<Image *> colorTargets{m_normalRT, m_albedoRT, m_srmRT, m_velocityRT, m_emissiveRT, m_viewportRT, m_transparencyRT};
        cmd->ClearColors(colorTargets);
    }

    void GbufferOpaquePass::ClearDepthStencil(CommandBuffer *cmd)
    {
        cmd->ClearDepthStencils({m_depthStencilRT});
    }

    void GbufferOpaquePass::Destroy()
    {
    }

    void GbufferTransparentPass::Init()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();

        m_srmRT = rs->GetRenderTarget("srm"); // Specular Roughness Metallic
        m_normalRT = rs->GetRenderTarget("normal");
        m_albedoRT = rs->GetRenderTarget("albedo");
        m_velocityRT = rs->GetRenderTarget("velocity");
        m_emissiveRT = rs->GetRenderTarget("emissive");
        m_viewportRT = rs->GetRenderTarget("viewport");
        m_transparencyRT = rs->GetRenderTarget("transparency");
        m_depthStencilRT = rs->GetDepthStencilTarget("depthStencil");

        m_attachments.resize(7);
        for (int i = 0; i < 7; i++)
        {
            m_attachments[i] = {};
            m_attachments[i].loadOp = vk::AttachmentLoadOp::eLoad;
        }
        m_attachments[0].image = m_normalRT;
        m_attachments[1].image = m_albedoRT;
        m_attachments[2].image = m_srmRT;
        m_attachments[3].image = m_velocityRT;
        m_attachments[4].image = m_emissiveRT;
        m_attachments[5].image = m_transparencyRT;
        m_attachments[6].image = m_depthStencilRT;

        m_scene = nullptr;
    }

    void GbufferTransparentPass::UpdatePassInfo()
    {
        std::vector<vk::Format> colorformats{
            m_normalRT->GetFormat(),
            m_albedoRT->GetFormat(),
            m_srmRT->GetFormat(),
            m_velocityRT->GetFormat(),
            m_emissiveRT->GetFormat(),
            m_transparencyRT->GetFormat()};

        vk::Format depthFormat = RHII.GetDepthFormat();

        m_passInfo->name = "gbuffer_transparent_pipeline";
        m_passInfo->pVertShader = Shader::Create(Path::Assets + "Shaders/Gbuffer/GBufferVS.hlsl", vk::ShaderStageFlagBits::eVertex, "mainVS", std::vector<Define>{}, ShaderCodeType::HLSL);
        m_passInfo->pFragShader = Shader::Create(Path::Assets + "Shaders/Gbuffer/GBufferPS.hlsl", vk::ShaderStageFlagBits::eFragment, "mainPS", std::vector<Define>{}, ShaderCodeType::HLSL);
        m_passInfo->dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        m_passInfo->cullMode = vk::CullModeFlagBits::eFront;
        m_passInfo->blendEnable = true;
        m_passInfo->colorBlendAttachments = {
            PipelineColorBlendAttachmentState::Default,
            PipelineColorBlendAttachmentState::Default,
            PipelineColorBlendAttachmentState::Default,
            PipelineColorBlendAttachmentState::Default,
            PipelineColorBlendAttachmentState::Default,
            PipelineColorBlendAttachmentState::TransparencyBlend};
        m_passInfo->colorFormats = colorformats;
        m_passInfo->depthFormat = depthFormat;
        m_passInfo->depthCompareOp = Settings::Get<GlobalSettings>().reverse_depth ? vk::CompareOp::eGreaterOrEqual : vk::CompareOp::eLessOrEqual;
        m_passInfo->depthTestEnable = true;
        m_passInfo->depthWriteEnable = true;
        m_passInfo->Update();
    }

    void GbufferTransparentPass::PassBarriers(CommandBuffer *cmd)
    {
    }

    void GbufferTransparentPass::ExecutePass(CommandBuffer *cmd)
    {
        PE_ERROR_IF(m_scene == nullptr, "Scene was not set");

        Camera *camera = GetGlobalSystem<RendererSystem>()->GetScene().GetCamera(0);

        if (m_scene->HasAlphaDrawInfo())
        {
            PushConstants_GBuffer pushConstants{};
            pushConstants.jointsCount = 0u;
            pushConstants.projJitter = camera->GetProjJitter();
            pushConstants.prevProjJitter = camera->GetPrevProjJitter();
            pushConstants.transparentPass = 1u;

            uint32_t frame = RHII.GetFrameIndex();
            size_t offset = m_scene->GetDrawInfosOpaque().size() * sizeof(vk::DrawIndexedIndirectCommand);
            uint32_t count = static_cast<uint32_t>(m_scene->GetDrawInfosAlphaCut().size() + m_scene->GetDrawInfosAlphaBlend().size());

            cmd->BeginPass(7, m_attachments.data(), "GbufferTransparentPass");
            cmd->BindIndexBuffer(m_scene->GetBuffer(), 0);
            cmd->BindVertexBuffer(m_scene->GetBuffer(), m_scene->GetVerticesOffset());
            cmd->SetViewport(0.f, 0.f, m_depthStencilRT->GetWidth_f(), m_depthStencilRT->GetHeight_f());
            cmd->SetScissor(0, 0, m_depthStencilRT->GetWidth(), m_depthStencilRT->GetHeight());
            cmd->BindPipeline(*m_passInfo);
            cmd->SetConstants(pushConstants);
            cmd->PushConstants();
            cmd->DrawIndexedIndirect(m_scene->GetIndirect(frame), offset, count);
            cmd->EndPass();
        }

        m_scene = nullptr;
    }

    void GbufferTransparentPass::Resize(uint32_t width, uint32_t height)
    {
        Init();
    }

    void GbufferTransparentPass::ClearRenderTargets(CommandBuffer *cmd)
    {
        std::vector<Image *> colorTargets{m_normalRT, m_albedoRT, m_srmRT, m_velocityRT, m_emissiveRT, m_viewportRT, m_transparencyRT};
        cmd->ClearColors(colorTargets);
    }

    void GbufferTransparentPass::ClearDepthStencil(CommandBuffer *cmd)
    {
        cmd->ClearDepthStencils({m_depthStencilRT});
    }

    void GbufferTransparentPass::Destroy()
    {
        Buffer::Destroy(m_constants);
    }
} // namespace pe
