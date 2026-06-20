#include "OcclusionCullingPass.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/RHI.h"
#include "API/RenderGraph.h"
#include "API/Shader.h"
#include "Camera/Camera.h"
#include "Render/SceneRendererHost.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"

namespace pe
{
    namespace
    {
        // Must match cbuffer OcclusionUBO at binding 14 in CullingCS.hlsl (HIZ_OCCLUSION).
        struct OcclusionUBO
        {
            mat4 viewProj;
            uint32_t pyrWidth;
            uint32_t pyrHeight;
            uint32_t mipCount;
            float bias;
        };
    } // namespace

    void OcclusionCullingPass::Init()
    {
        AcquirePyramid();
    }

    void OcclusionCullingPass::AcquirePyramid()
    {
        if (SceneRendererHost *rs = GetActiveSceneRendererHost())
            m_pyramid = rs->GetRenderTarget("depthPyramid");
    }

    void OcclusionCullingPass::UpdatePassInfo()
    {
        // Phase-2 cull: Hi-Z occlusion test (binding 13/14) + visibility rewrite (binding 15),
        // emitting only the newly-disoccluded opaque set B. No transparents/selected, no sort.
        m_passInfo->name = "CullPhase2_pipeline";
        m_passInfo->pCompShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Compute/CullingCS.hlsl",
                                                  .entryPoint = "mainCS",
                                                  .stage = PE_SHADER_STAGE_COMPUTE,
                                                  .defines = std::vector<Define>{Define{"HIZ_OCCLUSION", "1"}, Define{"PHASE2", "1"}}});
        m_passInfo->Update();
    }

    void OcclusionCullingPass::CreateUniforms(CommandBuffer *cmd)
    {
        (void)cmd;
        m_occlusionUniforms.resize(RHII.GetSwapchainImageCount());
        for (auto &uniform : m_occlusionUniforms)
        {
            uniform = Buffer::Create({
                .size = RHII.AlignUniform(sizeof(OcclusionUBO)),
                .usage = PE_BUFFER_USAGE_UNIFORM_BUFFER,
                .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU,
                .name = "OcclusionCulling_uniform_buffer",
            });
            uniform->Map();
            uniform->Zero();
            uniform->Flush();
            uniform->Unmap();
        }
    }

    void OcclusionCullingPass::Update()
    {
        AcquirePyramid();
        if (!m_scene || !m_pyramid || m_occlusionUniforms.empty())
            return;

        Camera *camera = m_scene->GetActiveCamera();
        if (!camera)
            return;

        OcclusionUBO ubo{};
        ubo.viewProj = camera->GetViewProjection();
        ubo.pyrWidth = m_pyramid->GetWidth();
        ubo.pyrHeight = m_pyramid->GetHeight();
        ubo.mipCount = m_pyramid->GetMipLevels();
        ubo.bias = Settings::Get<GlobalSettings>().occlusion_culling_bias;

        const uint32_t frame = RHII.GetFrameIndex();
        BufferRange range{};
        range.data = &ubo;
        range.size = sizeof(ubo);
        range.offset = 0;
        m_occlusionUniforms[frame]->Copy(1, &range, false);
    }

    void OcclusionCullingPass::DeclareInputs(RGBuilder &builder)
    {
        // Order this cull after the depth-pyramid build (250) wrote the pyramid.
        if (m_pyramid)
            builder.ReadCompute(m_pyramid);
    }

    void OcclusionCullingPass::ExecutePass(CommandBuffer *cmd)
    {
        if (!m_scene || m_scene->GetMeshCount() == 0 || !m_pyramid || m_occlusionUniforms.empty())
            return;

        const uint32_t frame = RHII.GetFrameIndex();
        cmd->BeginDebugRegion("CullPhase2Pass");
        m_scene->DispatchCullingPhase(cmd, m_passInfo.get(), Scene::CullPhase::Phase2, m_pyramid, m_occlusionUniforms[frame]);
        cmd->EndDebugRegion();
    }

    void OcclusionCullingPass::Resize(uint32_t width, uint32_t height)
    {
        (void)width;
        (void)height;
        AcquirePyramid();
    }

    void OcclusionCullingPass::Destroy()
    {
        if (m_passInfo)
            Shader::Destroy(m_passInfo->pCompShader);
        for (Buffer *b : m_occlusionUniforms)
            Buffer::Destroy(b);
        m_occlusionUniforms.clear();
        m_pyramid = nullptr;
    }
} // namespace pe
