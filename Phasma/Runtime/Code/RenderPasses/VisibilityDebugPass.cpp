#include "VisibilityDebugPass.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/RenderGraph.h"
#include "API/Shader.h"
#include "Render/SceneRendererHost.h"
#include "Render/SceneRenderTargets.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"

namespace pe
{
    namespace
    {
        struct PushConstants_VisibilityDebug
        {
            uint32_t verticesOffset;
            uint32_t linearColor;
        };
    } // namespace

    void VisibilityDebugPass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();
        m_visibility = rs->GetRenderTarget("visibility");
        m_depth = rs->GetDepthStencilTarget("depthStencil");
        m_albedo = rs->GetRenderTarget("albedo");
        m_debug = rs->GetRenderTarget("visibility_debug");
        m_lastGeometryVersion = ~0ull;
    }

    void VisibilityDebugPass::UpdatePassInfo()
    {
        m_passInfo->name = "VisibilityDebug_pipeline";
        m_passInfo->pCompShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/MaterialX/VisibilityDebug.slang",
                                                  .entryPoint = "mainCS",
                                                  .stage = PE_SHADER_STAGE_COMPUTE,
                                                  .defines = std::vector<Define>{}});
        m_passInfo->Update();
    }

    void VisibilityDebugPass::Update()
    {
        m_bound = false;

        Scene *scene = GetActiveScene();
        if (!scene ||
            !m_visibility ||
            !m_depth ||
            !m_albedo ||
            !m_debug ||
            !scene->GetBuffer() ||
            !scene->GetMaterialTable() ||
            !scene->GetMaterialByteBuffer())
        {
            return;
        }

        // The bindless table changes with the geometry version, as DepthPass rebinds it.
        if (scene->GetGeometryVersion() != m_lastGeometryVersion)
        {
            m_lastGeometryVersion = scene->GetGeometryVersion();

            for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); ++i)
            {
                Descriptor *textures = m_passInfo->GetDescriptors(i)[1];
                textures->SetSampler(0, scene->GetDefaultSampler());
                textures->SetImageViews(1, scene->GetImageViews());
                textures->Update();
            }
        }

        const uint32_t frame = RHII.GetFrameIndex();
        Descriptor *set = m_passInfo->GetDescriptors(frame)[0];

        set->SetImageView(0, m_visibility->GetSRV());
        set->SetImageView(1, m_depth->GetSRV());
        set->SetImageView(2, m_albedo->GetSRV());
        set->SetBuffer(3, scene->GetBuffer());
        set->SetBuffer(4, scene->GetUniforms(frame));
        set->SetBuffer(5, scene->GetMeshConstants());
        set->SetBuffer(6, scene->GetMaterialTable());
        set->SetBuffer(7, scene->GetMaterialByteBuffer());
        set->SetImageView(8, m_debug->GetUAV(0));
        set->Update();

        m_bound = true;
    }

    void VisibilityDebugPass::DeclareInputs(RGBuilder &builder)
    {
        if (!m_visibility || !m_debug)
            return;

        builder.ReadCompute(m_visibility);
        builder.ReadCompute(m_depth);
        builder.ReadCompute(m_albedo);
        builder.WriteCompute(m_debug);
    }

    void VisibilityDebugPass::DeclareOutputs(RGBuilder &builder)
    {
        if (m_debug)
            builder.OutputCustom(m_debug, PE_IMAGE_LAYOUT_GENERAL, PE_STAGE_COMPUTE_SHADER, PE_ACCESS_SHADER_WRITE);
    }

    void VisibilityDebugPass::ExecutePass(CommandBuffer *cmd)
    {
        Scene *scene = GetActiveScene();
        if (!scene || !m_visibility || !m_debug || !m_bound)
            return;

        PushConstants_VisibilityDebug pc{};
        pc.verticesOffset = static_cast<uint32_t>(scene->GetVerticesOffset());
        pc.linearColor = SceneUsesLinearColor() ? 1u : 0u;

        cmd->BeginDebugRegion("VisibilityDebugPass");
        cmd->BindPipeline(*m_passInfo);
        cmd->SetConstants(pc);
        cmd->PushConstants();
        // Four threads per pixel (one 2x2 quad), 8x8 threads per group.
        cmd->Dispatch((m_debug->GetWidth() * 2u + 7u) / 8u, (m_debug->GetHeight() * 2u + 7u) / 8u, 1);
        cmd->EndDebugRegion();
    }

    void VisibilityDebugPass::Resize(uint32_t width, uint32_t height)
    {
        (void)width;
        (void)height;
        Init();
    }

    void VisibilityDebugPass::Destroy()
    {
        if (m_passInfo)
            Shader::Destroy(m_passInfo->pCompShader);

        m_visibility = m_depth = m_albedo = m_debug = nullptr;
    }
} // namespace pe
