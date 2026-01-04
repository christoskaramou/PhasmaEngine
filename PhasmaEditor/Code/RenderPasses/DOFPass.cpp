#include "DOFPass.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/Shader.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    void DOFPass::Init()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();

        m_displayRT = rs->GetRenderTarget("display");
        m_depth = rs->GetDepthStencilTarget("depthStencil");
        m_frameImage = rs->CreateFSSampledImage(false);

        m_attachments.resize(1);
        m_attachments[0] = {};
        m_attachments[0].image = m_displayRT;
    }

    void DOFPass::UpdatePassInfo()
    {
        m_passInfo->name = "dof_pipeline";
        m_passInfo->pVertShader = Shader::Create(Path::Assets + "Shaders/Common/Quad.hlsl", vk::ShaderStageFlagBits::eVertex, "mainVS", std::vector<Define>{}, ShaderCodeType::HLSL);
        m_passInfo->pFragShader = Shader::Create(Path::Assets + "Shaders/DepthOfField/DOFPS.hlsl", vk::ShaderStageFlagBits::eFragment, "mainPS", std::vector<Define>{}, ShaderCodeType::HLSL);
        m_passInfo->dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        m_passInfo->cullMode = vk::CullModeFlagBits::eBack;
        m_passInfo->colorBlendAttachments = {PipelineColorBlendAttachmentState::Default};
        m_passInfo->colorFormats = {m_displayRT->GetFormat()};
        m_passInfo->Update();
    }

    void DOFPass::CreateUniforms(CommandBuffer *cmd)
    {
        UpdateDescriptorSets();
    }

    void DOFPass::UpdateDescriptorSets()
    {
        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
        {
            auto *DSet = m_passInfo->GetDescriptors(i)[0];
            DSet->SetImageView(0, m_frameImage->GetSRV(), m_frameImage->GetSampler()->ApiHandle());
            DSet->SetImageView(1, m_depth->GetSRV(), m_depth->GetSampler()->ApiHandle());
            DSet->Update();
        }
    }

    void DOFPass::Draw(CommandBuffer *cmd)
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        std::vector<ImageBarrierInfo> barriers(2);

        barriers[0].image = m_frameImage;
        barriers[0].layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barriers[0].stageFlags = vk::PipelineStageFlagBits2::eFragmentShader;
        barriers[0].accessMask = vk::AccessFlagBits2::eShaderRead;

        barriers[1].image = m_depth;
        barriers[1].layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barriers[1].stageFlags = vk::PipelineStageFlagBits2::eFragmentShader;
        barriers[1].accessMask = vk::AccessFlagBits2::eShaderRead;

        cmd->BeginDebugRegion("DOFPass");
        cmd->CopyImage(m_displayRT, m_frameImage); // Copy RT to image
        cmd->ImageBarriers(barriers);

        cmd->BeginPass(1, m_attachments.data(), "DOF");
        cmd->BindPipeline(*m_passInfo);
        cmd->SetViewport(0.f, 0.f, m_displayRT->GetWidth_f(), m_displayRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_displayRT->GetWidth(), m_displayRT->GetHeight());
        cmd->SetConstantAt(0, gSettings.dof_focus_scale);
        cmd->SetConstantAt(1, gSettings.dof_blur_range);
        cmd->PushConstants();
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();

        cmd->EndDebugRegion();
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
} // namespace pe
