#include "DepthPass.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/Shader.h"
#include "Scene/Scene.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    void DepthPass::Init()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();
        m_depthStencil = rs->GetDepthStencilTarget("depthStencil");

        m_attachments.resize(1);
        m_attachments[0] = {};
        m_attachments[0].image = m_depthStencil;
        m_attachments[0].loadOp = vk::AttachmentLoadOp::eClear;
        m_attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
    }

    void DepthPass::UpdatePassInfo()
    {
        m_passInfo->name = "DepthPrePass_pipeline";
        m_passInfo->pVertShader = Shader::Create(Path::Assets + "Shaders/Depth/DepthVS.hlsl", vk::ShaderStageFlagBits::eVertex, "mainVS", std::vector<Define>{}, ShaderCodeType::HLSL);
        m_passInfo->pFragShader = Shader::Create(Path::Assets + "Shaders/Depth/DepthPS.hlsl", vk::ShaderStageFlagBits::eFragment, "mainPS", std::vector<Define>{}, ShaderCodeType::HLSL);
        m_passInfo->dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        m_passInfo->cullMode = vk::CullModeFlagBits::eFront;
        m_passInfo->depthFormat = RHII.GetDepthFormat();
        m_passInfo->depthTestEnable = true;
        m_passInfo->depthWriteEnable = true;
        m_passInfo->Update();
    }

    void DepthPass::CreateUniforms(CommandBuffer *cmd)
    {
        // depth uniforms are created with the geometry
    }

    void DepthPass::UpdateDescriptorSets()
    {
        // depth pass descriptor sets are updated with Scene::Update
    }

    void DepthPass::ExecutePass(CommandBuffer *cmd)
    {
        PE_ERROR_IF(m_scene == nullptr, "Scene was not set");

        if (!m_scene->HasOpaqueDrawInfo())
        {
            ClearDepthStencil(cmd);
        }
        else
        {
            PushConstants_DepthPass pushConstants{};
            pushConstants.jointsCount = 0u;

            uint32_t frame = RHII.GetFrameIndex();
            size_t offset = 0;
            uint32_t count = static_cast<uint32_t>(m_scene->GetDrawInfosOpaque().size());

            cmd->BeginPass(1, m_attachments.data(), "DepthPass");
            cmd->SetViewport(0.f, 0.f, m_depthStencil->GetWidth_f(), m_depthStencil->GetHeight_f());
            cmd->SetScissor(0, 0, m_depthStencil->GetWidth(), m_depthStencil->GetHeight());
            cmd->BindPipeline(*m_passInfo);
            cmd->BindIndexBuffer(m_scene->GetBuffer(), 0);
            cmd->BindVertexBuffer(m_scene->GetBuffer(), m_scene->GetPositionsOffset());
            cmd->SetConstants(pushConstants);
            cmd->PushConstants();
            cmd->DrawIndexedIndirect(m_scene->GetIndirect(frame), offset, count);
            cmd->EndPass();
        }

        m_scene = nullptr;
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
} // namespace pe
