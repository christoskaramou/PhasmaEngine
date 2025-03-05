#include "SSRPass.h"
#include "Renderer/Command.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Image.h"
#include "Renderer/Pipeline.h"
#include "Renderer/Buffer.h"
#include "Renderer/RHI.h"
#include "Renderer/Shader.h"
#include "Systems/RendererSystem.h"
#include "Systems/CameraSystem.h"

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
        m_attachments[0].loadOp = AttachmentLoadOp::Load;
        m_attachments[0].storeOp = AttachmentStoreOp::Store;
    }

    void SSRPass::UpdatePassInfo()
    {
        m_passInfo->name = "ssr_pipeline";
        m_passInfo->pVertShader = Shader::Create(Path::Assets + "Shaders/Common/Quad.hlsl", ShaderStage::VertexBit, std::vector<Define>{}, ShaderCodeType::HLSL);
        m_passInfo->pFragShader = Shader::Create(Path::Assets + "Shaders/SSR/SSRPS.hlsl", ShaderStage::FragmentBit, std::vector<Define>{}, ShaderCodeType::HLSL);
        m_passInfo->dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        m_passInfo->cullMode = CullMode::Back;
        m_passInfo->colorBlendAttachments = {PipelineColorBlendAttachmentState::Default};
        m_passInfo->colorFormats = {m_ssrRT->GetFormat()};
        m_passInfo->ReflectDescriptors();
        m_passInfo->UpdateHash();
    }

    void SSRPass::CreateUniforms(CommandBuffer *cmd)
    {
        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; ++i)
        {
            m_UBReflection[i] = Buffer::Create(
                RHII.AlignUniform(4 * sizeof(mat4)), // * SWAPCHAIN_IMAGES,
                BufferUsage::UniformBufferBit,
                AllocationCreate::HostAccessSequentialWriteBit,
                "SSR_UB_Reflection_buffer");
            m_UBReflection[i]->Map();
            m_UBReflection[i]->Zero();
            m_UBReflection[i]->Flush();
            m_UBReflection[i]->Unmap();
        }

        UpdateDescriptorSets();
    }

    void SSRPass::UpdateDescriptorSets()
    {
        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; ++i)
        {
            auto *DSet = m_passInfo->GetDescriptors(i)[0];
            DSet->SetImageView(0, m_frameImage->GetSRV(), m_frameImage->GetSampler()->ApiHandle());
            DSet->SetImageView(1, m_depth->GetSRV(), m_depth->GetSampler()->ApiHandle());
            DSet->SetImageView(2, m_normalRT->GetSRV(), m_normalRT->GetSampler()->ApiHandle());
            DSet->SetImageView(3, m_srmRT->GetSRV(), m_srmRT->GetSampler()->ApiHandle());
            DSet->SetImageView(4, m_albedoRT->GetSRV(), m_albedoRT->GetSampler()->ApiHandle());
            DSet->SetBuffer(5, m_UBReflection[i]);
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
            range.offset = 0; // RHII.GetFrameDynamicOffset(UBReflection->Size(), RHII.GetFrameIndex());
            m_UBReflection[RHII.GetFrameIndex()]->Copy(1, &range, false);
        }
    }

    void SSRPass::Draw(CommandBuffer *cmd)
    {
        std::vector<ImageBarrierInfo> barriers(5);

        barriers[0].image = m_frameImage;
        barriers[0].layout = ImageLayout::ShaderReadOnly;
        barriers[0].stageFlags = PipelineStage::FragmentShaderBit;
        barriers[0].accessMask = Access::ShaderReadBit;

        barriers[1].image = m_normalRT;
        barriers[1].layout = ImageLayout::ShaderReadOnly;
        barriers[1].stageFlags = PipelineStage::FragmentShaderBit;
        barriers[1].accessMask = Access::ShaderReadBit;

        barriers[2].image = m_depth;
        barriers[2].layout = ImageLayout::ShaderReadOnly;
        barriers[2].stageFlags = PipelineStage::FragmentShaderBit;
        barriers[2].accessMask = Access::ShaderReadBit;

        barriers[3].image = m_srmRT;
        barriers[3].layout = ImageLayout::ShaderReadOnly;
        barriers[3].stageFlags = PipelineStage::FragmentShaderBit;
        barriers[3].accessMask = Access::ShaderReadBit;

        barriers[4].image = m_albedoRT;
        barriers[4].layout = ImageLayout::ShaderReadOnly;
        barriers[4].stageFlags = PipelineStage::FragmentShaderBit;
        barriers[4].accessMask = Access::ShaderReadBit;

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
        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; ++i)
            Buffer::Destroy(m_UBReflection[i]);
    }
}
