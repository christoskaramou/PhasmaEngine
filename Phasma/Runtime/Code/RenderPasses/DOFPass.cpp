#include "DOFPass.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/RenderGraph.h"
#include "API/Shader.h"
#include "Base/Settings.h"
#include "Render/SceneRendererHost.h"

namespace pe
{
    void DOFPass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();

        m_displayRT = rs->GetRenderTarget("display");
        m_depth = rs->GetDepthStencilTarget("depthStencil");
        m_frameImage = rs->CreateFSSampledImage(false);

        m_attachments.resize(1);
        m_attachments[0] = {};
        m_attachments[0].image = m_displayRT;
    }

    void DOFPass::UpdatePassInfo()
    {
        m_passInfo->name = "dof_pipeline";
        m_passInfo->pVertShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Common/Quad.hlsl", .entryPoint = "mainVS", .stage = PE_SHADER_STAGE_VERTEX, .defines = std::vector<Define>{}});
        m_passInfo->pFragShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/DepthOfField/DOFPS.hlsl", .entryPoint = "mainPS", .stage = PE_SHADER_STAGE_FRAGMENT, .defines = std::vector<Define>{}});
        m_passInfo->dynamicStates = {PE_DYNAMIC_STATE_VIEWPORT, PE_DYNAMIC_STATE_SCISSOR};
        m_passInfo->cullMode = PE_CULL_MODE_NONE;
        m_passInfo->colorBlendAttachments = {BlendState::Default};
        m_passInfo->colorFormats = {m_displayRT->GetFormat()};
        m_passInfo->depthTestEnable = false;
        m_passInfo->depthWriteEnable = false;
        m_passInfo->stencilTestEnable = false;
        m_passInfo->Update();
    }

    void DOFPass::CreateUniforms(CommandBuffer *cmd)
    {
        UpdateDescriptorSets();
    }

    void DOFPass::UpdateDescriptorSets()
    {
        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
        {
            auto *DSet = m_passInfo->GetDescriptors(i)[0];
            DSet->SetImageView(0, m_frameImage->GetSRV(), m_frameImage->GetSampler());
            DSet->SetImageView(1, m_depth->GetSRV(), m_depth->GetSampler());
            DSet->Update();
        }
    }

    void DOFPass::DeclareInputs(RGBuilder &builder)
    {
        builder.Read(m_depth);
    }

    void DOFPass::ExecutePass(CommandBuffer *cmd)
    {
        auto &gSettings = ActivePostProcessProfile();

        ImageBarrierInfo frameBarrier{};
        frameBarrier.image = m_frameImage;
        frameBarrier.layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        frameBarrier.stageFlags = PE_STAGE_FRAGMENT_SHADER;
        frameBarrier.accessMask = PE_ACCESS_SHADER_READ;

        cmd->BeginDebugRegion("DOFPass");
        cmd->CopyImage(m_displayRT, m_frameImage); // Copy RT to image
        cmd->ImageBarrier(frameBarrier);

        struct DOFPushConstants
        {
            float focusScale;
            float blurRange;
            float blend;
        };
        DOFPushConstants pc{};
        pc.focusScale = gSettings.dof_focus_scale;
        pc.blurRange = gSettings.dof_blur_range;
        pc.blend = ActivePostProcessBlend().dof;

        cmd->BeginPass(1, m_attachments.data(), "DOF");
        cmd->BindPipeline(*m_passInfo);
        cmd->SetViewport(0.f, 0.f, m_displayRT->GetWidth_f(), m_displayRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_displayRT->GetWidth(), m_displayRT->GetHeight());
        cmd->SetConstants(pc);
        cmd->PushConstants();
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();

        cmd->EndDebugRegion();
    }

    void DOFPass::Resize(uint32_t width, uint32_t height)
    {
        Image::Destroy(m_frameImage);
        Init();
        UpdateDescriptorSets();
    }

    void DOFPass::Destroy()
    {
        Image::Destroy(m_frameImage);
    }
} // namespace pe
