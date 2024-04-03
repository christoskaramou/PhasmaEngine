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
    BloomPass::BloomPass()
    {
    }

    BloomPass::~BloomPass()
    {
    }

    void BloomPass::Init()
    {
        m_renderQueue = RHII.GetRenderQueue();
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();

        m_brightFilterRT = rs->GetRenderTarget("brightFilter");
        m_gaussianBlurHorizontalRT = rs->GetRenderTarget("gaussianBlurHorizontal");
        m_gaussianBlurVerticalRT = rs->GetRenderTarget("gaussianBlurVertical");
        m_displayRT = rs->GetRenderTarget("display");
        m_frameImage = rs->CreateFSSampledImage(false);

        m_attachmentBF = {};
        m_attachmentBF.image = m_brightFilterRT;
        m_attachmentBF.initialLayout = ImageLayout::Attachment;
        m_attachmentBF.finalLayout = ImageLayout::ShaderReadOnly;

        m_attachmentGBH = {};
        m_attachmentGBH.image = m_gaussianBlurHorizontalRT;
        m_attachmentGBH.initialLayout = ImageLayout::Attachment;
        m_attachmentGBH.finalLayout = ImageLayout::ShaderReadOnly;

        m_attachmentGBV = {};
        m_attachmentGBV.image = m_gaussianBlurVerticalRT;
        m_attachmentGBV.initialLayout = ImageLayout::Attachment;
        m_attachmentGBV.finalLayout = ImageLayout::ShaderReadOnly;

        m_attachmentCombine = {};
        m_attachmentCombine.image = m_displayRT;
        m_attachmentCombine.initialLayout = ImageLayout::Attachment;
        m_attachmentCombine.finalLayout = ImageLayout::ShaderReadOnly;
    }

    void BloomPass::UpdatePassInfo()
    {
        UpdatePassInfoBrightFilter();
        UpdatePassInfoGaussianBlurHorizontal();
        UpdatePassInfoGaussianBlurVertical();
        UpdatePassInfoCombine();
    }

    void BloomPass::UpdatePassInfoBrightFilter()
    {
        PassInfo &info = m_passInfoBF;

        info.name = "BrightFilter_pipeline";
        info.pVertShader = Shader::Create("Shaders/Common/Quad.hlsl", ShaderStage::VertexBit);
        info.pFragShader = Shader::Create("Shaders/Bloom/BrightFilterPS.hlsl", ShaderStage::FragmentBit);
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {m_brightFilterRT->GetBlendAttachment()};
        info.colorFormats = m_brightFilterRT->GetFormatPtr();
        info.UpdateHash();
    }

    void BloomPass::UpdatePassInfoGaussianBlurHorizontal()
    {
        PassInfo &info = m_passInfoGBH;

        info.name = "GaussianBlurHorizontal_pipeline";
        info.pVertShader = Shader::Create("Shaders/Common/Quad.hlsl", ShaderStage::VertexBit);
        info.pFragShader = Shader::Create("Shaders/Bloom/GaussianBlurHPS.hlsl", ShaderStage::FragmentBit);
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {m_gaussianBlurHorizontalRT->GetBlendAttachment()};
        info.colorFormats = m_gaussianBlurHorizontalRT->GetFormatPtr();
        info.UpdateHash();
    }

    void BloomPass::UpdatePassInfoGaussianBlurVertical()
    {
        PassInfo &info = m_passInfoGBV;

        info.name = "GaussianBlurVertical_pipeline";
        info.pVertShader = Shader::Create("Shaders/Common/Quad.hlsl", ShaderStage::VertexBit);
        info.pFragShader = Shader::Create("Shaders/Bloom/GaussianBlurVPS.hlsl", ShaderStage::FragmentBit);
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {m_gaussianBlurVerticalRT->GetBlendAttachment()};
        info.colorFormats = m_gaussianBlurVerticalRT->GetFormatPtr();
        info.UpdateHash();
    }

    void BloomPass::UpdatePassInfoCombine()
    {
        PassInfo &info = m_passInfoCombine;

        info.name = "BloomCombine_pipeline";
        info.pVertShader = Shader::Create("Shaders/Common/Quad.hlsl", ShaderStage::VertexBit);
        info.pFragShader = Shader::Create("Shaders/Bloom/CombinePS.hlsl", ShaderStage::FragmentBit);
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {m_displayRT->GetBlendAttachment()};
        info.colorFormats = m_displayRT->GetFormatPtr();
        info.UpdateHash();
    }

    void BloomPass::CreateUniforms(CommandBuffer *cmd)
    {
        UpdateDescriptorSets();
    }

    void BloomPass::UpdateDescriptorSets()
    {
        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
        {
            auto *DSBrightFilter = m_passInfoBF.GetDescriptors(i)[0];
            DSBrightFilter->SetImageView(0, m_frameImage->GetSRV(), m_frameImage->GetSampler()->ApiHandle());
            DSBrightFilter->Update();

            auto *DSGaussianBlurHorizontal = m_passInfoGBH.GetDescriptors(i)[0];
            DSGaussianBlurHorizontal->SetImageView(0, m_brightFilterRT->GetSRV(), m_brightFilterRT->GetSampler()->ApiHandle());
            DSGaussianBlurHorizontal->Update();

            auto *DSGaussianBlurVertical = m_passInfoGBV.GetDescriptors(i)[0];
            DSGaussianBlurVertical->SetImageView(0, m_gaussianBlurHorizontalRT->GetSRV(), m_gaussianBlurHorizontalRT->GetSampler()->ApiHandle());
            DSGaussianBlurVertical->Update();

            auto *DSCombine = m_passInfoCombine.GetDescriptors(i)[0];
            DSCombine->SetImageView(0, m_frameImage->GetSRV(), m_frameImage->GetSampler()->ApiHandle());
            DSCombine->SetImageView(1, m_gaussianBlurVerticalRT->GetSRV(), m_gaussianBlurVerticalRT->GetSampler()->ApiHandle());
            DSCombine->Update();
        }
    }

    void BloomPass::Update(Camera *camera)
    {
    }

    CommandBuffer *BloomPass::Draw()
    {
        CommandBuffer *cmd = CommandBuffer::GetFree(m_renderQueue);
        cmd->Begin();

        cmd->BeginDebugRegion("BloomPass");

        // Copy RT
        cmd->CopyImage(m_displayRT, m_frameImage);

        ImageBarrierInfo barrienRead{};
        barrienRead.layout = ImageLayout::ShaderReadOnly;
        barrienRead.stageFlags = PipelineStage::FragmentShaderBit;
        barrienRead.accessMask = Access::ShaderReadBit;
        ImageBarrierInfo barrienWrite{};
        barrienWrite.layout = ImageLayout::Attachment;
        barrienWrite.stageFlags = PipelineStage::ColorAttachmentOutputBit;
        barrienWrite.accessMask = Access::ColorAttachmentWriteBit;

        barrienRead.image = m_frameImage;
        cmd->ImageBarrier(barrienRead);
        barrienWrite.image = m_brightFilterRT;
        cmd->ImageBarrier(barrienWrite);
        cmd->BeginPass({m_attachmentBF}, "BrightFilter");
        cmd->BindPipeline(m_passInfoBF);
        cmd->SetViewport(0.f, 0.f, m_brightFilterRT->GetWidth_f(), m_brightFilterRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_brightFilterRT->GetWidth(), m_brightFilterRT->GetHeight());
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();

        auto &gSettings = Settings::Get<GlobalSettings>();

        // PushConstants_Bloom
        cmd->SetConstants(gSettings.bloom_range);

        barrienRead.image = m_brightFilterRT;
        cmd->ImageBarrier(barrienRead);
        barrienWrite.image = m_gaussianBlurHorizontalRT;
        cmd->ImageBarrier(barrienWrite);
        cmd->BeginPass({m_attachmentGBH}, "GaussianBlurHorizontal");
        cmd->BindPipeline(m_passInfoGBH);
        cmd->SetViewport(0.f, 0.f, m_gaussianBlurHorizontalRT->GetWidth_f(), m_gaussianBlurHorizontalRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_gaussianBlurHorizontalRT->GetWidth(), m_gaussianBlurHorizontalRT->GetHeight());
        cmd->PushConstants();
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();

        barrienRead.image = m_gaussianBlurHorizontalRT;
        cmd->ImageBarrier(barrienRead);
        barrienWrite.image = m_gaussianBlurVerticalRT;
        cmd->ImageBarrier(barrienWrite);
        cmd->BeginPass({m_attachmentGBV}, "GaussianBlurVertical");
        cmd->BindPipeline(m_passInfoGBV);
        cmd->SetViewport(0.f, 0.f, m_gaussianBlurVerticalRT->GetWidth_f(), m_gaussianBlurVerticalRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_gaussianBlurVerticalRT->GetWidth(), m_gaussianBlurVerticalRT->GetHeight());
        cmd->PushConstants();
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();

        // PushConstants_Bloom_Combine
        cmd->SetConstants(gSettings.bloom_strength);

        barrienRead.image = m_frameImage;
        cmd->ImageBarrier(barrienRead);
        barrienRead.image = m_gaussianBlurVerticalRT;
        cmd->ImageBarrier(barrienRead);
        barrienWrite.image = m_displayRT;
        cmd->ImageBarrier(barrienWrite);
        cmd->BeginPass({m_attachmentCombine}, "Bloom Combine");
        cmd->BindPipeline(m_passInfoCombine);
        cmd->SetViewport(0.f, 0.f, m_displayRT->GetWidth_f(), m_displayRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_displayRT->GetWidth(), m_displayRT->GetHeight());
        cmd->PushConstants();
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();

        cmd->EndDebugRegion();

        cmd->End();
        return cmd;
    }

    void BloomPass::Resize(uint32_t width, uint32_t height)
    {
        Image::Destroy(m_frameImage);
        Init();
        UpdateDescriptorSets();
    }

    void BloomPass::Destroy()
    {
        Image::Destroy(m_frameImage);
    }
}
