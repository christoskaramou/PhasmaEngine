#include "DepthPass.h"
#include "Scene/Model.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Surface.h"
#include "Renderer/Shader.h"
#include "GUI/GUI.h"
#include "tinygltf/stb_image.h"
#include "Renderer/Reflection.h"
#include "Renderer/RHI.h"
#include "Renderer/Queue.h"
#include "Renderer/Command.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Descriptor.h"
#include "Systems/LightSystem.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Image.h"
#include "Renderer/Buffer.h"
#include "Renderer/Pipeline.h"
#include "Systems/RendererSystem.h"
#include "Scene/Geometry.h"

namespace pe
{
    void DepthPass::Init()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();
        m_depthStencil = rs->GetDepthStencilTarget("depthStencil");

        m_attachments.resize(1);
        m_attachments[0] = {};
        m_attachments[0].image = m_depthStencil;
        m_attachments[0].loadOp = AttachmentLoadOp::Clear;
        m_attachments[0].storeOp = AttachmentStoreOp::Store;
    }

    void DepthPass::UpdatePassInfo()
    {
        m_passInfo->name = "DepthPrePass_pipeline";
        m_passInfo->pVertShader = Shader::Create(Path::Executable + "Assets/Shaders/Depth/DepthVS.hlsl", ShaderStage::VertexBit, "mainVS", std::vector<Define>{}, ShaderCodeType::HLSL);
        m_passInfo->pFragShader = Shader::Create(Path::Executable + "Assets/Shaders/Depth/DepthPS.hlsl", ShaderStage::FragmentBit, "mainPS", std::vector<Define>{}, ShaderCodeType::HLSL);
        m_passInfo->dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        m_passInfo->cullMode = CullMode::Front;
        m_passInfo->depthFormat = RHII.GetDepthFormat();
        m_passInfo->depthTestEnable = true;
        m_passInfo->depthWriteEnable = true;
        m_passInfo->ReflectDescriptors();
        m_passInfo->UpdateHash();
    }

    void DepthPass::CreateUniforms(CommandBuffer *cmd)
    {
        // depth uniforms are created with the geometry
    }

    void DepthPass::UpdateDescriptorSets()
    {
        // depth pass descriptor sets are updated with Scene::Update
    }

    void DepthPass::Draw(CommandBuffer *cmd)
    {
        PE_ERROR_IF(m_geometry == nullptr, "Geometry was not set");

        if (!m_geometry->HasOpaqueDrawInfo())
        {
            ClearDepthStencil(cmd);
        }
        else
        {
            PushConstants_DepthPass pushConstants{};
            pushConstants.jointsCount = 0u;

            uint32_t frame = RHII.GetFrameIndex();
            size_t offset = 0;
            uint32_t count = static_cast<uint32_t>(m_geometry->GetDrawInfosOpaque().size());

            cmd->BeginPass(1, m_attachments.data(), "DepthPass");
            cmd->SetViewport(0.f, 0.f, m_depthStencil->GetWidth_f(), m_depthStencil->GetHeight_f());
            cmd->SetScissor(0, 0, m_depthStencil->GetWidth(), m_depthStencil->GetHeight());
            cmd->BindPipeline(*m_passInfo);
            cmd->BindIndexBuffer(m_geometry->GetBuffer(), 0);
            cmd->BindVertexBuffer(m_geometry->GetBuffer(), m_geometry->GetPositionsOffset());
            cmd->SetConstants(pushConstants);
            cmd->PushConstants();
            cmd->DrawIndexedIndirect(m_geometry->GetIndirect(frame), offset, count, sizeof(DrawIndexedIndirectCommand));
            cmd->EndPass();
        }

        m_geometry = nullptr;
    }

    void DepthPass::Resize(uint32_t width, uint32_t height)
    {
        Init();
        UpdateDescriptorSets();
    }

    void DepthPass::ClearDepthStencil(CommandBuffer *cmd)
    {
        cmd->ClearDepthStencils({m_depthStencil});
    }

    void DepthPass::Destroy()
    {
    }
}
