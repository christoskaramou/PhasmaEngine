#include "Renderer/RenderPasses/FXAAPass.h"
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
#include "Systems/RendererSystem.h"

namespace pe
{
    FXAAPass::FXAAPass()
    {
    }

    FXAAPass::~FXAAPass()
    {
    }

    void FXAAPass::Init()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();

        m_viewportRT = rs->GetRenderTarget("viewport");
        m_frameImage = rs->CreateFSSampledImage(true);

        m_attachments.resize(1);
        m_attachments[0] = {};
        m_attachments[0].image = m_viewportRT;
    }

    void FXAAPass::UpdatePassInfo()
    {
        m_passInfo->name = "fxaa_pipeline";
        m_passInfo->pVertShader = Shader::Create("Shaders/Common/Quad.hlsl", ShaderStage::VertexBit);
        m_passInfo->pFragShader = Shader::Create("Shaders/FXAA/FXAAPS.hlsl", ShaderStage::FragmentBit);
        m_passInfo->dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        m_passInfo->cullMode = CullMode::Back;
        m_passInfo->colorBlendAttachments = {PipelineColorBlendAttachmentState::Default};
        m_passInfo->colorFormats = {m_viewportRT->GetFormat()};
        m_passInfo->ReflectDescriptors();
        m_passInfo->UpdateHash();
    }

    void FXAAPass::CreateUniforms(CommandBuffer *cmd)
    {
        UpdateDescriptorSets();
    }

    void FXAAPass::UpdateDescriptorSets()
    {
        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
        {
            auto *DSet = m_passInfo->GetDescriptors(i)[0];
            DSet->SetImageView(0, m_frameImage->GetSRV(), m_frameImage->GetSampler()->ApiHandle());
            DSet->Update();
        }
    }

    void FXAAPass::Update(Camera *camera)
    {
    }

    void FXAAPass::Draw(CommandBuffer *cmd)
    {
        std::vector<ImageBarrierInfo> barriers(2);

        barriers[0].image = m_frameImage;
        barriers[0].layout = ImageLayout::ShaderReadOnly;
        barriers[0].stageFlags = PipelineStage::FragmentShaderBit;
        barriers[0].accessMask = Access::ShaderReadBit;
        
        barriers[1].image = m_viewportRT;
        barriers[1].layout = ImageLayout::Attachment;
        barriers[1].stageFlags = PipelineStage::ColorAttachmentOutputBit;
        barriers[1].accessMask = Access::ColorAttachmentWriteBit;

        cmd->BeginDebugRegion("FXAAPass");
        cmd->CopyImage(m_viewportRT, m_frameImage);
        cmd->ImageBarriers(barriers);
        
        cmd->BeginPass(1, m_attachments.data(), "FXAA");
        cmd->BindPipeline(*m_passInfo);
        cmd->SetViewport(0.f, 0.f, m_viewportRT->GetWidth_f(), m_viewportRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_viewportRT->GetWidth(), m_viewportRT->GetHeight());
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
        cmd->EndDebugRegion();
    }

    void FXAAPass::Resize(uint32_t width, uint32_t height)
    {
        Image::Destroy(m_frameImage);
        Init();
        UpdateDescriptorSets();
    }

    void FXAAPass::Destroy()
    {
        Image::Destroy(m_frameImage);
    }
}
