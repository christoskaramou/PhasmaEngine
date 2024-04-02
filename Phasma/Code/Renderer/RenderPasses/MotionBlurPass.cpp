#include "Renderer/RenderPasses/MotionBlurPass.h"
#include "Renderer/Surface.h"
#include "Renderer/Swapchain.h"
#include "GUI/GUI.h"
#include "Shader/Shader.h"
#include "Renderer/RHI.h"
#include "Renderer/Queue.h"
#include "Renderer/Command.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/Image.h"
#include "Renderer/Pipeline.h"
#include "Renderer/Buffer.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    MotionBlurPass::MotionBlurPass()
    {
    }

    MotionBlurPass::~MotionBlurPass()
    {
    }

    void MotionBlurPass::Init()
    {
        m_renderQueue = RHII.GetRenderQueue();
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();

        m_displayRT = rs->GetRenderTarget("display");
        m_velocityRT = rs->GetRenderTarget("velocity");
        m_depth = rs->GetDepthStencilTarget("depthStencil");

        m_frameImage = rs->CreateFSSampledImage(false);

        m_attachment = {};
        m_attachment.image = m_displayRT;
        m_attachment.initialLayout = ImageLayout::Attachment;
        m_attachment.finalLayout = ImageLayout::ShaderReadOnly;
    }

    void MotionBlurPass::UpdatePassInfo()
    {
        PassInfo &info = m_passInfo;

        info.name = "motionBlur_pipeline";
        info.pVertShader = Shader::Create("Shaders/Common/Quad.hlsl", ShaderStage::VertexBit);
        info.pFragShader = Shader::Create("Shaders/MotionBlur/MotionBlurPS.hlsl", ShaderStage::FragmentBit);
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {m_displayRT->GetBlendAttachment()};
        info.colorFormats = m_displayRT->GetFormatPtr();
        info.UpdateHash();
    }

    void MotionBlurPass::CreateUniforms(CommandBuffer *cmd)
    {
        UpdateDescriptorSets();
    }

    void MotionBlurPass::UpdateDescriptorSets()
    {
        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; ++i)
        {
            auto *DSet = m_passInfo.GetDescriptors(i)[0];
            DSet->SetImageView(0, m_frameImage->GetSRV(), m_frameImage->GetSampler()->Handle());
            DSet->SetImageView(1, m_depth->GetSRV(), m_depth->GetSampler()->Handle());
            DSet->SetImageView(2, m_velocityRT->GetSRV(), m_velocityRT->GetSampler()->Handle());
            DSet->Update();
        }
    }

    void MotionBlurPass::Update(Camera *camera)
    {
    }

    CommandBuffer *MotionBlurPass::Draw()
    {
        CommandBuffer *cmd = CommandBuffer::GetFree(m_renderQueue);
        cmd->Begin();

        cmd->BeginDebugRegion("MotionBlurPass");

        Camera *camera = GetGlobalSystem<CameraSystem>()->GetCamera(0);

        cmd->CopyImage(m_displayRT, m_frameImage);

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
        cmd->ImageBarriers(barriers);

        auto &gSettings = Settings::Get<GlobalSettings>();
        
        cmd->BeginPass({m_attachment}, "MotionBlur");
        cmd->BindPipeline(m_passInfo);
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

        cmd->End();
        return cmd;
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
