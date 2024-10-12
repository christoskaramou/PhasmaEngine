#include "Renderer/RenderPasses/MotionBlurPass.h"
#include "Shader/Shader.h"
#include "Renderer/Command.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Image.h"
#include "Renderer/Pipeline.h"
#include "Systems/RendererSystem.h"
#include "Systems/CameraSystem.h"

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
        m_attachments[0].initialLayout = ImageLayout::Attachment;
        m_attachments[0].finalLayout = ImageLayout::ShaderReadOnly;
    }

    void MotionBlurPass::UpdatePassInfo()
    {
        m_passInfo->name = "motionBlur_pipeline";
        m_passInfo->pVertShader = Shader::Create("Shaders/Common/Quad.hlsl", ShaderStage::VertexBit);
        m_passInfo->pFragShader = Shader::Create("Shaders/MotionBlur/MotionBlurPS.hlsl", ShaderStage::FragmentBit);
        m_passInfo->dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        m_passInfo->cullMode = CullMode::Back;
        m_passInfo->colorBlendAttachments = {PipelineColorBlendAttachmentState::Default};
        m_passInfo->colorFormats = {m_displayRT->GetFormat()};
        m_passInfo->ReflectDescriptors();
        m_passInfo->UpdateHash();
    }

    void MotionBlurPass::CreateUniforms(CommandBuffer *cmd)
    {
        UpdateDescriptorSets();
    }

    void MotionBlurPass::UpdateDescriptorSets()
    {
        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; ++i)
        {
            auto *DSet = m_passInfo->GetDescriptors(i)[0];
            DSet->SetImageView(0, m_frameImage->GetSRV(), m_frameImage->GetSampler()->ApiHandle());
            DSet->SetImageView(1, m_depth->GetSRV(), m_depth->GetSampler()->ApiHandle());
            DSet->SetImageView(2, m_velocityRT->GetSRV(), m_velocityRT->GetSampler()->ApiHandle());
            DSet->Update();
        }
    }

    void MotionBlurPass::Update(Camera *camera)
    {
    }

    void MotionBlurPass::Draw(CommandBuffer *cmd)
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        Camera *camera = GetGlobalSystem<CameraSystem>()->GetCamera(0);

        ImageBarrierInfo barrier{};
        barrier.layout = ImageLayout::ShaderReadOnly;
        barrier.stageFlags = PipelineStage::FragmentShaderBit;
        barrier.accessMask = Access::ShaderReadBit;

        std::vector<ImageBarrierInfo> barriers;
        barriers.reserve(4);
        barrier.image = m_frameImage;
        barriers.push_back(barrier);
        barrier.image = m_velocityRT;
        barriers.push_back(barrier);
        barrier.image = m_depth;
        barriers.push_back(barrier);
        barrier.image = m_displayRT;
        barrier.layout = ImageLayout::Attachment;
        barrier.stageFlags = PipelineStage::ColorAttachmentOutputBit;
        barrier.accessMask = Access::ColorAttachmentWriteBit;
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
}
