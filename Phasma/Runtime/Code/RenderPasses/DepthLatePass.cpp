#include "DepthLatePass.h"
#include "DepthPass.h" // PushConstants_DepthPass
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/Shader.h"
#include "Render/SceneRendererHost.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"

namespace pe
{
    void DepthLatePass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();
        m_depthStencil = rs->GetDepthStencilTarget("depthStencil");

        m_attachments.resize(1);
        m_attachments[0] = {};
        m_attachments[0].image = m_depthStencil;
        m_attachments[0].loadOp = PE_LOAD_OP_LOAD; // preserve set-A depth from DepthPass@200
        m_attachments[0].storeOp = PE_STORE_OP_STORE;

        if (!m_passInfoDS)
            m_passInfoDS = std::make_shared<PassInfo>();
    }

    void DepthLatePass::UpdatePassInfo()
    {
        auto configureDepthPass = [](PassInfo &passInfo, const std::string &name, PeCullMode cullMode)
        {
            passInfo.name = name;
            passInfo.pVertShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Depth/DepthVS.hlsl", .entryPoint = "mainVS", .stage = PE_SHADER_STAGE_VERTEX, .defines = std::vector<Define>{}});
            passInfo.pFragShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Depth/DepthPS.hlsl", .entryPoint = "mainPS", .stage = PE_SHADER_STAGE_FRAGMENT, .defines = std::vector<Define>{}});
            passInfo.dynamicStates = {PE_DYNAMIC_STATE_VIEWPORT, PE_DYNAMIC_STATE_SCISSOR};
            passInfo.cullMode = cullMode;
            passInfo.depthFormat = RHII.GetDepthFormat();
            passInfo.depthTestEnable = true;
            passInfo.depthWriteEnable = true;
            passInfo.Update();
        };

        configureDepthPass(*m_passInfo, "DepthLatePass_pipeline", PE_CULL_MODE_FRONT);

        if (!m_passInfoDS)
            m_passInfoDS = std::make_shared<PassInfo>();
        configureDepthPass(*m_passInfoDS, "DepthLatePass_DS_pipeline", PE_CULL_MODE_NONE);
    }

    void DepthLatePass::Update()
    {
        Scene &scene = *GetActiveScene();
        uint32_t frame = RHII.GetFrameIndex();

        uint64_t geoVersion = scene.GetGeometryVersion();
        if (geoVersion != m_lastGeometryVersion)
        {
            m_lastGeometryVersion = geoVersion;

            for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
            {
                for (PassInfo *passInfo : {m_passInfo.get(), m_passInfoDS.get()})
                {
                    if (!passInfo)
                        continue;

                    const auto &dpSets = passInfo->GetDescriptors(i);
                    Descriptor *dpSetTextures = dpSets[1];
                    dpSetTextures->SetBuffer(0, scene.GetMeshConstants());
                    dpSetTextures->SetSampler(1, scene.GetDefaultSampler());
                    dpSetTextures->SetImageViews(2, scene.GetImageViews());
                    dpSetTextures->Update();
                }
            }
        }

        if (scene.GetMeshCount() > 0)
        {
            for (PassInfo *passInfo : {m_passInfo.get(), m_passInfoDS.get()})
            {
                if (!passInfo)
                    continue;

                const auto &sets = passInfo->GetDescriptors(frame);
                Descriptor *setUniforms = sets[0];
                setUniforms->SetBuffer(0, scene.GetUniforms(frame));
                setUniforms->SetBuffer(1, scene.GetMeshConstants());
                setUniforms->Update();
            }
        }
    }

    void DepthLatePass::UpdateDescriptorSets()
    {
        m_lastGeometryVersion = ~0ull;
    }

    void DepthLatePass::ExecutePass(CommandBuffer *cmd)
    {
        PE_ERROR_IF(m_scene == nullptr, "Scene was not set");

        if (m_scene->GetMeshCount() == 0)
        {
            m_scene = nullptr;
            return; // set A already cleared the depth in DepthPass@200; nothing to add
        }

        PushConstants_DepthPass pushConstants{};
        pushConstants.jointsCount = static_cast<uint32_t>(m_scene->GetMaxJointCount());

        uint32_t frame = RHII.GetFrameIndex();

        cmd->BeginPass(1, m_attachments.data(), "DepthLatePass");
        cmd->SetViewport(0.f, 0.f, m_depthStencil->GetWidth_f(), m_depthStencil->GetHeight_f());
        cmd->SetScissor(0, 0, m_depthStencil->GetWidth(), m_depthStencil->GetHeight());
        cmd->BindPipeline(*m_passInfo);
        cmd->BindIndexBuffer(m_scene->GetBuffer(), 0);
        cmd->BindVertexBuffer(m_scene->GetBuffer(), m_scene->GetPositionsOffset());
        m_scene->BindDrawIdBuffer(cmd);
        cmd->SetConstants(pushConstants);
        cmd->PushConstants();
        // set B (newly-disoccluded), single-sided
        cmd->DrawIndexedIndirectCount(m_scene->GetOccOpaqueSSB(frame), 0, m_scene->GetOccCountersB(frame), 0 * sizeof(uint32_t), m_scene->GetMeshCount());
        cmd->DrawIndexedIndirectCount(m_scene->GetOccAlphaCutSSB(frame), 0, m_scene->GetOccCountersB(frame), 1 * sizeof(uint32_t), m_scene->GetMeshCount());

        cmd->BindPipeline(*m_passInfoDS);
        // set B, double-sided
        cmd->DrawIndexedIndirectCount(m_scene->GetOccOpaqueDSB(frame), 0, m_scene->GetOccCountersB(frame), 5 * sizeof(uint32_t), m_scene->GetMeshCount());
        cmd->DrawIndexedIndirectCount(m_scene->GetOccAlphaCutDSB(frame), 0, m_scene->GetOccCountersB(frame), 6 * sizeof(uint32_t), m_scene->GetMeshCount());
        cmd->EndPass();

        m_scene = nullptr;
    }

    void DepthLatePass::Resize(uint32_t width, uint32_t height)
    {
        Init();
        UpdateDescriptorSets();
    }

    void DepthLatePass::Destroy()
    {
        if (!m_passInfoDS)
            return;

        Shader::Destroy(m_passInfoDS->pVertShader);
        Shader::Destroy(m_passInfoDS->pFragShader);
        m_passInfoDS.reset();
    }
} // namespace pe
