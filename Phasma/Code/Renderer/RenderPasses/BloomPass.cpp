#include "Renderer/RenderPasses/BloomPass.h"
#include "GUI/GUI.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Surface.h"
#include "Shader/Shader.h"
#include "Renderer/RHI.h"
#include "Renderer/Queue.h"
#include "Renderer/Command.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Image.h"
#include "Renderer/Pipeline.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    void BloomBrightFilterPass::Init()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();
        m_brightFilterRT = rs->GetRenderTarget("brightFilter");
        m_displayRT = rs->GetRenderTarget("display");

        m_attachments.resize(1);
        m_attachments[0] = {};
        m_attachments[0].image = m_brightFilterRT;
        m_attachments[0].initialLayout = ImageLayout::Attachment;
        m_attachments[0].finalLayout = ImageLayout::ShaderReadOnly;
    }

    void BloomBrightFilterPass::UpdatePassInfo()
    {
        m_passInfo->name = "BrightFilter_pipeline";
        m_passInfo->pVertShader = Shader::Create("Shaders/Common/Quad.hlsl", ShaderStage::VertexBit);
        m_passInfo->pFragShader = Shader::Create("Shaders/Bloom/BrightFilterPS.hlsl", ShaderStage::FragmentBit);
        m_passInfo->dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        m_passInfo->cullMode = CullMode::Back;
        m_passInfo->colorBlendAttachments = {PipelineColorBlendAttachmentState::Default};
        m_passInfo->colorFormats = {m_brightFilterRT->GetFormat()};
        m_passInfo->ReflectDescriptors();
        m_passInfo->UpdateHash();
    }

    void BloomBrightFilterPass::CreateUniforms(CommandBuffer *cmd)
    {
        UpdateDescriptorSets();
    }

    void BloomBrightFilterPass::UpdateDescriptorSets()
    {
        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
        {
            auto *DSet = m_passInfo->GetDescriptors(i)[0];
            DSet->SetImageView(0, m_displayRT->GetSRV(), m_displayRT->GetSampler()->ApiHandle());
            DSet->Update();
        }
    }

    void BloomBrightFilterPass::Draw(CommandBuffer *cmd)
    {
        std::vector<ImageBarrierInfo> barriers(2);
        barriers[0].image = m_displayRT;
        barriers[0].layout = ImageLayout::ShaderReadOnly;
        barriers[0].stageFlags = PipelineStage::FragmentShaderBit;
        barriers[0].accessMask = Access::ShaderReadBit;
        barriers[1].image = m_brightFilterRT;
        barriers[1].layout = ImageLayout::Attachment;
        barriers[1].stageFlags = PipelineStage::ColorAttachmentOutputBit;
        barriers[1].accessMask = Access::ColorAttachmentWriteBit;

        cmd->ImageBarriers(barriers);
        cmd->BeginPass(m_attachments, "Bloom_BrightFilter");
        cmd->BindPipeline(*m_passInfo);
        cmd->SetViewport(0.f, 0.f, m_brightFilterRT->GetWidth_f(), m_brightFilterRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_brightFilterRT->GetWidth(), m_brightFilterRT->GetHeight());
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
    }

    void BloomBrightFilterPass::Resize(uint32_t width, uint32_t height)
    {
        Init();
        UpdateDescriptorSets();
    }

    void BloomGaussianBlurHorizontalPass::Init()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();
        m_gaussianBlurHorizontalRT = rs->GetRenderTarget("gaussianBlurHorizontal");
        m_brightFilterRT = rs->GetRenderTarget("brightFilter");

        m_attachments.resize(1);
        m_attachments[0] = {};
        m_attachments[0].image = m_gaussianBlurHorizontalRT;
        m_attachments[0].initialLayout = ImageLayout::Attachment;
        m_attachments[0].finalLayout = ImageLayout::ShaderReadOnly;
    }

    void BloomGaussianBlurHorizontalPass::UpdatePassInfo()
    {
        m_passInfo->name = "GaussianBlurHorizontal_pipeline";
        m_passInfo->pVertShader = Shader::Create("Shaders/Common/Quad.hlsl", ShaderStage::VertexBit);
        m_passInfo->pFragShader = Shader::Create("Shaders/Bloom/GaussianBlurHPS.hlsl", ShaderStage::FragmentBit);
        m_passInfo->dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        m_passInfo->cullMode = CullMode::Back;
        m_passInfo->colorBlendAttachments = {PipelineColorBlendAttachmentState::Default};
        m_passInfo->colorFormats = {m_gaussianBlurHorizontalRT->GetFormat()};
        m_passInfo->ReflectDescriptors();
        m_passInfo->UpdateHash();
    }

    void BloomGaussianBlurHorizontalPass::CreateUniforms(CommandBuffer *cmd)
    {
        UpdateDescriptorSets();
    }

    void BloomGaussianBlurHorizontalPass::UpdateDescriptorSets()
    {
        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
        {
            auto *DSet = m_passInfo->GetDescriptors(i)[0];
            DSet->SetImageView(0, m_brightFilterRT->GetSRV(), m_brightFilterRT->GetSampler()->ApiHandle());
            DSet->Update();
        }
    }

    void BloomGaussianBlurHorizontalPass::Draw(CommandBuffer *cmd)
    {
        auto &gSettings = Settings::Get<GlobalSettings>();

        std::vector<ImageBarrierInfo> barriers(2);
        barriers[0].image = m_brightFilterRT;
        barriers[0].layout = ImageLayout::ShaderReadOnly;
        barriers[0].stageFlags = PipelineStage::FragmentShaderBit;
        barriers[0].accessMask = Access::ShaderReadBit;
        barriers[1].image = m_gaussianBlurHorizontalRT;
        barriers[1].layout = ImageLayout::Attachment;
        barriers[1].stageFlags = PipelineStage::ColorAttachmentOutputBit;
        barriers[1].accessMask = Access::ColorAttachmentWriteBit;

        cmd->ImageBarriers(barriers);
        cmd->BeginPass(m_attachments, "Bloom_GaussianBlurHorizontal");
        cmd->BindPipeline(*m_passInfo);
        cmd->SetViewport(0.f, 0.f, m_gaussianBlurHorizontalRT->GetWidth_f(), m_gaussianBlurHorizontalRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_gaussianBlurHorizontalRT->GetWidth(), m_gaussianBlurHorizontalRT->GetHeight());
        cmd->SetConstantAt(0, gSettings.bloom_range);
        cmd->SetConstantAt(1, gSettings.bloom_strength);
        cmd->PushConstants();
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
    }

    void BloomGaussianBlurHorizontalPass::Resize(uint32_t width, uint32_t height)
    {
        Init();
        UpdateDescriptorSets();
    }

    void BloomGaussianBlurVerticalPass::Init()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();
        m_displayRT = rs->GetRenderTarget("display");
        m_gaussianBlurHorizontalRT = rs->GetRenderTarget("gaussianBlurHorizontal");

        m_attachments.resize(1);
        m_attachments[0] = {};
        m_attachments[0].image = m_displayRT;
        m_attachments[0].initialLayout = ImageLayout::Attachment;
        m_attachments[0].finalLayout = ImageLayout::ShaderReadOnly;
        m_attachments[0].loadOp = AttachmentLoadOp::Load;
    }

    void BloomGaussianBlurVerticalPass::UpdatePassInfo()
    {
        m_passInfo->name = "GaussianBlurVertical_pipeline";
        m_passInfo->pVertShader = Shader::Create("Shaders/Common/Quad.hlsl", ShaderStage::VertexBit);
        m_passInfo->pFragShader = Shader::Create("Shaders/Bloom/GaussianBlurVPS.hlsl", ShaderStage::FragmentBit);
        m_passInfo->dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        m_passInfo->cullMode = CullMode::Back;
        m_passInfo->blendEnable = true;
        m_passInfo->colorBlendAttachments = {PipelineColorBlendAttachmentState::AdditiveColor};
        m_passInfo->colorFormats = {m_displayRT->GetFormat()};
        m_passInfo->ReflectDescriptors();
        m_passInfo->UpdateHash();
    }

    void BloomGaussianBlurVerticalPass::CreateUniforms(CommandBuffer *cmd)
    {
        UpdateDescriptorSets();
    }

    void BloomGaussianBlurVerticalPass::UpdateDescriptorSets()
    {
        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
        {
            auto *DSet = m_passInfo->GetDescriptors(i)[0];
            DSet->SetImageView(0, m_gaussianBlurHorizontalRT->GetSRV(), m_gaussianBlurHorizontalRT->GetSampler()->ApiHandle());
            DSet->Update();
        }
    }

    void BloomGaussianBlurVerticalPass::Draw(CommandBuffer *cmd)
    {
        auto &gSettings = Settings::Get<GlobalSettings>();

        std::vector<ImageBarrierInfo> barriers(2);
        barriers[0].image = m_gaussianBlurHorizontalRT;
        barriers[0].layout = ImageLayout::ShaderReadOnly;
        barriers[0].stageFlags = PipelineStage::FragmentShaderBit;
        barriers[0].accessMask = Access::ShaderReadBit;
        barriers[1].image = m_displayRT;
        barriers[1].layout = ImageLayout::Attachment;
        barriers[1].stageFlags = PipelineStage::ColorAttachmentOutputBit;
        barriers[1].accessMask = Access::ColorAttachmentWriteBit;

        cmd->ImageBarriers(barriers);
        cmd->BeginPass(m_attachments, "Bloom_GaussianBlurVertical");
        cmd->BindPipeline(*m_passInfo);
        cmd->SetViewport(0.f, 0.f, m_displayRT->GetWidth_f(), m_displayRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_displayRT->GetWidth(), m_displayRT->GetHeight());
        cmd->SetConstantAt(0, gSettings.bloom_range);
        cmd->SetConstantAt(1, gSettings.bloom_strength);
        cmd->PushConstants();
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
    }

    void BloomGaussianBlurVerticalPass::Resize(uint32_t width, uint32_t height)
    {
        Init();
        UpdateDescriptorSets();
    }

    // void BloomCombinePass::Init()
    // {
    //     RendererSystem *rs = GetGlobalSystem<RendererSystem>();
    //     m_gaussianBlurVerticalRT = rs->GetRenderTarget("gaussianBlurVertical");
    //     m_gaussianBlurHorizontalRT = rs->GetRenderTarget("gaussianBlurHorizontal");

    //     m_attachments.resize(1);
    //     m_attachments[0] = {};
    //     m_attachments[0].image = m_gaussianBlurVerticalRT;
    //     m_attachments[0].initialLayout = ImageLayout::Attachment;
    //     m_attachments[0].finalLayout = ImageLayout::ShaderReadOnly;
    // }

    // void BloomCombinePass::UpdatePassInfo()
    // {
    //     m_passInfo->name = "BloomCombine_pipeline";
    //     m_passInfo->pVertShader = Shader::Create("Shaders/Common/Quad.hlsl", ShaderStage::VertexBit);
    //     m_passInfo->pFragShader = Shader::Create("Shaders/Bloom/CombinePS.hlsl", ShaderStage::FragmentBit);
    //     m_passInfo->dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
    //     m_passInfo->cullMode = CullMode::Back;
    //     m_passInfo->colorBlendAttachments = {m_displayRT->GetBlendAttachment()};
    //     m_passInfo->colorFormats = {m_displayRT->GetFormat()};
    //     m_passInfo->ReflectDescriptors();
    //     m_passInfo->UpdateHash();
    // }

    // void BloomCombinePass::CreateUniforms(CommandBuffer *cmd)
    // {
    //     UpdateDescriptorSets();
    // }

    // void BloomCombinePass::UpdateDescriptorSets()
    // {
    //     for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
    //     {
    //         auto *DSet = m_passInfo->GetDescriptors(i)[0];
    //         DSet->SetImageView(0, m_gaussianBlurHorizontalRT->GetSRV(), m_gaussianBlurHorizontalRT->GetSampler()->ApiHandle());
    //         DSet->Update();
    //     }
    // }

    // void BloomCombinePass::Draw(CommandBuffer *cmd)
    // {
    //     auto &gSettings = Settings::Get<GlobalSettings>();

    //     std::vector<ImageBarrierInfo> barriers(2);
    //     barriers[0].image = m_gaussianBlurHorizontalRT;
    //     barriers[0].layout = ImageLayout::ShaderReadOnly;
    //     barriers[0].stageFlags = PipelineStage::FragmentShaderBit;
    //     barriers[0].accessMask = Access::ShaderReadBit;
    //     barriers[1].image = m_gaussianBlurVerticalRT;
    //     barriers[1].layout = ImageLayout::Attachment;
    //     barriers[1].stageFlags = PipelineStage::ColorAttachmentOutputBit;
    //     barriers[1].accessMask = Access::ColorAttachmentWriteBit;

    //     cmd->ImageBarriers(barriers);
    //     cmd->BeginPass(m_attachments, "GaussianBlurVertical");
    //     cmd->BindPipeline(*m_passInfo);
    //     cmd->SetViewport(0.f, 0.f, m_gaussianBlurVerticalRT->GetWidth_f(), m_gaussianBlurVerticalRT->GetHeight_f());
    //     cmd->SetScissor(0, 0, m_gaussianBlurVerticalRT->GetWidth(), m_gaussianBlurVerticalRT->GetHeight());
    //     cmd->PushConstants();
    //     cmd->Draw(3, 1, 0, 0);
    //     cmd->EndPass();
    // }

    // void BloomCombinePass::Resize(uint32_t width, uint32_t height)
    // {
    //     Init();
    //     UpdateDescriptorSets();
    // }

    // void BloomCombinePass::Destroy()
    // {
    // }

    // void BloomPass::Init()
    // {
    //     RendererSystem *rs = GetGlobalSystem<RendererSystem>();

    //     m_brightFilterRT = rs->GetRenderTarget("brightFilter");
    //     m_gaussianBlurHorizontalRT = rs->GetRenderTarget("gaussianBlurHorizontal");
    //     m_gaussianBlurVerticalRT = rs->GetRenderTarget("gaussianBlurVertical");
    //     m_displayRT = rs->GetRenderTarget("display");
    //     m_frameImage = rs->CreateFSSampledImage(false);

    //     m_attachmentBF = {};
    //     m_attachmentBF.image = m_brightFilterRT;
    //     m_attachmentBF.initialLayout = ImageLayout::Attachment;
    //     m_attachmentBF.finalLayout = ImageLayout::ShaderReadOnly;

    //     m_attachmentGBH = {};
    //     m_attachmentGBH.image = m_gaussianBlurHorizontalRT;
    //     m_attachmentGBH.initialLayout = ImageLayout::Attachment;
    //     m_attachmentGBH.finalLayout = ImageLayout::ShaderReadOnly;

    //     m_attachmentGBV = {};
    //     m_attachmentGBV.image = m_gaussianBlurVerticalRT;
    //     m_attachmentGBV.initialLayout = ImageLayout::Attachment;
    //     m_attachmentGBV.finalLayout = ImageLayout::ShaderReadOnly;

    //     m_attachmentCombine = {};
    //     m_attachmentCombine.image = m_displayRT;
    //     m_attachmentCombine.initialLayout = ImageLayout::Attachment;
    //     m_attachmentCombine.finalLayout = ImageLayout::ShaderReadOnly;
    // }

    // void BloomPass::UpdatePassInfo()
    // {
    //     UpdatePassInfoBrightFilter();
    //     UpdatePassInfoGaussianBlurHorizontal();
    //     UpdatePassInfoGaussianBlurVertical();
    //     UpdatePassInfoCombine();
    // }

    // void BloomPass::UpdatePassInfoBrightFilter()
    // {
    //     PassInfo &info = m_passInfoBF;

    //     info.name = "BrightFilter_pipeline";
    //     info.pVertShader = Shader::Create("Shaders/Common/Quad.hlsl", ShaderStage::VertexBit);
    //     info.pFragShader = Shader::Create("Shaders/Bloom/BrightFilterPS.hlsl", ShaderStage::FragmentBit);
    //     info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
    //     info.cullMode = CullMode::Back;
    //     info.colorBlendAttachments = {m_brightFilterRT->GetBlendAttachment()};
    //     info.colorFormats = {m_brightFilterRT->GetFormat()};
    //     info.ReflectDescriptors();
    //     info.UpdateHash();
    // }

    // void BloomPass::UpdatePassInfoGaussianBlurHorizontal()
    // {
    //     PassInfo &info = m_passInfoGBH;

    //     info.name = "GaussianBlurHorizontal_pipeline";
    //     info.pVertShader = Shader::Create("Shaders/Common/Quad.hlsl", ShaderStage::VertexBit);
    //     info.pFragShader = Shader::Create("Shaders/Bloom/GaussianBlurHPS.hlsl", ShaderStage::FragmentBit);
    //     info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
    //     info.cullMode = CullMode::Back;
    //     info.colorBlendAttachments = {m_gaussianBlurHorizontalRT->GetBlendAttachment()};
    //     info.colorFormats = {m_gaussianBlurHorizontalRT->GetFormat()};
    //     info.ReflectDescriptors();
    //     info.UpdateHash();
    // }

    // void BloomPass::UpdatePassInfoGaussianBlurVertical()
    // {
    //     PassInfo &info = m_passInfoGBV;

    //     info.name = "GaussianBlurVertical_pipeline";
    //     info.pVertShader = Shader::Create("Shaders/Common/Quad.hlsl", ShaderStage::VertexBit);
    //     info.pFragShader = Shader::Create("Shaders/Bloom/GaussianBlurVPS.hlsl", ShaderStage::FragmentBit);
    //     info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
    //     info.cullMode = CullMode::Back;
    //     info.colorBlendAttachments = {m_gaussianBlurVerticalRT->GetBlendAttachment()};
    //     info.colorFormats = {m_gaussianBlurVerticalRT->GetFormat()};
    //     info.ReflectDescriptors();
    //     info.UpdateHash();
    // }

    // void BloomPass::UpdatePassInfoCombine()
    // {
    //     PassInfo &info = m_passInfoCombine;

    //     info.name = "BloomCombine_pipeline";
    //     info.pVertShader = Shader::Create("Shaders/Common/Quad.hlsl", ShaderStage::VertexBit);
    //     info.pFragShader = Shader::Create("Shaders/Bloom/CombinePS.hlsl", ShaderStage::FragmentBit);
    //     info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
    //     info.cullMode = CullMode::Back;
    //     info.colorBlendAttachments = {m_displayRT->GetBlendAttachment()};
    //     info.colorFormats = {m_displayRT->GetFormat()};
    //     info.ReflectDescriptors();
    //     info.UpdateHash();
    // }

    // void BloomPass::CreateUniforms(CommandBuffer *cmd)
    // {
    //     UpdateDescriptorSets();
    // }

    // void BloomPass::UpdateDescriptorSets()
    // {
    //     for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
    //     {
    //         auto *DSBrightFilter = m_passInfoBF.GetDescriptors(i)[0];
    //         DSBrightFilter->SetImageView(0, m_frameImage->GetSRV(), m_frameImage->GetSampler()->ApiHandle());
    //         DSBrightFilter->Update();

    //         auto *DSGaussianBlurHorizontal = m_passInfoGBH.GetDescriptors(i)[0];
    //         DSGaussianBlurHorizontal->SetImageView(0, m_brightFilterRT->GetSRV(), m_brightFilterRT->GetSampler()->ApiHandle());
    //         DSGaussianBlurHorizontal->Update();

    //         auto *DSGaussianBlurVertical = m_passInfoGBV.GetDescriptors(i)[0];
    //         DSGaussianBlurVertical->SetImageView(0, m_gaussianBlurHorizontalRT->GetSRV(), m_gaussianBlurHorizontalRT->GetSampler()->ApiHandle());
    //         DSGaussianBlurVertical->Update();

    //         auto *DSCombine = m_passInfoCombine.GetDescriptors(i)[0];
    //         DSCombine->SetImageView(0, m_frameImage->GetSRV(), m_frameImage->GetSampler()->ApiHandle());
    //         DSCombine->SetImageView(1, m_gaussianBlurVerticalRT->GetSRV(), m_gaussianBlurVerticalRT->GetSampler()->ApiHandle());
    //         DSCombine->Update();
    //     }
    // }

    // void BloomPass::Draw(CommandBuffer *cmd)
    // {
    //     auto &gSettings = Settings::Get<GlobalSettings>();

    //     ImageBarrierInfo barrierRead{};
    //     barrierRead.layout = ImageLayout::ShaderReadOnly;
    //     barrierRead.stageFlags = PipelineStage::FragmentShaderBit;
    //     barrierRead.accessMask = Access::ShaderReadBit;
    //     ImageBarrierInfo barrierWrite{};
    //     barrierWrite.layout = ImageLayout::Attachment;
    //     barrierWrite.stageFlags = PipelineStage::ColorAttachmentOutputBit;
    //     barrierWrite.accessMask = Access::ColorAttachmentWriteBit;

    //     cmd->BeginDebugRegion("BloomPass");

    //     // Copy RT
    //     cmd->CopyImage(m_displayRT, m_frameImage);
    //     barrierRead.image = m_frameImage;
    //     cmd->ImageBarrier(barrierRead);
    //     barrierWrite.image = m_brightFilterRT;
    //     cmd->ImageBarrier(barrierWrite);
    //     cmd->BeginPass({m_attachmentBF}, "BrightFilter");
    //     cmd->BindPipeline(m_passInfoBF);
    //     cmd->SetViewport(0.f, 0.f, m_brightFilterRT->GetWidth_f(), m_brightFilterRT->GetHeight_f());
    //     cmd->SetScissor(0, 0, m_brightFilterRT->GetWidth(), m_brightFilterRT->GetHeight());
    //     cmd->Draw(3, 1, 0, 0);
    //     cmd->EndPass();

    //     // PushConstants_Bloom
    //     cmd->SetConstants(gSettings.bloom_range);
    //     barrierRead.image = m_brightFilterRT;
    //     cmd->ImageBarrier(barrierRead);
    //     barrierWrite.image = m_gaussianBlurHorizontalRT;
    //     cmd->ImageBarrier(barrierWrite);
    //     cmd->BeginPass({m_attachmentGBH}, "GaussianBlurHorizontal");
    //     cmd->BindPipeline(m_passInfoGBH);
    //     cmd->SetViewport(0.f, 0.f, m_gaussianBlurHorizontalRT->GetWidth_f(), m_gaussianBlurHorizontalRT->GetHeight_f());
    //     cmd->SetScissor(0, 0, m_gaussianBlurHorizontalRT->GetWidth(), m_gaussianBlurHorizontalRT->GetHeight());
    //     cmd->PushConstants();
    //     cmd->Draw(3, 1, 0, 0);
    //     cmd->EndPass();

    //     barrierRead.image = m_gaussianBlurHorizontalRT;
    //     cmd->ImageBarrier(barrierRead);
    //     barrierWrite.image = m_gaussianBlurVerticalRT;
    //     cmd->ImageBarrier(barrierWrite);
    //     cmd->BeginPass({m_attachmentGBV}, "GaussianBlurVertical");
    //     cmd->BindPipeline(m_passInfoGBV);
    //     cmd->SetViewport(0.f, 0.f, m_gaussianBlurVerticalRT->GetWidth_f(), m_gaussianBlurVerticalRT->GetHeight_f());
    //     cmd->SetScissor(0, 0, m_gaussianBlurVerticalRT->GetWidth(), m_gaussianBlurVerticalRT->GetHeight());
    //     cmd->PushConstants();
    //     cmd->Draw(3, 1, 0, 0);
    //     cmd->EndPass();

    //     // PushConstants_Bloom_Combine
    //     cmd->SetConstants(gSettings.bloom_strength);
    //     barrierRead.image = m_frameImage;
    //     cmd->ImageBarrier(barrierRead);
    //     barrierRead.image = m_gaussianBlurVerticalRT;
    //     cmd->ImageBarrier(barrierRead);
    //     barrierWrite.image = m_displayRT;
    //     cmd->ImageBarrier(barrierWrite);
    //     cmd->BeginPass({m_attachmentCombine}, "Bloom Combine");
    //     cmd->BindPipeline(m_passInfoCombine);
    //     cmd->SetViewport(0.f, 0.f, m_displayRT->GetWidth_f(), m_displayRT->GetHeight_f());
    //     cmd->SetScissor(0, 0, m_displayRT->GetWidth(), m_displayRT->GetHeight());
    //     cmd->PushConstants();
    //     cmd->Draw(3, 1, 0, 0);
    //     cmd->EndPass();

    //     cmd->EndDebugRegion();
    // }

    // void BloomPass::Resize(uint32_t width, uint32_t height)
    // {
    //     Image::Destroy(m_frameImage);
    //     Init();
    //     UpdateDescriptorSets();
    // }

    // void BloomPass::Destroy()
    // {
    //     Image::Destroy(m_frameImage);
    // }
}
