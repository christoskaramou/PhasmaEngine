#include "GridPass.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/Shader.h"
#include "Scene/Scene.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    void GridPass::Init()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();

        m_viewportRT = rs->GetRenderTarget("display");
        m_depthRT = rs->GetDepthStencilTarget("depthStencil");
        m_scene = nullptr;

        m_attachments.resize(1);
        m_attachments[0] = {};
        m_attachments[0].image = m_viewportRT;
        m_attachments[0].loadOp = vk::AttachmentLoadOp::eLoad;
        m_attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
    }

    void GridPass::UpdatePassInfo()
    {
        m_passInfo->name = "Grid_pipeline";
        m_passInfo->pVertShader = Shader::Create(Path::Assets + "Shaders/Common/Quad.hlsl", vk::ShaderStageFlagBits::eVertex, "mainVS", std::vector<Define>{}, ShaderCodeType::HLSL);
        m_passInfo->pFragShader = Shader::Create(Path::Assets + "Shaders/Utilities/GridPS.hlsl", vk::ShaderStageFlagBits::eFragment, "mainPS", std::vector<Define>{}, ShaderCodeType::HLSL);

        // Dynamic States
        m_passInfo->dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor, vk::DynamicState::eDepthTestEnable, vk::DynamicState::eDepthWriteEnable};

        m_passInfo->topology = vk::PrimitiveTopology::eTriangleList; // Fullscreen triangle
        m_passInfo->cullMode = vk::CullModeFlagBits::eNone;
        m_passInfo->polygonMode = vk::PolygonMode::eFill;

        // Blending (Alpha Blend)
        m_passInfo->colorBlendAttachments = {PipelineColorBlendAttachmentState::Default};
        m_passInfo->colorBlendAttachments[0].blendEnable = VK_TRUE;
        m_passInfo->colorBlendAttachments[0].srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        m_passInfo->colorBlendAttachments[0].dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        m_passInfo->colorBlendAttachments[0].colorBlendOp = vk::BlendOp::eAdd;

        m_passInfo->colorFormats = {m_viewportRT->GetFormat()};

        m_passInfo->Update();
    }

    void GridPass::CreateUniforms(CommandBuffer *cmd)
    {
        UpdateDescriptorSets();
    }

    void GridPass::UpdateDescriptorSets()
    {
        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); ++i)
        {
            auto *DSet = m_passInfo->GetDescriptors(i)[0];
            DSet->SetImageView(1, m_depthRT->GetSRV(), m_depthRT->GetSampler());
            DSet->Update();
        }
    }

    void GridPass::Update()
    {
        Scene &scene = GetGlobalSystem<RendererSystem>()->GetScene();
        uint32_t frame = RHII.GetFrameIndex();
        auto &sets = m_passInfo->GetDescriptors(frame);
        if (!sets.empty())
        {
            Descriptor *setUniforms = sets[0];
            setUniforms->SetBuffer(0, scene.GetUniforms(frame));
            setUniforms->Update();
        }
    }

    void GridPass::ExecutePass(CommandBuffer *cmd)
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        Scene &scene = GetGlobalSystem<RendererSystem>()->GetScene();

        cmd->BeginDebugRegion("GridPass");

        ImageBarrierInfo barrier{};
        barrier.image = m_depthRT;
        barrier.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.stageFlags = vk::PipelineStageFlagBits2::eFragmentShader;
        barrier.accessMask = vk::AccessFlagBits2::eShaderRead;
        cmd->ImageBarrier(barrier);

        cmd->BeginPass(1, m_attachments.data(), "GridPass");

        cmd->SetViewport(0.f, 0.f, m_viewportRT->GetWidth_f(), m_viewportRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_viewportRT->GetWidth(), m_viewportRT->GetHeight());
        cmd->SetDepthTestEnable(false);
        cmd->SetDepthWriteEnable(false);

        cmd->BindPipeline(*m_passInfo);

        // Fullscreen triangle
        cmd->Draw(3, 1, 0, 0);

        cmd->EndPass();

        cmd->EndDebugRegion();
    }

    void GridPass::Resize(uint32_t width, uint32_t height)
    {
        Init();
        UpdateDescriptorSets();
    }

    void GridPass::Destroy()
    {
    }
} // namespace pe
