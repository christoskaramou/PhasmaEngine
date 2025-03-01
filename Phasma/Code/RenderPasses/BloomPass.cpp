#include "BloomPass.h"
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
        ImageBarrierInfo barrier{};
        barrier.image = m_displayRT;
        barrier.layout = ImageLayout::ShaderReadOnly;
        barrier.stageFlags = PipelineStage::FragmentShaderBit;
        barrier.accessMask = Access::ShaderReadBit;

        cmd->ImageBarrier(barrier);
        cmd->BeginPass(1, m_attachments.data(), "Bloom_BrightFilter");
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

        ImageBarrierInfo barrier;
        barrier.image = m_brightFilterRT;
        barrier.layout = ImageLayout::ShaderReadOnly;
        barrier.stageFlags = PipelineStage::FragmentShaderBit;
        barrier.accessMask = Access::ShaderReadBit;

        cmd->ImageBarrier(barrier);
        cmd->BeginPass(1, m_attachments.data(), "Bloom_GaussianBlurHorizontal");
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

        ImageBarrierInfo barrier;
        barrier.image = m_gaussianBlurHorizontalRT;
        barrier.layout = ImageLayout::ShaderReadOnly;
        barrier.stageFlags = PipelineStage::FragmentShaderBit;
        barrier.accessMask = Access::ShaderReadBit;

        cmd->ImageBarrier(barrier);
        cmd->BeginPass(1, m_attachments.data(), "Bloom_GaussianBlurVertical");
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
}
