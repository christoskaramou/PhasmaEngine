#include "SelectionOutlinePass.h"
#include "DepthPass.h"
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
    namespace
    {
        // Mirrors PushConstants_SelectionOutline in Structures.hlsl.
        struct PushConstants_SelectionOutline
        {
            vec4 color;
            vec4 params0; // x/y = mask texel size, z = solid thickness px, w = inner fade px
            vec4 params1; // x = outer fade px
        };
    } // namespace

    void SelectionOutlinePass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();

        // Composite into the pre-TAA "viewport" color target (not "display"): scheduled before TAA so
        // the temporal resolve de-jitters the outline along with the scene, and the mask (render-scale)
        // matches this target 1:1.
        m_colorRT = rs->GetRenderTarget("viewport");
        m_maskRT = rs->GetRenderTarget("selectionMask");
        if (!m_maskRT)
            m_maskRT = rs->CreateRenderTarget("selectionMask", PE_FORMAT_R8_UNORM);
        m_scene = nullptr;

        m_attachments.resize(1);
        m_attachments[0] = {};
        m_attachments[0].image = m_colorRT;
        m_attachments[0].loadOp = PE_LOAD_OP_LOAD;
        m_attachments[0].storeOp = PE_STORE_OP_STORE;

        if (!m_passInfoMask)
            m_passInfoMask = std::make_shared<PassInfo>();
    }

    void SelectionOutlinePass::UpdatePassInfo()
    {
        // Mask pipeline: draw selected silhouettes into R8, no depth (x-ray), overwrite.
        m_passInfoMask->name = "SelectionOutlineMask_pipeline";
        m_passInfoMask->pVertShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Depth/DepthVS.hlsl",
                                                      .entryPoint = "mainVS",
                                                      .stage = PE_SHADER_STAGE_VERTEX,
                                                      .defines = std::vector<Define>{}});
        m_passInfoMask->pFragShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Outline/OutlineMaskPS.hlsl",
                                                      .entryPoint = "mainPS",
                                                      .stage = PE_SHADER_STAGE_FRAGMENT,
                                                      .defines = std::vector<Define>{}});
        m_passInfoMask->dynamicStates = {PE_DYNAMIC_STATE_VIEWPORT, PE_DYNAMIC_STATE_SCISSOR};
        m_passInfoMask->cullMode = PE_CULL_MODE_NONE;
        m_passInfoMask->colorBlendAttachments = {BlendState::Default};
        m_passInfoMask->colorFormats = {m_maskRT->GetFormat()};
        m_passInfoMask->depthTestEnable = false;
        m_passInfoMask->depthWriteEnable = false;
        m_passInfoMask->stencilTestEnable = false;
        m_passInfoMask->Update();

        // Composite pipeline: fullscreen, sample mask, alpha-blend outline over display.
        m_passInfo->name = "SelectionOutlineComposite_pipeline";
        m_passInfo->pVertShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Common/Quad.hlsl",
                                                  .entryPoint = "mainVS",
                                                  .stage = PE_SHADER_STAGE_VERTEX,
                                                  .defines = std::vector<Define>{}});
        m_passInfo->pFragShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Outline/OutlineCompositePS.hlsl",
                                                  .entryPoint = "mainPS",
                                                  .stage = PE_SHADER_STAGE_FRAGMENT,
                                                  .defines = std::vector<Define>{}});
        m_passInfo->dynamicStates = {PE_DYNAMIC_STATE_VIEWPORT, PE_DYNAMIC_STATE_SCISSOR};
        m_passInfo->cullMode = PE_CULL_MODE_NONE;
        m_passInfo->colorBlendAttachments = {BlendState::Default};
        m_passInfo->colorBlendAttachments[0].blendEnable = true;
        m_passInfo->colorBlendAttachments[0].srcColorBlendFactor = PE_BLEND_FACTOR_SRC_ALPHA;
        m_passInfo->colorBlendAttachments[0].dstColorBlendFactor = PE_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        m_passInfo->colorBlendAttachments[0].colorBlendOp = PE_BLEND_OP_ADD;
        m_passInfo->colorFormats = {m_colorRT->GetFormat()};
        m_passInfo->depthTestEnable = false;
        m_passInfo->depthWriteEnable = false;
        m_passInfo->stencilTestEnable = false;
        m_passInfo->Update();
    }

    void SelectionOutlinePass::UpdateDescriptorSets()
    {
        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
        {
            const auto &sets = m_passInfo->GetDescriptors(i);
            if (sets.empty() || !sets[0])
                continue;
            sets[0]->SetImageView(0, m_maskRT->GetSRV(), m_maskRT->GetSampler());
            sets[0]->Update();
        }
    }

    void SelectionOutlinePass::Update()
    {
        if (!m_passInfoMask)
            return;

        Scene &scene = *GetActiveScene();
        if (scene.GetMeshCount() == 0)
            return;
        if (!Settings::Get<SceneSettings>().selection_outline || !scene.HasSelectedRenderableMeshes())
            return;

        uint32_t frame = RHII.GetFrameIndex();
        const auto &sets = m_passInfoMask->GetDescriptors(frame);
        if (sets.empty() || !sets[0])
            return;

        sets[0]->SetBuffer(0, scene.GetUniforms(frame));
        sets[0]->SetBuffer(1, scene.GetMeshConstants());
        sets[0]->Update();
    }

    void SelectionOutlinePass::ExecutePass(CommandBuffer *cmd)
    {
        PE_ERROR_IF(m_scene == nullptr, "Scene was not set");
        Scene *scene = m_scene;
        m_scene = nullptr;

        const SceneSettings &gs = Settings::Get<SceneSettings>();
        if (!gs.selection_outline || scene->GetMeshCount() == 0 || !scene->HasSelectedRenderableMeshes())
            return;

        PushConstants_DepthPass maskConstants{};
        maskConstants.jointsCount = static_cast<uint32_t>(scene->GetMaxJointCount());

        uint32_t frame = RHII.GetFrameIndex();
        const uint32_t mesh = scene->GetMeshCount();

        cmd->BeginDebugRegion("SelectionOutline");

        // 1) Selected silhouettes -> R8 mask (cleared each frame; empty when nothing selected).
        Attachment maskAttachment{};
        maskAttachment.image = m_maskRT;
        maskAttachment.loadOp = PE_LOAD_OP_CLEAR;
        maskAttachment.storeOp = PE_STORE_OP_STORE;

        cmd->BeginPass(1, &maskAttachment, "SelectionOutlineMask");
        cmd->SetViewport(0.f, 0.f, m_maskRT->GetWidth_f(), m_maskRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_maskRT->GetWidth(), m_maskRT->GetHeight());
        cmd->BindPipeline(*m_passInfoMask);
        cmd->BindIndexBuffer(scene->GetBuffer(), 0);
        cmd->BindVertexBuffer(scene->GetBuffer(), scene->GetPositionsOffset());
        cmd->SetConstants(maskConstants);
        cmd->PushConstants();
        cmd->DrawIndexedIndirectCount(scene->GetIndirectSelected(frame),
                                      0,
                                      scene->GetCullingCountersBuffer(frame),
                                      4 * sizeof(uint32_t),
                                      mesh);
        cmd->EndPass();

        // 2) Mask color-attachment -> shader read for the composite.
        ImageBarrierInfo barrier{};
        barrier.image = m_maskRT;
        barrier.layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.stageFlags = PE_STAGE_FRAGMENT_SHADER;
        barrier.accessMask = PE_ACCESS_SHADER_SAMPLED_READ;
        cmd->ImageBarrier(barrier);

        // 3) Composite outline over the display image.
        PushConstants_SelectionOutline pc{};
        pc.color = vec4(gs.selection_outline_color_r, gs.selection_outline_color_g, gs.selection_outline_color_b,
                        gs.selection_outline_color_a);
        pc.params0 = vec4(1.f / m_maskRT->GetWidth_f(), 1.f / m_maskRT->GetHeight_f(), gs.selection_outline_thickness,
                          gs.selection_outline_inner_fade);
        pc.params1 = vec4(gs.selection_outline_outer_fade, 0.f, 0.f, 0.f);

        cmd->BeginPass(1, m_attachments.data(), "SelectionOutlineComposite");
        cmd->SetViewport(0.f, 0.f, m_colorRT->GetWidth_f(), m_colorRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_colorRT->GetWidth(), m_colorRT->GetHeight());
        cmd->BindPipeline(*m_passInfo);
        cmd->SetConstants(pc);
        cmd->PushConstants();
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();

        cmd->EndDebugRegion();
    }

    void SelectionOutlinePass::Resize(uint32_t width, uint32_t height)
    {
        (void)width;
        (void)height;
        Init();
        UpdateDescriptorSets();
    }

    void SelectionOutlinePass::Destroy()
    {
        if (!m_passInfoMask)
            return;

        Shader::Destroy(m_passInfoMask->pVertShader);
        Shader::Destroy(m_passInfoMask->pFragShader);
        m_passInfoMask.reset();
    }
} // namespace pe
