#include "FXAAPass.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/Shader.h"
#include "Base/Settings.h"
#include "Render/SceneRendererHost.h"

namespace pe
{
    struct FXAABlendPC
    {
        float blend;
    };
    void FXAAPass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();

        m_viewportRT = rs->GetRenderTarget("viewport");
        m_frameImage = rs->CreateFSSampledImage(true);

        m_attachments.resize(1);
        m_attachments[0] = {};
        m_attachments[0].image = m_viewportRT;
    }

    void FXAAPass::UpdatePassInfo()
    {
        m_passInfo->name = "fxaa_pipeline";
        m_passInfo->pVertShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Common/Quad.hlsl", .entryPoint = "mainVS", .stage = PE_SHADER_STAGE_VERTEX, .defines = std::vector<Define>{}});
        m_passInfo->pFragShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/FXAA/FXAAPS.hlsl", .entryPoint = "mainPS", .stage = PE_SHADER_STAGE_FRAGMENT, .defines = std::vector<Define>{}});
        m_passInfo->dynamicStates = {PE_DYNAMIC_STATE_VIEWPORT, PE_DYNAMIC_STATE_SCISSOR};
        m_passInfo->cullMode = PE_CULL_MODE_NONE;
        m_passInfo->colorBlendAttachments = {BlendState::Default};
        m_passInfo->colorFormats = {m_viewportRT->GetFormat()};
        m_passInfo->depthTestEnable = false;
        m_passInfo->depthWriteEnable = false;
        m_passInfo->stencilTestEnable = false;
        m_passInfo->Update();
    }

    void FXAAPass::CreateUniforms(CommandBuffer *cmd)
    {
        UpdateDescriptorSets();
    }

    void FXAAPass::UpdateDescriptorSets()
    {
        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
        {
            auto *DSet = m_passInfo->GetDescriptors(i)[0];
            DSet->SetImageView(0, m_frameImage->GetSRV(), m_frameImage->GetSampler());
            DSet->Update();
        }
    }

    void FXAAPass::ExecutePass(CommandBuffer *cmd)
    {
        ImageBarrierInfo barrier{};
        barrier.image = m_frameImage;
        barrier.layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.stageFlags = PE_STAGE_FRAGMENT_SHADER;
        barrier.accessMask = PE_ACCESS_SHADER_READ;

        cmd->BeginDebugRegion("FXAAPass");
        cmd->CopyImage(m_viewportRT, m_frameImage);
        cmd->ImageBarrier(barrier);

        FXAABlendPC pc{};
        pc.blend = ActivePostProcessBlend().fxaa;

        cmd->BeginPass(1, m_attachments.data(), "FXAA");
        cmd->BindPipeline(*m_passInfo);
        cmd->SetViewport(0.f, 0.f, m_viewportRT->GetWidth_f(), m_viewportRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_viewportRT->GetWidth(), m_viewportRT->GetHeight());
        cmd->SetConstants(pc);
        cmd->PushConstants();
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
} // namespace pe
