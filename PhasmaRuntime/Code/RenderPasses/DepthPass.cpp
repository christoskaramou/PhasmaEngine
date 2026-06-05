#include "DepthPass.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/Shader.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Render/SceneRendererHost.h"

namespace pe
{
    void DepthPass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();
        m_depthStencil = rs->GetDepthStencilTarget("depthStencil");

        m_attachments.resize(1);
        m_attachments[0] = {};
        m_attachments[0].image = m_depthStencil;
        m_attachments[0].loadOp = PE_LOAD_OP_CLEAR;
        m_attachments[0].storeOp = PE_STORE_OP_STORE;
    }

    void DepthPass::UpdatePassInfo()
    {
        m_passInfo->name = "DepthPrePass_pipeline";
        m_passInfo->pVertShader = Shader::Create({.sourcePath = Path::Assets + "Shaders/Depth/DepthVS.hlsl", .entryPoint = "mainVS", .stage = PE_SHADER_STAGE_VERTEX, .defines = std::vector<Define>{}});
        m_passInfo->pFragShader = Shader::Create({.sourcePath = Path::Assets + "Shaders/Depth/DepthPS.hlsl", .entryPoint = "mainPS", .stage = PE_SHADER_STAGE_FRAGMENT, .defines = std::vector<Define>{}});
        m_passInfo->dynamicStates = {PE_DYNAMIC_STATE_VIEWPORT, PE_DYNAMIC_STATE_SCISSOR};
        m_passInfo->cullMode = PE_CULL_MODE_FRONT;
        m_passInfo->depthFormat = RHII.GetDepthFormat();
        m_passInfo->depthTestEnable = true;
        m_passInfo->depthWriteEnable = true;
        m_passInfo->Update();
    }

    void DepthPass::Update()
    {
        Scene &scene = *GetActiveScene();
        uint32_t frame = RHII.GetFrameIndex();

        uint64_t geoVersion = scene.GetGeometryVersion();
        if (geoVersion != m_lastGeometryVersion)
        {
            m_lastGeometryVersion = geoVersion;

            for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
            {
                const auto &dpSets = m_passInfo->GetDescriptors(i);
                Descriptor *dpSetTextures = dpSets[1];
                dpSetTextures->SetBuffer(0, scene.GetMeshConstants());
                dpSetTextures->SetSampler(1, scene.GetDefaultSampler());
                dpSetTextures->SetImageViews(2, scene.GetImageViews());
                dpSetTextures->Update();
            }
        }

        if (scene.GetMeshCount() > 0)
        {
            const auto &sets = m_passInfo->GetDescriptors(frame);
            Descriptor *setUniforms = sets[0];
            setUniforms->SetBuffer(0, scene.GetUniforms(frame));
            setUniforms->SetBuffer(1, scene.GetMeshConstants());
            setUniforms->Update();
        }
    }

    void DepthPass::CreateUniforms(CommandBuffer *cmd)
    {
        // depth uniforms are created with the geometry
    }

    void DepthPass::UpdateDescriptorSets()
    {
        // Force geometry-dependent descriptor rebind on next Update()
        m_lastGeometryVersion = ~0ull;
    }

    void DepthPass::ExecutePass(CommandBuffer *cmd)
    {
        PE_ERROR_IF(m_scene == nullptr, "Scene was not set");

        if (m_scene->GetMeshCount() == 0)
        {
            ClearDepthStencil(cmd);
        }
        else
        {
            PushConstants_DepthPass pushConstants{};
            pushConstants.jointsCount = static_cast<uint32_t>(m_scene->GetMaxJointCount());

            uint32_t frame = RHII.GetFrameIndex();

            cmd->BeginPass(1, m_attachments.data(), "DepthPass");
            cmd->SetViewport(0.f, 0.f, m_depthStencil->GetWidth_f(), m_depthStencil->GetHeight_f());
            cmd->SetScissor(0, 0, m_depthStencil->GetWidth(), m_depthStencil->GetHeight());
            cmd->BindPipeline(*m_passInfo);
            cmd->BindIndexBuffer(m_scene->GetBuffer(), 0);
            cmd->BindVertexBuffer(m_scene->GetBuffer(), m_scene->GetPositionsOffset());
            cmd->SetConstants(pushConstants);
            cmd->PushConstants();
            cmd->DrawIndexedIndirectCount(m_scene->GetIndirectOpaqueSS(frame), 0, m_scene->GetCullingCountersBuffer(frame), 0 * sizeof(uint32_t), m_scene->GetMeshCount());
            cmd->DrawIndexedIndirectCount(m_scene->GetIndirectAlphaCutSS(frame), 0, m_scene->GetCullingCountersBuffer(frame), 1 * sizeof(uint32_t), m_scene->GetMeshCount());
            cmd->DrawIndexedIndirectCount(m_scene->GetIndirectOpaqueDS(frame), 0, m_scene->GetCullingCountersBuffer(frame), 5 * sizeof(uint32_t), m_scene->GetMeshCount());
            cmd->DrawIndexedIndirectCount(m_scene->GetIndirectAlphaCutDS(frame), 0, m_scene->GetCullingCountersBuffer(frame), 6 * sizeof(uint32_t), m_scene->GetMeshCount());
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
