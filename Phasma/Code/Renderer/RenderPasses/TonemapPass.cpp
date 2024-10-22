#include "Renderer/RenderPasses/TonemapPass.h"
#include "Shader/Shader.h"
#include "Renderer/Command.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Image.h"
#include "Renderer/Pipeline.h"
#include "Systems/RendererSystem.h"
#include "Renderer/RenderPass.h"


namespace pe
{
    void TonemapPass::Init()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();

        m_displayRT = rs->GetRenderTarget("display");
        m_frameImage = rs->CreateFSSampledImage(false);

        m_attachments.resize(1);
        m_attachments[0] = {};
        m_attachments[0].image = m_displayRT;
    }

    void TonemapPass::UpdatePassInfo()
    {
        m_passInfo->name = "tonemap_pipeline";
        m_passInfo->pVertShader = Shader::Create("Shaders/Common/Quad.hlsl", ShaderStage::VertexBit);
        m_passInfo->pFragShader = Shader::Create("Shaders/Tonemap/TonemapPS.hlsl", ShaderStage::FragmentBit);
        m_passInfo->dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        m_passInfo->cullMode = CullMode::Back;
        m_passInfo->colorBlendAttachments = {PipelineColorBlendAttachmentState::Default};
        m_passInfo->colorFormats = {m_displayRT->GetFormat()};
        m_passInfo->ReflectDescriptors();
        m_passInfo->UpdateHash();
    }

    void TonemapPass::CreateUniforms(CommandBuffer *cmd)
    {
        UpdateDescriptorSets();
    }

    void TonemapPass::UpdateDescriptorSets()
    {
        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
        {
            auto *DSet = m_passInfo->GetDescriptors(i)[0];
            DSet->SetImageView(0, m_frameImage->GetSRV(), m_frameImage->GetSampler()->ApiHandle());
            DSet->Update();
        }
    }

    void TonemapPass::Update(Camera *camera)
    {
    }

    void TonemapPass::Draw(CommandBuffer *cmd)
    {
        std::vector<ImageBarrierInfo> barriers(2);
        barriers[0].image = m_frameImage;
        barriers[0].layout = ImageLayout::ShaderReadOnly;
        barriers[0].stageFlags = PipelineStage::FragmentShaderBit;
        barriers[0].accessMask = Access::ShaderReadBit;
        barriers[1].image = m_displayRT;
        barriers[1].layout = ImageLayout::Attachment;
        barriers[1].stageFlags = PipelineStage::ColorAttachmentOutputBit;
        barriers[1].accessMask = Access::ColorAttachmentWriteBit;

        cmd->BeginDebugRegion("TonemapPass");
        cmd->CopyImage(m_displayRT, m_frameImage); // Copy RT to image
        cmd->ImageBarriers(barriers);

        cmd->BeginPass(1, m_attachments.data(), "Tonemap");
        cmd->BindPipeline(*m_passInfo);
        cmd->SetViewport(0.f, 0.f, m_displayRT->GetWidth_f(), m_displayRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_displayRT->GetWidth(), m_displayRT->GetHeight());
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
        
        cmd->EndDebugRegion();
    }

    void TonemapPass::Resize(uint32_t width, uint32_t height)
    {
        Image::Destroy(m_frameImage);
        Init();
        UpdateDescriptorSets();
    }

    void TonemapPass::Destroy()
    {
        Image::Destroy(m_frameImage);
    }
}
