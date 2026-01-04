#include "MotionBlurPass.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/Shader.h"
#include "Camera/Camera.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    void MotionBlurPass::Init()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();

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
        m_passInfo->pVertShader = Shader::Create(Path::Assets + "Shaders/Common/Quad.hlsl", vk::ShaderStageFlagBits::eVertex, "mainVS", std::vector<Define>{}, ShaderCodeType::HLSL);
        m_passInfo->pFragShader = Shader::Create(Path::Assets + "Shaders/MotionBlur/MotionBlurPS.hlsl", vk::ShaderStageFlagBits::eFragment, "mainPS", std::vector<Define>{}, ShaderCodeType::HLSL);
        m_passInfo->dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        m_passInfo->cullMode = vk::CullModeFlagBits::eBack;
        m_passInfo->colorBlendAttachments = {PipelineColorBlendAttachmentState::Default};
        m_passInfo->colorFormats = {m_displayRT->GetFormat()};
        m_passInfo->Update();
    }

    void MotionBlurPass::CreateUniforms(CommandBuffer *cmd)
    {
        UpdateDescriptorSets();
    }

    void MotionBlurPass::UpdateDescriptorSets()
    {
        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); ++i)
        {
            auto *DSet = m_passInfo->GetDescriptors(i)[0];
            DSet->SetImageView(0, m_frameImage->GetSRV(), m_frameImage->GetSampler()->ApiHandle());
            DSet->SetImageView(1, m_depth->GetSRV(), m_depth->GetSampler()->ApiHandle());
            DSet->SetImageView(2, m_velocityRT->GetSRV(), m_velocityRT->GetSampler()->ApiHandle());
            DSet->Update();
        }
    }

    void MotionBlurPass::Draw(CommandBuffer *cmd)
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        Camera *camera = GetGlobalSystem<RendererSystem>()->GetScene().GetCamera(0);

        ImageBarrierInfo barrier{};
        barrier.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.stageFlags = vk::PipelineStageFlagBits2::eFragmentShader;
        barrier.accessMask = vk::AccessFlagBits2::eShaderRead;

        std::vector<ImageBarrierInfo> barriers;
        barriers.reserve(4);
        barrier.image = m_frameImage;
        barriers.push_back(barrier);
        barrier.image = m_velocityRT;
        barriers.push_back(barrier);
        barrier.image = m_depth;
        barriers.push_back(barrier);

        cmd->BeginDebugRegion("MotionBlurPass");
        cmd->CopyImage(m_displayRT, m_frameImage);
        cmd->ImageBarriers(barriers);

        cmd->BeginPass(1, m_attachments.data(), "MotionBlur");
        cmd->BindPipeline(*m_passInfo);
        cmd->SetViewport(0.f, 0.f, m_displayRT->GetWidth_f(), m_displayRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_displayRT->GetWidth(), m_displayRT->GetHeight());
        cmd->SetConstantAt(0, 1.0f / static_cast<float>(FrameTimer::Instance().GetDelta()));
        cmd->SetConstantAt(1, gSettings.motion_blur_strength);
        cmd->SetConstantAt(2, camera->GetProjJitter());
        cmd->SetConstantAt(4, gSettings.motion_blur_samples);
        cmd->PushConstants();
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();

        cmd->EndDebugRegion();
    }

    void MotionBlurPass::Resize(uint32_t width, uint32_t height)
    {
        Image::Destroy(m_frameImage);
        Init();
        UpdateDescriptorSets();
    }

    void MotionBlurPass::Destroy()
    {
        Image::Destroy(m_frameImage);
    }
} // namespace pe
