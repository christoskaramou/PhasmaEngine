#include "RTDepthResolvePass.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/RenderGraph.h"
#include "API/Shader.h"
#include "Render/SceneRendererHost.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"

namespace pe
{
    void RTDepthResolvePass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();
        m_rtDepth = rs->GetRenderTarget("rtDepth"); // created by RayTracingPass::Init, which runs first
        m_depthStencil = rs->GetDepthStencilRT();
        m_scene = nullptr;

        m_attachments.resize(1);
        m_attachments[0] = {};
        m_attachments[0].image = m_depthStencil;
        m_attachments[0].loadOp = PE_LOAD_OP_CLEAR;
        m_attachments[0].storeOp = PE_STORE_OP_STORE;
    }

    void RTDepthResolvePass::UpdatePassInfo()
    {
        m_passInfo->name = "RTDepthResolve_pipeline";
        m_passInfo->pVertShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Common/Quad.hlsl", .entryPoint = "mainVS", .stage = PE_SHADER_STAGE_VERTEX, .defines = std::vector<Define>{}});
        m_passInfo->pFragShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Utilities/RTDepthResolvePS.hlsl", .entryPoint = "mainPS", .stage = PE_SHADER_STAGE_FRAGMENT, .defines = std::vector<Define>{}});
        m_passInfo->dynamicStates = {PE_DYNAMIC_STATE_VIEWPORT, PE_DYNAMIC_STATE_SCISSOR};
        m_passInfo->cullMode = PE_CULL_MODE_NONE;
        m_passInfo->depthFormat = RHII.GetDepthFormat();
        m_passInfo->depthTestEnable = true; // depth writes only happen while testing is enabled
        m_passInfo->depthWriteEnable = true;
        m_passInfo->depthCompareOp = PE_COMPARE_OP_ALWAYS;
        m_passInfo->Update();
    }

    void RTDepthResolvePass::CreateUniforms(CommandBuffer *cmd)
    {
        UpdateDescriptorSets();
    }

    void RTDepthResolvePass::UpdateDescriptorSets()
    {
        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); ++i)
        {
            const auto &sets = m_passInfo->GetDescriptors(i);
            PE_ERROR_IF(sets.empty() || !sets[0], "RTDepthResolvePass: descriptor set 0 was not reflected");
            auto *DSet = sets[0];
            DSet->SetImageView(0, m_rtDepth->GetSRV(), m_rtDepth->GetSampler());
            DSet->Update();
        }
    }

    void RTDepthResolvePass::Update()
    {
    }

    void RTDepthResolvePass::DeclareInputs(RGBuilder &builder)
    {
        builder.Read(m_rtDepth);
    }

    void RTDepthResolvePass::ExecutePass(CommandBuffer *cmd)
    {
        cmd->BeginDebugRegion("RTDepthResolvePass");

        cmd->BeginPass(1, m_attachments.data(), "RTDepthResolvePass");

        cmd->SetViewport(0.f, 0.f, m_depthStencil->GetWidth_f(), m_depthStencil->GetHeight_f());
        cmd->SetScissor(0, 0, m_depthStencil->GetWidth(), m_depthStencil->GetHeight());

        cmd->BindPipeline(*m_passInfo);

        // Fullscreen triangle
        cmd->Draw(3, 1, 0, 0);

        cmd->EndPass();

        cmd->EndDebugRegion();

        m_scene = nullptr;
    }

    void RTDepthResolvePass::Resize(uint32_t width, uint32_t height)
    {
        Init();
        UpdateDescriptorSets();
    }

    void RTDepthResolvePass::Destroy()
    {
    }
} // namespace pe
