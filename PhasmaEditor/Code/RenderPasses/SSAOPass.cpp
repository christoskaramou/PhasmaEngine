#include "SSAOPass.h"
#include "SSAOCacaoBackend.h"
#include "API/Command.h"
#include "Camera/Camera.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    void SSAOPass::Init()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();
        m_ssaoRT = rs->GetRenderTarget("ssao");
        m_normalRT = rs->GetRenderTarget("normal");
        m_depth = rs->GetDepthStencilTarget("depthStencil");

        SSAOCacaoBackend::Init(m_depth, m_normalRT, m_ssaoRT);
    }

    void SSAOPass::Update()
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        if (gSettings.ssao)
        {
            Camera *camera = GetGlobalSystem<RendererSystem>()->GetScene().GetActiveCamera();
            m_proj = camera->GetProjection();

            static const mat4 biasMat = mat4(
                1.0, 0.0, 0.0, 0.0,
                0.0, -1.0, 0.0, 0.0,
                0.0, 0.0, 1.0, 0.0,
                0.0, 0.0, 0.0, 1.0);

            m_normalsToView = transpose(biasMat * camera->GetInvView());
            SSAOCacaoBackend::UpdateSettings();
        }
    }

    void SSAOPass::DeclareInputs(RGBuilder &builder)
    {
        builder.ReadCompute(m_normalRT);
        builder.ReadCompute(m_depth);
        builder.WriteCompute(m_ssaoRT);
    }

    void SSAOPass::DeclareOutputs(RGBuilder &builder)
    {
        builder.OutputCustom(m_ssaoRT,
                             PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             PE_STAGE_COMPUTE_SHADER,
                             PE_ACCESS_SHADER_READ);
    }

    void SSAOPass::ExecutePass(CommandBuffer *cmd)
    {
        MemoryBarrierInfo barrier{};
        barrier.srcAccessMask = PE_ACCESS_SHADER_WRITE;
        barrier.dstAccessMask = PE_ACCESS_NONE;
        barrier.srcStageMask = PE_STAGE_COMPUTE_SHADER;
        barrier.dstStageMask = PE_STAGE_ALL_COMMANDS;

        cmd->BeginDebugRegion("SSAOPass");
        cmd->MemoryBarrier(barrier);
        SSAOCacaoBackend::Draw(cmd, m_ssaoRT, m_proj, m_normalsToView);
        cmd->EndDebugRegion();
    }

    void SSAOPass::Resize(uint32_t width, uint32_t height)
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();
        m_ssaoRT = rs->GetRenderTarget("ssao");
        m_normalRT = rs->GetRenderTarget("normal");
        m_depth = rs->GetDepthStencilTarget("depthStencil");

        SSAOCacaoBackend::Resize(m_depth, m_normalRT, m_ssaoRT);
    }

    void SSAOPass::Destroy()
    {
        SSAOCacaoBackend::Destroy();
    }
} // namespace pe
