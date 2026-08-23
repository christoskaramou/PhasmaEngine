#include "MotionBlurPass.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/RenderGraph.h"
#include "API/Shader.h"
#include "Camera/Camera.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Render/SceneRendererHost.h"

namespace pe
{
    void MotionBlurPass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();

        m_displayRT = rs->GetRenderTarget("display");
        m_velocityRT = rs->GetRenderTarget("velocity");
        m_depth = rs->GetDepthStencilTarget("depthStencil");

        m_frameImage = rs->CreateFSSampledImage(false);

        m_attachments.resize(1);
        m_attachments[0] = {};
        m_attachments[0].image = m_displayRT;
    }

    void MotionBlurPass::UpdatePassInfo()
    {
        m_passInfo->name = "motionBlur_pipeline";
        m_passInfo->pVertShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Common/Quad.hlsl", .entryPoint = "mainVS", .stage = PE_SHADER_STAGE_VERTEX, .defines = std::vector<Define>{}});
        m_passInfo->pFragShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/MotionBlur/MotionBlurPS.hlsl", .entryPoint = "mainPS", .stage = PE_SHADER_STAGE_FRAGMENT, .defines = std::vector<Define>{}});
        m_passInfo->dynamicStates = {PE_DYNAMIC_STATE_VIEWPORT, PE_DYNAMIC_STATE_SCISSOR};
        m_passInfo->cullMode = PE_CULL_MODE_NONE;
        m_passInfo->colorBlendAttachments = {BlendState::Default};
        m_passInfo->colorFormats = {m_displayRT->GetFormat()};
        m_passInfo->depthTestEnable = false;
        m_passInfo->depthWriteEnable = false;
        m_passInfo->stencilTestEnable = false;
        m_passInfo->Update();
    }

    void MotionBlurPass::CreateUniforms(CommandBuffer *cmd)
    {
        UpdateDescriptorSets();
    }

    void MotionBlurPass::UpdateDescriptorSets()
    {
        if (!m_frameImage || !m_depth || !m_velocityRT)
            return;

        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); ++i)
        {
            auto *DSet = m_passInfo->GetDescriptors(i)[0];
            DSet->SetImageView(0, m_frameImage->GetSRV(), m_frameImage->GetSampler());
            DSet->SetImageView(1, m_depth->GetSRV(), m_depth->GetSampler());
            DSet->SetImageView(2, m_velocityRT->GetSRV(), m_velocityRT->GetSampler());
            DSet->Update();
        }
    }

    void MotionBlurPass::DeclareInputs(RGBuilder &builder)
    {
        builder.Read(m_velocityRT);
        builder.Read(m_depth);
    }

    void MotionBlurPass::ExecutePass(CommandBuffer *cmd)
    {
        auto &gSettings = ActivePostProcessProfile();
        Camera *camera = GetActiveScene()->GetActiveCamera();

        ImageBarrierInfo frameBarrier{};
        frameBarrier.image = m_frameImage;
        frameBarrier.layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        frameBarrier.stageFlags = PE_STAGE_FRAGMENT_SHADER;
        frameBarrier.accessMask = PE_ACCESS_SHADER_SAMPLED_READ;

        cmd->BeginDebugRegion("MotionBlurPass");
        cmd->CopyImage(m_displayRT, m_frameImage);
        cmd->ImageBarrier(frameBarrier);

        cmd->BeginPass(1, m_attachments.data(), "MotionBlur");
        cmd->BindPipeline(*m_passInfo);
        cmd->SetViewport(0.f, 0.f, m_displayRT->GetWidth_f(), m_displayRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_displayRT->GetWidth(), m_displayRT->GetHeight());
        cmd->SetConstantAt(0, 1.0f / (static_cast<float>(FrameTimer::Instance().GetDelta()) + FLT_EPSILON));
        cmd->SetConstantAt(1, gSettings.motion_blur_strength);
        cmd->SetConstantAt(2, camera->GetProjJitter());
        cmd->SetConstantAt(4, gSettings.motion_blur_samples);
        cmd->SetConstantAt(5, ActivePostProcessBlend().motion_blur);
        cmd->PushConstants();
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();

        cmd->EndDebugRegion();
    }

    void MotionBlurPass::Resize(uint32_t width, uint32_t height)
    {
        Image::Destroy(m_frameImage);
        Init();
        if (m_velocityRT)
            UpdateDescriptorSets();
    }

    void MotionBlurPass::Destroy()
    {
        Image::Destroy(m_frameImage);
    }
} // namespace pe
