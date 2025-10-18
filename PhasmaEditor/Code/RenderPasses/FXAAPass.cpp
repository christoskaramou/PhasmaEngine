#include "FXAAPass.h"
#include "GUI/GUI.h"
#include "API/Swapchain.h"
#include "API/Surface.h"
#include "API/Shader.h"
#include "API/RHI.h"
#include "API/Queue.h"
#include "API/Command.h"
#include "API/RenderPass.h"
#include "API/Descriptor.h"
#include "API/Framebuffer.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "Systems/RendererSystem.h"

namespace pe
{
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
        m_passInfo->pVertShader = Shader::Create(Path::Executable + "Assets/Shaders/Common/Quad.hlsl", vk::ShaderStageFlagBits::eVertex, "mainVS", std::vector<Define>{}, ShaderCodeType::HLSL);
        m_passInfo->pFragShader = Shader::Create(Path::Executable + "Assets/Shaders/FXAA/FXAAPS.hlsl", vk::ShaderStageFlagBits::eFragment, "mainPS", std::vector<Define>{}, ShaderCodeType::HLSL);
        m_passInfo->dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        m_passInfo->cullMode = vk::CullModeFlagBits::eBack;
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
        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
        {
            auto *DSet = m_passInfo->GetDescriptors(i)[0];
            DSet->SetImageView(0, m_frameImage->GetSRV(), m_frameImage->GetSampler()->ApiHandle());
            DSet->Update();
        }
    }

    void FXAAPass::Draw(CommandBuffer *cmd)
    {
        ImageBarrierInfo barrier;
        barrier.image = m_frameImage;
        barrier.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.stageFlags = vk::PipelineStageFlagBits2::eFragmentShader;
        barrier.accessMask = vk::AccessFlagBits2::eShaderRead;

        cmd->BeginDebugRegion("FXAAPass");
        cmd->CopyImage(m_viewportRT, m_frameImage);
        cmd->ImageBarrier(barrier);

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
