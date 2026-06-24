#include "BloomPass.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/RenderGraph.h"
#include "API/Shader.h"
#include "Render/SceneRendererHost.h"

namespace pe
{
    void BloomBrightFilterPass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();
        m_brightFilterRT = rs->GetRenderTarget("brightFilter");
        if (!m_brightFilterRT)
            m_brightFilterRT =
                rs->CreateRenderTarget("brightFilter", RHII.GetSwapchainFormat(), PE_IMAGE_USAGE_NONE, false);
        m_displayRT = rs->GetRenderTarget("display");

        m_attachments.resize(1);
        m_attachments[0] = {};
        m_attachments[0].image = m_brightFilterRT;
    }

    void BloomBrightFilterPass::UpdatePassInfo()
    {
        m_passInfo->name = "BrightFilter_pipeline";
        m_passInfo->pVertShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Common/Quad.hlsl", .entryPoint = "mainVS", .stage = PE_SHADER_STAGE_VERTEX, .defines = std::vector<Define>{}});
        m_passInfo->pFragShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Bloom/BrightFilterPS.hlsl", .entryPoint = "mainPS", .stage = PE_SHADER_STAGE_FRAGMENT, .defines = std::vector<Define>{}});
        m_passInfo->dynamicStates = {PE_DYNAMIC_STATE_VIEWPORT, PE_DYNAMIC_STATE_SCISSOR};
        m_passInfo->cullMode = PE_CULL_MODE_NONE;
        m_passInfo->colorBlendAttachments = {BlendState::Default};
        m_passInfo->colorFormats = {m_brightFilterRT->GetFormat()};
        m_passInfo->depthTestEnable = false;
        m_passInfo->depthWriteEnable = false;
        m_passInfo->stencilTestEnable = false;
        m_passInfo->Update();
    }

    void BloomBrightFilterPass::CreateUniforms(CommandBuffer *cmd)
    {
        UpdateDescriptorSets();
    }

    void BloomBrightFilterPass::UpdateDescriptorSets()
    {
        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
        {
            auto *DSet = m_passInfo->GetDescriptors(i)[0];
            DSet->SetImageView(0, m_displayRT->GetSRV(), m_displayRT->GetSampler());
            DSet->Update();
        }
    }

    void BloomBrightFilterPass::DeclareInputs(RGBuilder &builder)
    {
        builder.Read(m_displayRT);
    }

    void BloomBrightFilterPass::ExecutePass(CommandBuffer *cmd)
    {
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

    void BloomBrightFilterPass::Destroy()
    {
        if (SceneRendererHost *rs = GetActiveSceneRendererHost())
            rs->DestroyRenderTarget("brightFilter");
        m_brightFilterRT = nullptr;
        m_displayRT = nullptr;
    }

    void BloomGaussianBlurHorizontalPass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();
        m_gaussianBlurHorizontalRT = rs->GetRenderTarget("gaussianBlurHorizontal");
        if (!m_gaussianBlurHorizontalRT)
            m_gaussianBlurHorizontalRT =
                rs->CreateRenderTarget("gaussianBlurHorizontal", RHII.GetSwapchainFormat(), PE_IMAGE_USAGE_NONE, false);
        m_brightFilterRT = rs->GetRenderTarget("brightFilter");

        m_attachments.resize(1);
        m_attachments[0] = {};
        m_attachments[0].image = m_gaussianBlurHorizontalRT;
    }

    void BloomGaussianBlurHorizontalPass::UpdatePassInfo()
    {
        m_passInfo->name = "GaussianBlurHorizontal_pipeline";
        m_passInfo->pVertShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Common/Quad.hlsl", .entryPoint = "mainVS", .stage = PE_SHADER_STAGE_VERTEX, .defines = std::vector<Define>{}});
        m_passInfo->pFragShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Bloom/GaussianBlurHPS.hlsl", .entryPoint = "mainPS", .stage = PE_SHADER_STAGE_FRAGMENT, .defines = std::vector<Define>{}});
        m_passInfo->dynamicStates = {PE_DYNAMIC_STATE_VIEWPORT, PE_DYNAMIC_STATE_SCISSOR};
        m_passInfo->cullMode = PE_CULL_MODE_NONE;
        m_passInfo->colorBlendAttachments = {BlendState::Default};
        m_passInfo->colorFormats = {m_gaussianBlurHorizontalRT->GetFormat()};
        m_passInfo->depthTestEnable = false;
        m_passInfo->depthWriteEnable = false;
        m_passInfo->stencilTestEnable = false;
        m_passInfo->Update();
    }

    void BloomGaussianBlurHorizontalPass::CreateUniforms(CommandBuffer *cmd)
    {
        UpdateDescriptorSets();
    }

    void BloomGaussianBlurHorizontalPass::UpdateDescriptorSets()
    {
        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
        {
            auto *DSet = m_passInfo->GetDescriptors(i)[0];
            DSet->SetImageView(0, m_brightFilterRT->GetSRV(), m_brightFilterRT->GetSampler());
            DSet->Update();
        }
    }

    void BloomGaussianBlurHorizontalPass::DeclareInputs(RGBuilder &builder)
    {
        builder.Read(m_brightFilterRT);
    }

    void BloomGaussianBlurHorizontalPass::ExecutePass(CommandBuffer *cmd)
    {
        auto &gSettings = ActivePostProcessProfile();

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

    void BloomGaussianBlurHorizontalPass::Destroy()
    {
        if (SceneRendererHost *rs = GetActiveSceneRendererHost())
            rs->DestroyRenderTarget("gaussianBlurHorizontal");
        m_gaussianBlurHorizontalRT = nullptr;
        m_brightFilterRT = nullptr;
    }

    void BloomGaussianBlurVerticalPass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();
        m_displayRT = rs->GetRenderTarget("display");
        m_gaussianBlurHorizontalRT = rs->GetRenderTarget("gaussianBlurHorizontal");

        m_attachments.resize(1);
        m_attachments[0] = {};
        m_attachments[0].image = m_displayRT;
        m_attachments[0].loadOp = PE_LOAD_OP_LOAD;
    }

    void BloomGaussianBlurVerticalPass::UpdatePassInfo()
    {
        m_passInfo->name = "GaussianBlurVertical_pipeline";
        m_passInfo->pVertShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Common/Quad.hlsl", .entryPoint = "mainVS", .stage = PE_SHADER_STAGE_VERTEX, .defines = std::vector<Define>{}});
        m_passInfo->pFragShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Bloom/GaussianBlurVPS.hlsl", .entryPoint = "mainPS", .stage = PE_SHADER_STAGE_FRAGMENT, .defines = std::vector<Define>{}});
        m_passInfo->dynamicStates = {PE_DYNAMIC_STATE_VIEWPORT, PE_DYNAMIC_STATE_SCISSOR};
        m_passInfo->cullMode = PE_CULL_MODE_NONE;
        m_passInfo->blendEnable = true;
        m_passInfo->colorBlendAttachments = {BlendState::AdditiveColor};
        m_passInfo->colorFormats = {m_displayRT->GetFormat()};
        m_passInfo->depthTestEnable = false;
        m_passInfo->depthWriteEnable = false;
        m_passInfo->stencilTestEnable = false;
        m_passInfo->Update();
    }

    void BloomGaussianBlurVerticalPass::CreateUniforms(CommandBuffer *cmd)
    {
        UpdateDescriptorSets();
    }

    void BloomGaussianBlurVerticalPass::UpdateDescriptorSets()
    {
        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
        {
            auto *DSet = m_passInfo->GetDescriptors(i)[0];
            DSet->SetImageView(0, m_gaussianBlurHorizontalRT->GetSRV(), m_gaussianBlurHorizontalRT->GetSampler());
            DSet->Update();
        }
    }

    void BloomGaussianBlurVerticalPass::DeclareInputs(RGBuilder &builder)
    {
        builder.Read(m_gaussianBlurHorizontalRT);
    }

    void BloomGaussianBlurVerticalPass::ExecutePass(CommandBuffer *cmd)
    {
        auto &gSettings = ActivePostProcessProfile();
        const float blendedStrength = gSettings.bloom_strength * std::clamp(ActivePostProcessBlend().bloom, 0.0f, 1.0f);

        cmd->BeginPass(1, m_attachments.data(), "Bloom_GaussianBlurVertical");
        cmd->BindPipeline(*m_passInfo);
        cmd->SetViewport(0.f, 0.f, m_displayRT->GetWidth_f(), m_displayRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_displayRT->GetWidth(), m_displayRT->GetHeight());
        cmd->SetConstantAt(0, gSettings.bloom_range);
        cmd->SetConstantAt(1, blendedStrength);
        cmd->PushConstants();
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
    }

    void BloomGaussianBlurVerticalPass::Resize(uint32_t width, uint32_t height)
    {
        Init();
        UpdateDescriptorSets();
    }

    void BloomGaussianBlurVerticalPass::Destroy()
    {
        m_displayRT = nullptr;
        m_gaussianBlurHorizontalRT = nullptr;
    }
} // namespace pe
