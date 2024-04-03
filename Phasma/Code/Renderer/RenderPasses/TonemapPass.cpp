#include "Renderer/RenderPasses/TonemapPass.h"
#include "GUI/GUI.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Surface.h"
#include "Shader/Shader.h"
#include "Renderer/RHI.h"
#include "Renderer/Queue.h"
#include "Renderer/Command.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/Image.h"
#include "Renderer/Pipeline.h"
#include "Systems/CameraSystem.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    TonemapPass::TonemapPass()
    {
    }

    TonemapPass::~TonemapPass()
    {
    }

    void TonemapPass::Init()
    {
        m_renderQueue = RHII.GetRenderQueue();
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();

        m_displayRT = rs->GetRenderTarget("display");
        m_frameImage = rs->CreateFSSampledImage(false);

        m_attachment = {};
        m_attachment.image = m_displayRT;
        m_attachment.initialLayout = ImageLayout::Attachment;
        m_attachment.finalLayout = ImageLayout::ShaderReadOnly;
    }

    void TonemapPass::UpdatePassInfo()
    {
        PassInfo &info = m_passInfo;

        info.name = "tonemap_pipeline";
        info.pVertShader = Shader::Create("Shaders/Common/Quad.hlsl", ShaderStage::VertexBit);
        info.pFragShader = Shader::Create("Shaders/Tonemap/TonemapPS.hlsl", ShaderStage::FragmentBit);
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {m_displayRT->GetBlendAttachment()};
        info.colorFormats = m_displayRT->GetFormatPtr();
        info.UpdateHash();
    }

    void TonemapPass::CreateUniforms(CommandBuffer *cmd)
    {
        UpdateDescriptorSets();
    }

    void TonemapPass::UpdateDescriptorSets()
    {
        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
        {
            auto *DSet = m_passInfo.GetDescriptors(i)[0];
            DSet->SetImageView(0, m_frameImage->GetSRV(), m_frameImage->GetSampler()->ApiHandle());
            DSet->Update();
        }
    }

    void TonemapPass::Update(Camera *camera)
    {
    }

    CommandBuffer *TonemapPass::Draw()
    {
        CommandBuffer *cmd = CommandBuffer::GetFree(m_renderQueue);
        cmd->Begin();

        cmd->BeginDebugRegion("TonemapPass");

        cmd->CopyImage(m_displayRT, m_frameImage); // Copy RT to image

        std::vector<ImageBarrierInfo> barriers(2);
        barriers[0].image = m_frameImage;
        barriers[0].layout = ImageLayout::ShaderReadOnly;
        barriers[0].stageFlags = PipelineStage::FragmentShaderBit;
        barriers[0].accessMask = Access::ShaderReadBit;
        barriers[1].image = m_displayRT;
        barriers[1].layout = ImageLayout::Attachment;
        barriers[1].stageFlags = PipelineStage::ColorAttachmentOutputBit;
        barriers[1].accessMask = Access::ColorAttachmentWriteBit;

        cmd->ImageBarriers(barriers);
        cmd->BeginPass({m_attachment}, "Tonemap");
        cmd->BindPipeline(m_passInfo);
        cmd->SetViewport(0.f, 0.f, m_displayRT->GetWidth_f(), m_displayRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_displayRT->GetWidth(), m_displayRT->GetHeight());
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();

        cmd->EndDebugRegion();

        cmd->End();
        return cmd;
    }

    void TonemapPass::Resize(uint32_t width, uint32_t height)
    {
        Image::Destroy(m_frameImage);
        Init();
        UpdateDescriptorSets();
    }

    void TonemapPass::Destroy()
    {
        Image::Destroy(m_frameImage);
    }
}
