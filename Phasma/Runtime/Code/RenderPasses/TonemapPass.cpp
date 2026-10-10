#include "TonemapPass.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/RenderGraph.h"
#include "API/Shader.h"
#include "API/Sampler.h"
#include "Render/SceneRenderTargets.h"
#include "Render/SceneRendererHost.h"

namespace pe
{
    struct TonemapBlendPC
    {
        float blend;
        uint32_t linearColor;
    };
    void TonemapPass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();

        m_displayRT = rs->GetRenderTarget("display");
        m_sceneColor = rs->GetRenderTarget("hdrDisplay");

        ImageDesc desc{};
        desc.format = m_displayRT->GetFormat();
        desc.width = m_displayRT->GetWidth();
        desc.height = m_displayRT->GetHeight();
        desc.usage = PE_IMAGE_USAGE_SAMPLED | PE_IMAGE_USAGE_COLOR_ATTACHMENT | PE_IMAGE_USAGE_TRANSFER_DST;
        desc.name = "Tonemap_Resolved";
        m_resolvedImage = Image::Create(desc);
        m_resolvedImage->CreateRTV();
        m_resolvedImage->CreateSRV(PE_IMAGE_VIEW_TYPE_2D);
        m_resolvedImage->SetSampler(Sampler::Create(Sampler::CreateInfoInit(), "Tonemap_ResolvedSampler"));

        m_attachments.resize(1);
        m_attachments[0] = {};
        m_attachments[0].loadOp = PE_LOAD_OP_DONT_CARE;
        Update();
    }

    void TonemapPass::UpdatePassInfo()
    {
        m_passInfo->name = "tonemap_pipeline";
        m_passInfo->pVertShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Common/Quad.hlsl", .entryPoint = "mainVS", .stage = PE_SHADER_STAGE_VERTEX, .defines = std::vector<Define>{}});
        m_passInfo->pFragShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Tonemap/TonemapPS.hlsl", .entryPoint = "mainPS", .stage = PE_SHADER_STAGE_FRAGMENT, .defines = std::vector<Define>{}});
        m_passInfo->dynamicStates = {PE_DYNAMIC_STATE_VIEWPORT, PE_DYNAMIC_STATE_SCISSOR};
        m_passInfo->cullMode = PE_CULL_MODE_NONE;
        m_passInfo->colorBlendAttachments = {BlendState::Default};
        m_passInfo->colorFormats = {m_displayRT->GetFormat()};
        m_passInfo->depthTestEnable = false;
        m_passInfo->depthWriteEnable = false;
        m_passInfo->stencilTestEnable = false;
        m_passInfo->Update();
    }

    void TonemapPass::CreateUniforms(CommandBuffer *cmd)
    {
        UpdateDescriptorSets();
    }

    void TonemapPass::UpdateDescriptorSets()
    {
        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
        {
            auto *DSet = m_passInfo->GetDescriptors(i)[0];
            Image *input = SceneUsesHDR() ? m_sceneColor : m_resolvedImage;
            DSet->SetImageView(0, input->GetSRV(), input->GetSampler());
            DSet->Update();
        }
    }

    void TonemapPass::Update()
    {
        const auto &pp = ActivePostProcessProfile();
        m_displayRT = SceneUsesHDR() && pp.taa && pp.cas_sharpening ? m_resolvedImage : RequireActiveSceneRendererHost().GetDisplayRT();
        m_attachments[0].image = m_displayRT;
    }

    void TonemapPass::DeclareInputs(RGBuilder &builder)
    {
        // LDR copies the scene colour (it is the display target) and samples the copy.
        if (SceneUsesHDR())
            builder.Read(m_sceneColor);
        else
            builder.Barrier(m_sceneColor, PE_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, PE_STAGE_TRANSFER, PE_ACCESS_TRANSFER_READ);
    }

    void TonemapPass::DeclareOutputs(RGBuilder &builder)
    {
        Update(); // Includes the player's per-frame swapchain override.
        IRenderPassComponent::DeclareOutputs(builder);
    }

    void TonemapPass::ExecutePass(CommandBuffer *cmd)
    {
        cmd->BeginDebugRegion("TonemapPass");
        if (!SceneUsesHDR())
        {
            cmd->CopyImage(m_sceneColor, m_resolvedImage);
            cmd->ImageBarrier({.image = m_resolvedImage, .layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .stageFlags = PE_STAGE_FRAGMENT_SHADER, .accessMask = PE_ACCESS_SHADER_READ});
        }

        TonemapBlendPC pc{};
        pc.blend = ActivePostProcessProfile().tonemapping ? ActivePostProcessBlend().tonemapping : 0.f;
        pc.linearColor = SceneUsesLinearColor() ? 1u : 0u;

        cmd->BeginPass(1, m_attachments.data(), "Tonemap");
        cmd->BindPipeline(*m_passInfo);
        cmd->SetViewport(0.f, 0.f, m_displayRT->GetWidth_f(), m_displayRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_displayRT->GetWidth(), m_displayRT->GetHeight());
        cmd->SetConstants(pc);
        cmd->PushConstants();
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();

        cmd->EndDebugRegion();
    }

    void TonemapPass::Resize(uint32_t width, uint32_t height)
    {
        Image::Destroy(m_resolvedImage);
        Init();
        UpdateDescriptorSets();
    }

    void TonemapPass::Destroy()
    {
        Image::Destroy(m_resolvedImage);
    }
} // namespace pe
