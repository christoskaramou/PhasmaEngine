#include "Renderer/RenderPasses/AabbsPass.h"
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
    AabbsPass::AabbsPass()
    {
        m_geometry = nullptr;
    }

    AabbsPass::~AabbsPass()
    {
    }

    void AabbsPass::Init()
    {
        m_renderQueue = RHII.GetRenderQueue();
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();

        m_viewportRT = rs->GetRenderTarget("viewport");
        m_depthRT = rs->GetDepthStencilTarget("depthStencil");

        attachments.resize(2);
        attachments[0] = {};
        attachments[0].image = m_viewportRT;
        attachments[0].loadOp = AttachmentLoadOp::Load;
        attachments[0].storeOp = AttachmentStoreOp::Store;
        attachments[0].initialLayout = ImageLayout::Attachment;
        attachments[0].finalLayout = ImageLayout::ShaderReadOnly;
        attachments[1] = {};
        attachments[1].image = m_depthRT;
        attachments[1].loadOp = AttachmentLoadOp::Load;
        attachments[1].storeOp = AttachmentStoreOp::Store;
        attachments[1].initialLayout = ImageLayout::Attachment;
        attachments[1].finalLayout = ImageLayout::ShaderReadOnly;
    }

    void AabbsPass::UpdatePassInfo()
    {
        m_passInfo.name = "AABBs_pipeline";
        m_passInfo.pVertShader = Shader::Create("Shaders/Utilities/AABBsVS.hlsl", ShaderStage::VertexBit);
        m_passInfo.pFragShader = Shader::Create("Shaders/Utilities/AABBsPS.hlsl", ShaderStage::FragmentBit);
        m_passInfo.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor, DynamicState::LineWidth, DynamicState::DepthTestEnable, DynamicState::DepthWriteEnable};
        m_passInfo.topology = PrimitiveTopology::LineList;
        m_passInfo.colorBlendAttachments = {m_viewportRT->GetBlendAttachment()};
        m_passInfo.colorFormats = {m_viewportRT->GetFormat()};
        m_passInfo.UpdateHash();
    }

    void AabbsPass::CreateUniforms(CommandBuffer *cmd)
    {
        UpdateDescriptorSets();
    }

    void AabbsPass::UpdateDescriptorSets()
    {
    }

    void AabbsPass::Update(Camera *camera)
    {
    }

    CommandBuffer *AabbsPass::Draw()
    {
        PE_ERROR_IF(m_geometry == nullptr, "Geometry was not set");

        if (!m_geometry->HasDrawInfo())
            return nullptr;

        CommandBuffer *cmd = CommandBuffer::GetFree(m_renderQueue);
        cmd->Begin();
        cmd->BeginDebugRegion("Aabbs");

        Camera &camera = *GetGlobalSystem<CameraSystem>()->GetCamera(0);
        ShadowPass *shadows = GetGlobalComponent<ShadowPass>();

        struct PushConstants_AABB
        {
            vec2 projJitter;
            uint32_t meshIndex;
            uint32_t color;
        };

        std::vector<ImageBarrierInfo> barrierInfo{};
        barrierInfo.resize(2);
        
        barrierInfo[0].image = m_viewportRT;
        barrierInfo[0].layout = ImageLayout::Attachment;
        barrierInfo[0].stageFlags = PipelineStage::ColorAttachmentOutputBit;
        barrierInfo[0].accessMask = Access::ColorAttachmentWriteBit;

        barrierInfo[1].image = m_depthRT;
        barrierInfo[1].layout = ImageLayout::Attachment;
        barrierInfo[1].stageFlags = PipelineStage::EarlyFragmentTestsBit;
        barrierInfo[1].accessMask = Access::DepthStencilAttachmentWriteBit;

        cmd->ImageBarriers(barrierInfo);

        PushConstants_AABB constants{};
        constants.projJitter = camera.GetProjJitter();

        auto &gSettings = Settings::Get<GlobalSettings>();

        cmd->BeginPass(attachments, "AabbsPass");
        cmd->BindIndexBuffer(m_geometry->GetBuffer(), m_geometry->GetAabbIndicesOffset());
        cmd->BindVertexBuffer(m_geometry->GetBuffer(), m_geometry->GetAabbVerticesOffset());
        cmd->SetViewport(0.f, 0.f, m_viewportRT->GetWidth_f(), m_viewportRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_viewportRT->GetWidth(), m_viewportRT->GetHeight());
        cmd->SetLineWidth(1.f + gSettings.render_scale * gSettings.render_scale);
        cmd->SetDepthTestEnable(gSettings.aabbs_depth_dware);
        cmd->SetDepthWriteEnable(false);
        cmd->BindPipeline(m_passInfo);

        // Lambda to draw from drawInfos
        auto DrawFromInfos = [&](const auto &drawInfos)
        {
            for (auto &drawInfo : drawInfos)
            {
                ModelGltf &model = *drawInfo.second.model;

                int node = drawInfo.second.node;
                int mesh = model.nodes[node].mesh;
                if (mesh < 0)
                    continue;

                int primitive = drawInfo.second.primitive;
                PrimitiveInfo &primitiveInfo = model.m_meshesInfo[mesh].primitivesInfo[primitive];

                constants.meshIndex = GetUboDataOffset(model.m_meshesInfo[mesh].dataOffset);
                constants.color = primitiveInfo.aabbColor;

                cmd->SetConstants(constants);
                cmd->PushConstants();

                cmd->DrawIndexed(24, 1, 0, static_cast<int>(primitiveInfo.aabbVertexOffset), 0);
            }
        };

        DrawFromInfos(m_geometry->GetDrawInfosOpaque());
        DrawFromInfos(m_geometry->GetDrawInfosAlphaCut());
        DrawFromInfos(m_geometry->GetDrawInfosAlphaBlend());

        cmd->EndPass();
        cmd->EndDebugRegion();
        cmd->End();

        m_geometry = nullptr;
        return cmd;
    }

    void AabbsPass::Resize(uint32_t width, uint32_t height)
    {
        Init();
        UpdateDescriptorSets();
    }

    void AabbsPass::Destroy()
    {
    }
}
