#include "CullingPass.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/RenderGraph.h"
#include "API/Shader.h"
#include "Camera/Camera.h"
#include "Render/SceneRendererHost.h"
#include "Scene/Scene.h"

namespace pe
{
    namespace
    {
        // Must match cbuffer OcclusionUBO at binding 14 in CullingCS.hlsl (VOXEL_HIZ / HIZ_OCCLUSION).
        struct OcclusionUBO
        {
            mat4 viewProj;
            uint32_t pyrWidth;
            uint32_t pyrHeight;
            uint32_t mipCount;
            float bias;
        };
    } // namespace

    void CullingPass::Init()
    {
        if (!m_sortPassInfo)
            m_sortPassInfo = std::make_unique<PassInfo>();
        AcquirePyramid();
    }

    void CullingPass::AcquirePyramid()
    {
        if (SceneRendererHost *rs = GetActiveSceneRendererHost())
            m_voxelHiZ = rs->GetRenderTarget("voxelHiZ");
    }

    bool CullingPass::VoxelOcclusionActive() const
    {
        return Settings::Get<SceneSettings>().occlusion_culling && m_voxelHiZ && m_scene &&
               m_scene->HasArenaVoxels() && !m_occlusionUniforms.empty();
    }

    void CullingPass::UpdatePassInfo()
    {
        Shader *oldComp = m_passInfo->pCompShader;
        m_passInfo->name = "Culling_pipeline";
        try
        {
            // VOXEL_HIZ compiles the voxel-only temporal occlusion test (binding 13/14). It is a no-op
            // at runtime unless the push flag enableVoxelOcclusion is set (a voxel pyramid was bound).
            m_passInfo->pCompShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Compute/CullingCS.hlsl", .entryPoint = "mainCS", .stage = PE_SHADER_STAGE_COMPUTE, .defines = std::vector<Define>{Define{"VOXEL_HIZ", "1"}}});
            m_passInfo->Update();
        }
        catch (...)
        {
            if (m_passInfo->pCompShader != oldComp)
                Shader::Destroy(m_passInfo->pCompShader);
            m_passInfo->pCompShader = oldComp;
            throw;
        }

        Shader *oldSortComp = m_sortPassInfo->pCompShader;
        m_sortPassInfo->name = "BitonicSort_pipeline";
        try
        {
            m_sortPassInfo->pCompShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Compute/BitonicSortCS.hlsl", .entryPoint = "mainCS", .stage = PE_SHADER_STAGE_COMPUTE, .defines = std::vector<Define>{}});
            m_sortPassInfo->Update();
        }
        catch (...)
        {
            if (m_sortPassInfo->pCompShader != oldSortComp)
                Shader::Destroy(m_sortPassInfo->pCompShader);
            m_sortPassInfo->pCompShader = oldSortComp;
            throw;
        }
    }

    void CullingPass::CreateUniforms(CommandBuffer *cmd)
    {
        (void)cmd;
        m_occlusionUniforms.resize(RHII.GetSwapchainImageCount());
        for (auto &uniform : m_occlusionUniforms)
        {
            uniform = Buffer::Create({
                .size = RHII.AlignUniform(sizeof(OcclusionUBO)),
                .usage = PE_BUFFER_USAGE_UNIFORM_BUFFER,
                .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU,
                .name = "VoxelOcclusion_uniform_buffer",
            });
            uniform->Map();
            uniform->Zero();
            uniform->Flush();
            uniform->Unmap();
        }
    }

    void CullingPass::Update()
    {
        AcquirePyramid();
        if (!m_scene || m_occlusionUniforms.empty())
            return;

        Camera *camera = m_scene->GetActiveCamera();
        if (!camera)
            return;

        // The pyramid this frame samples was built last frame from depth rendered with last frame's
        // view-proj, so the AABB->screen projection must use that, not this frame's camera.
        OcclusionUBO ubo{};
        ubo.viewProj = m_havePrevViewProj ? m_prevViewProj : camera->GetViewProjection();
        if (m_voxelHiZ)
        {
            ubo.pyrWidth = m_voxelHiZ->GetWidth();
            ubo.pyrHeight = m_voxelHiZ->GetHeight();
            ubo.mipCount = m_voxelHiZ->GetMipLevels();
        }
        ubo.bias = Settings::Get<SceneSettings>().occlusion_culling_bias;

        const uint32_t frame = RHII.GetFrameIndex();
        BufferRange range{};
        range.data = &ubo;
        range.size = sizeof(ubo);
        range.offset = 0;
        m_occlusionUniforms[frame]->Copy(1, &range, false);

        m_prevViewProj = camera->GetViewProjection();
        m_havePrevViewProj = true;
    }

    void CullingPass::DeclareInputs(RGBuilder &builder)
    {
        // Read last frame's voxel pyramid (WRITE->READ barrier). Only when it actually feeds the cull.
        if (VoxelOcclusionActive())
            builder.ReadCompute(m_voxelHiZ);
    }

    void CullingPass::ExecutePass(CommandBuffer *cmd)
    {
        if (!m_scene || m_scene->GetMeshCount() == 0)
            return;

        cmd->BeginDebugRegion("CullingPass");

        const uint32_t frame = RHII.GetFrameIndex();
        const bool voxelOcc = VoxelOcclusionActive();
        m_scene->DispatchCulling(cmd, m_passInfo.get(), m_sortPassInfo.get(),
                                 voxelOcc ? m_voxelHiZ : nullptr,
                                 voxelOcc ? m_occlusionUniforms[frame] : nullptr);

        cmd->EndDebugRegion();
    }

    void CullingPass::Resize(uint32_t width, uint32_t height)
    {
        (void)width;
        (void)height;
        AcquirePyramid();
    }

    void CullingPass::Destroy()
    {
        Shader::Destroy(m_passInfo->pCompShader);
        Shader::Destroy(m_sortPassInfo->pCompShader);
        m_sortPassInfo.reset();
        for (Buffer *b : m_occlusionUniforms)
            Buffer::Destroy(b);
        m_occlusionUniforms.clear();
        m_voxelHiZ = nullptr;
    }
} // namespace pe
