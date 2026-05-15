#include "GridPass.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/Shader.h"
#include "Camera/Camera.h"
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
        m_attachments[0].loadOp = PE_LOAD_OP_LOAD;
        m_attachments[0].storeOp = PE_STORE_OP_STORE;
    }

    void GridPass::UpdatePassInfo()
    {
        m_passInfo->name = "Grid_pipeline";
        m_passInfo->pVertShader = Shader::Create({.sourcePath = Path::Assets + "Shaders/Common/Quad.hlsl", .entryPoint = "mainVS", .stage = PE_SHADER_STAGE_VERTEX, .defines = std::vector<Define>{}});
        m_passInfo->pFragShader = Shader::Create({.sourcePath = Path::Assets + "Shaders/Utilities/GridPS.hlsl", .entryPoint = "mainPS", .stage = PE_SHADER_STAGE_FRAGMENT, .defines = std::vector<Define>{}});

        // Dynamic States
        m_passInfo->dynamicStates = {PE_DYNAMIC_STATE_VIEWPORT, PE_DYNAMIC_STATE_SCISSOR, PE_DYNAMIC_STATE_DEPTH_TEST_ENABLE, PE_DYNAMIC_STATE_DEPTH_WRITE_ENABLE};

        m_passInfo->cullMode = PE_CULL_MODE_NONE;

        // Blending (Alpha Blend)
        m_passInfo->colorBlendAttachments = {BlendState::Default};
        m_passInfo->colorBlendAttachments[0].blendEnable = true;
        m_passInfo->colorBlendAttachments[0].srcColorBlendFactor = PE_BLEND_FACTOR_SRC_ALPHA;
        m_passInfo->colorBlendAttachments[0].dstColorBlendFactor = PE_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        m_passInfo->colorBlendAttachments[0].colorBlendOp = PE_BLEND_OP_ADD;

        m_passInfo->colorFormats = {m_viewportRT->GetFormat()};
        m_passInfo->depthTestEnable = false;
        m_passInfo->depthWriteEnable = false;

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
            const auto &sets = m_passInfo->GetDescriptors(i);
            PE_ERROR_IF(sets.empty() || !sets[0], "GridPass: descriptor set 0 was not reflected");
            auto *DSet = sets[0];
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

    void GridPass::DeclareInputs(RGBuilder &builder)
    {
        builder.Read(m_depthRT);
    }

    void GridPass::ExecutePass(CommandBuffer *cmd)
    {
        PE_ERROR_IF(m_scene == nullptr, "Scene was not set");

        struct PushConstants_Grid
        {
            vec2 projJitter;
            vec2 padding;
        } constants{};

        if (!m_scene->GetCameras().empty())
            constants.projJitter = m_scene->GetActiveCamera()->GetProjJitter();

        cmd->BeginDebugRegion("GridPass");

        cmd->BeginPass(1, m_attachments.data(), "GridPass");

        cmd->SetViewport(0.f, 0.f, m_viewportRT->GetWidth_f(), m_viewportRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_viewportRT->GetWidth(), m_viewportRT->GetHeight());
        cmd->SetDepthTestEnable(false);
        cmd->SetDepthWriteEnable(false);

        cmd->BindPipeline(*m_passInfo);
        cmd->SetConstants(constants);
        cmd->PushConstants();

        // Fullscreen triangle
        cmd->Draw(3, 1, 0, 0);

        cmd->EndPass();

        cmd->EndDebugRegion();

        m_scene = nullptr;
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
