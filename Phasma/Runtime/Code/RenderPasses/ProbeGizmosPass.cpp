#include "ProbeGizmosPass.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/RenderGraph.h"
#include "API/Shader.h"
#include "Camera/Camera.h"
#include "Render/SceneRendererHost.h"
#include "RenderPasses/GlobalIlluminationPass.h"
#include "RenderPasses/ScreenProbePass.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"

namespace pe
{
    namespace
    {
        // Structures.hlsl PushConstants_ProbeGizmos.
        struct PushConstants_ProbeGizmos
        {
            vec2 projJitter;
            vec2 invDisplaySize;
            uint32_t mode;
            uint32_t tilesX;
            float radius;
            float padding;
        };

        GlobalIlluminationPass *ActiveProbeVolume()
        {
            auto *gi = GetGlobalComponent<GlobalIlluminationPass>();
            const uint32_t frame = RHII.GetFrameIndex();
            const bool ready = gi &&
                               gi->GetProbeUniform(frame) &&
                               gi->GetIrradianceHistory() &&
                               gi->GetProbeCount() > 0;
            return ready ? gi : nullptr;
        }

        // Only when the pass ran this frame (passes update in parallel, so this is read once they all have).
        ScreenProbePass *ActiveScreenProbes()
        {
            if (!Settings::Get<SceneSettings>().draw_screen_probes)
                return nullptr;

            auto *screen = GetGlobalComponent<ScreenProbePass>();
            const bool ran = screen && screen->GetProbePositions() && screen->GetFilteredSH() &&
                             screen->GetData().frame == static_cast<uint32_t>(RHII.GetFrameCounter());
            return ran ? screen : nullptr;
        }
    } // namespace

    void ProbeGizmosPass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();

        m_displayRT = rs->GetDisplayRT();
        m_depthRT = rs->GetDepthStencilTarget("depthStencil");
        m_scene = nullptr;

        m_attachments.resize(1);
        m_attachments[0] = {};
        m_attachments[0].image = m_displayRT;
        m_attachments[0].loadOp = PE_LOAD_OP_LOAD;
        m_attachments[0].storeOp = PE_STORE_OP_STORE;
    }

    void ProbeGizmosPass::UpdatePassInfo()
    {
        const std::string source = Path::RuntimeAssets + "Shaders/Utilities/ProbeGizmos.hlsl";
        m_passInfo->name = "ProbeGizmos_pipeline";
        m_passInfo->pVertShader = Shader::Create({.sourcePath = source,
                                                  .entryPoint = "mainVS",
                                                  .stage = PE_SHADER_STAGE_VERTEX});
        m_passInfo->pFragShader = Shader::Create({.sourcePath = source,
                                                  .entryPoint = "mainPS",
                                                  .stage = PE_SHADER_STAGE_FRAGMENT});
        m_passInfo->dynamicStates = {PE_DYNAMIC_STATE_VIEWPORT, PE_DYNAMIC_STATE_SCISSOR};
        m_passInfo->cullMode = PE_CULL_MODE_NONE;
        m_passInfo->colorBlendAttachments = {BlendState::Default};
        m_passInfo->colorFormats = {m_displayRT->GetFormat()};
        m_passInfo->depthTestEnable = false;
        m_passInfo->depthWriteEnable = false;
        m_passInfo->Update();
    }

    void ProbeGizmosPass::DeclareInputs(RGBuilder &builder)
    {
        builder.Read(m_depthRT);
        if (GlobalIlluminationPass *gi = ActiveProbeVolume())
            builder.Read(gi->GetIrradianceHistory());

        if (ScreenProbePass *screen = ActiveScreenProbes())
        {
            builder.Read(screen->GetProbePositions());
            builder.Read(screen->GetFilteredSH());
        }
    }

    void ProbeGizmosPass::ExecutePass(CommandBuffer *cmd)
    {
        PE_ERROR_IF(m_scene == nullptr, "Scene was not set");
        GlobalIlluminationPass *gi = ActiveProbeVolume();
        if (!gi || m_scene->GetCameras().empty())
        {
            m_scene = nullptr;
            return;
        }

        // Bound here, not in Update: whether the screen probes ran this frame is known only after every Update.
        const uint32_t frame = RHII.GetFrameIndex();
        ScreenProbePass *screen = ActiveScreenProbes();
        Image *irradiance = gi->GetIrradianceHistory();
        const auto &sets = m_passInfo->GetDescriptors(frame);
        PE_ERROR_IF(sets.empty() || !sets[0], "ProbeGizmosPass: descriptor set 0 was not reflected");

        Descriptor *set = sets[0];
        set->SetBuffer(0, m_scene->GetUniforms(frame));
        set->SetImageView(1, irradiance->GetSRV());
        set->SetImageView(3, m_depthRT->GetSRV(), m_depthRT->GetSampler());
        set->SetImageView(4, screen ? screen->GetProbePositions()->GetSRV() : irradiance->GetSRV());
        set->SetImageView(5, screen ? screen->GetFilteredSH()->GetSRV() : irradiance->GetSRV());
        set->SetBuffer(8, gi->GetProbeUniform(frame));
        set->SetSampler(9, RequireActiveSceneRendererHost().GetIBL_LUT()->GetSampler());
        set->Update();

        PushConstants_ProbeGizmos constants{};
        constants.projJitter = m_scene->GetActiveCamera()->GetProjJitter();
        constants.invDisplaySize = vec2(1.f / m_displayRT->GetWidth_f(), 1.f / m_displayRT->GetHeight_f());

        cmd->BeginDebugRegion("ProbeGizmos");
        cmd->BeginPass(1, m_attachments.data(), "ProbeGizmosPass");
        cmd->SetViewport(0.f, 0.f, m_displayRT->GetWidth_f(), m_displayRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_displayRT->GetWidth(), m_displayRT->GetHeight());
        cmd->BindPipeline(*m_passInfo);

        if (Settings::Get<SceneSettings>().draw_gi_probes)
        {
            constants.mode = 0;
            constants.radius = 0.1f; // of the cascade spacing
            cmd->SetConstants(constants);
            cmd->PushConstants();
            cmd->Draw(6, gi->GetProbeCount(), 0, 0);
        }

        if (screen)
        {
            const ScreenProbeUniforms &data = screen->GetData();
            constants.mode = 1;
            constants.tilesX = data.tilesX;
            constants.radius = 0.0025f; // of the view distance: about 2 px at 1000 px and a 60 degree field of view
            cmd->SetConstants(constants);
            cmd->PushConstants();
            cmd->Draw(6, data.tilesX * (data.tilesY + data.adaptiveRows), 0, 0);
        }

        cmd->EndPass();
        cmd->EndDebugRegion();

        m_scene = nullptr;
    }

    void ProbeGizmosPass::Resize(uint32_t width, uint32_t height)
    {
        Init();
    }
} // namespace pe
