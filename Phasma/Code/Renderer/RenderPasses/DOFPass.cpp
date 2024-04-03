#include "Renderer/RenderPasses/DOFPass.h"
#include "GUI/GUI.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Surface.h"
#include "Shader/Shader.h"
#include "Renderer/RHI.h"
#include "Renderer/Queue.h"
#include "Renderer/Command.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/Image.h"
#include "Renderer/Pipeline.h"
#include "Systems/CameraSystem.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    DOFPass::DOFPass()
    {
    }

    DOFPass::~DOFPass()
    {
    }

    void DOFPass::Init()
    {
        m_renderQueue = RHII.GetRenderQueue();
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();

        m_displayRT = rs->GetRenderTarget("display");
        m_depth = rs->GetDepthStencilTarget("depthStencil");
        m_frameImage = rs->CreateFSSampledImage(false);

        m_attachment = {};
        m_attachment.image = m_displayRT;
        m_attachment.initialLayout = ImageLayout::Attachment;
        m_attachment.finalLayout = ImageLayout::ShaderReadOnly;
    }

    void DOFPass::UpdatePassInfo()
    {
        PassInfo &info = m_passInfo;

        info.name = "dof_pipeline";
        info.pVertShader = Shader::Create("Shaders/Common/Quad.hlsl", ShaderStage::VertexBit);
        info.pFragShader = Shader::Create("Shaders/DepthOfField/DOFPS.hlsl", ShaderStage::FragmentBit);
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {m_displayRT->GetBlendAttachment()};
        info.colorFormats = m_displayRT->GetFormatPtr();
        info.UpdateHash();
    }

    void DOFPass::CreateUniforms(CommandBuffer *cmd)
    {
        UpdateDescriptorSets();
    }

    void DOFPass::UpdateDescriptorSets()
    {
        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
        {
            auto *DSet = m_passInfo.GetDescriptors(i)[0];
            DSet->SetImageView(0, m_frameImage->GetSRV(), m_frameImage->GetSampler()->ApiHandle());
            DSet->SetImageView(1, m_depth->GetSRV(), m_depth->GetSampler()->ApiHandle());
            DSet->Update();
        }
    }

    void DOFPass::Update(Camera *camera)
    {
    }

    CommandBuffer *DOFPass::Draw()
    {
        CommandBuffer *cmd = CommandBuffer::GetFree(m_renderQueue);
        cmd->Begin();

        cmd->BeginDebugRegion("DOFPass");

        cmd->CopyImage(m_displayRT, m_frameImage); // Copy RT to image

        std::vector<ImageBarrierInfo> barriers(3);
        barriers[0].image = m_frameImage;
        barriers[0].layout = ImageLayout::ShaderReadOnly;
        barriers[0].stageFlags = PipelineStage::FragmentShaderBit;
        barriers[0].accessMask = Access::ShaderReadBit;
        barriers[1].image = m_depth;
        barriers[1].layout = ImageLayout::ShaderReadOnly;
        barriers[1].stageFlags = PipelineStage::FragmentShaderBit;
        barriers[1].accessMask = Access::ShaderReadBit;
        barriers[2].image = m_displayRT;
        barriers[2].layout = ImageLayout::Attachment;
        barriers[2].stageFlags = PipelineStage::ColorAttachmentOutputBit;
        barriers[2].accessMask = Access::ColorAttachmentWriteBit;

        auto &gSettings = Settings::Get<GlobalSettings>();
        cmd->ImageBarriers(barriers);
        cmd->BeginPass({m_attachment}, "DOF");
        cmd->BindPipeline(m_passInfo);
        cmd->SetViewport(0.f, 0.f, m_displayRT->GetWidth_f(), m_displayRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_displayRT->GetWidth(), m_displayRT->GetHeight());
        cmd->SetConstantAt(0, gSettings.dof_focus_scale);
        cmd->SetConstantAt(1, gSettings.dof_blur_range);
        cmd->PushConstants();
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();

        cmd->EndDebugRegion();

        cmd->End();
        return cmd;
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
}
