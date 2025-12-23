#include "SSRPass.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/Shader.h"
#include "Systems/CameraSystem.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    void SSRPass::Init()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();

        m_ssrRT = rs->GetRenderTarget("ssr");
        m_viewportRT = rs->GetRenderTarget("viewport");
        m_normalRT = rs->GetRenderTarget("normal");
        m_depth = rs->GetDepthStencilTarget("depthStencil");
        m_srmRT = rs->GetRenderTarget("srm");
        m_albedoRT = rs->GetRenderTarget("albedo");
        m_frameImage = rs->CreateFSSampledImage(true);

        m_attachments.resize(1);
        m_attachments[0] = {};
        m_attachments[0].image = m_viewportRT;
        m_attachments[0].loadOp = vk::AttachmentLoadOp::eLoad;
        m_attachments[0].storeOp = vk::AttachmentStoreOp::eStore;

        m_reflectionUBs.resize(RHII.GetSwapchainImageCount());
    }

    void SSRPass::UpdatePassInfo()
    {
        m_passInfo->name = "ssr_pipeline";
        m_passInfo->pVertShader = Shader::Create(Path::Executable + "Assets/Shaders/Common/Quad.hlsl", vk::ShaderStageFlagBits::eVertex, "mainVS", std::vector<Define>{}, ShaderCodeType::HLSL);
        m_passInfo->pFragShader = Shader::Create(Path::Executable + "Assets/Shaders/SSR/SSRPS.hlsl", vk::ShaderStageFlagBits::eFragment, "mainPS", std::vector<Define>{}, ShaderCodeType::HLSL);
        m_passInfo->dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        m_passInfo->cullMode = vk::CullModeFlagBits::eBack;
        m_passInfo->colorBlendAttachments = {PipelineColorBlendAttachmentState::Default};
        m_passInfo->colorFormats = {m_ssrRT->GetFormat()};
        m_passInfo->Update();
    }

    void SSRPass::CreateUniforms(CommandBuffer *cmd)
    {
        for (auto &uniform : m_reflectionUBs)
        {
            uniform = Buffer::Create(
                RHII.AlignUniform(4 * sizeof(mat4)),
                vk::BufferUsageFlagBits2::eUniformBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                "SSR_UB_Reflection_buffer");
            uniform->Map();
            uniform->Zero();
            uniform->Flush();
            uniform->Unmap();
        }

        UpdateDescriptorSets();
    }

    void SSRPass::UpdateDescriptorSets()
    {
        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); ++i)
        {
            auto *DSet = m_passInfo->GetDescriptors(i)[0];
            DSet->SetImageView(0, m_frameImage->GetSRV(), m_frameImage->GetSampler()->ApiHandle());
            DSet->SetImageView(1, m_depth->GetSRV(), m_depth->GetSampler()->ApiHandle());
            DSet->SetImageView(2, m_normalRT->GetSRV(), m_normalRT->GetSampler()->ApiHandle());
            DSet->SetImageView(3, m_srmRT->GetSRV(), m_srmRT->GetSampler()->ApiHandle());
            DSet->SetImageView(4, m_albedoRT->GetSRV(), m_albedoRT->GetSampler()->ApiHandle());
            DSet->SetBuffer(5, m_reflectionUBs[i]);
            DSet->Update();
        }
    }

    void SSRPass::Update()
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        if (gSettings.ssr)
        {
            Camera *camera = GetGlobalSystem<CameraSystem>()->GetCamera(0);
            m_reflectionInput[0][0] = vec4(camera->GetPosition(), 1.0f);
            m_reflectionInput[0][1] = vec4(camera->GetFront(), 1.0f);
            m_reflectionInput[0][2] = vec4();
            m_reflectionInput[0][3] = vec4();
            m_reflectionInput[1] = camera->GetProjection();
            m_reflectionInput[2] = camera->GetView();
            m_reflectionInput[3] = camera->GetInvProjection();

            BufferRange range{};
            range.data = &m_reflectionInput;
            range.size = sizeof(m_reflectionInput);
            range.offset = 0;
            m_reflectionUBs[RHII.GetFrameIndex()]->Copy(1, &range, false);
        }
    }

    void SSRPass::Draw(CommandBuffer *cmd)
    {
        std::vector<ImageBarrierInfo> barriers(5);

        barriers[0].image = m_frameImage;
        barriers[0].layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barriers[0].stageFlags = vk::PipelineStageFlagBits2::eFragmentShader;
        barriers[0].accessMask = vk::AccessFlagBits2::eShaderRead;

        barriers[1].image = m_normalRT;
        barriers[1].layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barriers[1].stageFlags = vk::PipelineStageFlagBits2::eFragmentShader;
        barriers[1].accessMask = vk::AccessFlagBits2::eShaderRead;

        barriers[2].image = m_depth;
        barriers[2].layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barriers[2].stageFlags = vk::PipelineStageFlagBits2::eFragmentShader;
        barriers[2].accessMask = vk::AccessFlagBits2::eShaderRead;

        barriers[3].image = m_srmRT;
        barriers[3].layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barriers[3].stageFlags = vk::PipelineStageFlagBits2::eFragmentShader;
        barriers[3].accessMask = vk::AccessFlagBits2::eShaderRead;

        barriers[4].image = m_albedoRT;
        barriers[4].layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barriers[4].stageFlags = vk::PipelineStageFlagBits2::eFragmentShader;
        barriers[4].accessMask = vk::AccessFlagBits2::eShaderRead;

        cmd->BeginDebugRegion("SSRPass");
        cmd->CopyImage(m_viewportRT, m_frameImage);
        cmd->ImageBarriers(barriers);

        cmd->BeginPass(1, m_attachments.data(), "SSR");
        cmd->BindPipeline(*m_passInfo);
        cmd->SetViewport(0.f, 0.f, m_ssrRT->GetWidth_f(), m_ssrRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_ssrRT->GetWidth(), m_ssrRT->GetHeight());
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();

        cmd->EndDebugRegion();
    }

    void SSRPass::Resize(uint32_t width, uint32_t height)
    {
        Image::Destroy(m_frameImage);
        Init();
        UpdateDescriptorSets();
    }

    void SSRPass::Destroy()
    {
        Image::Destroy(m_frameImage);

        for (auto &uniform : m_reflectionUBs)
            Buffer::Destroy(uniform);
    }
} // namespace pe
