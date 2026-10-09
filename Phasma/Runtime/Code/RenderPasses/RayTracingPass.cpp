#include "RayTracingPass.h"
#include "GlobalIlluminationPass.h"
#include "API/AccelerationStructure.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/RenderGraph.h"
#include "API/Shader.h"
#include "Camera/Camera.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Render/SceneRendererHost.h"
#include "Skybox/Skybox.h"

namespace pe
{
    void RayTracingPass::Init()
    {
        m_scene = nullptr;
        auto *rs = &RequireActiveSceneRendererHost();
        m_display = rs->GetViewportRT();
        // Primary-hit depth for RTDepthResolvePass. Registry targets are destroyed wholesale on
        // resize, so create-by-name here is always fresh-sized.
        m_rtDepth = rs->CreateRenderTarget("rtDepth", PE_FORMAT_R32_SFLOAT);
        m_uniforms.resize(RHII.GetSwapchainImageCount());
    }

    void RayTracingPass::UpdatePassInfo()
    {
        std::vector<Define> defines;
        if (m_display->GetFormat() == PE_FORMAT_R16G16B16A16_SFLOAT)
            defines.push_back({"HDR_SCENE", "1"});
        if (m_probeTracing)
            defines.push_back({"GI_PROBES", "1"});
        else
            defines.push_back({"GI_SHADING", "1"});

        // Shaders
        Shader *rayGen = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/RayTracing/RayTrace.hlsl", .entryPoint = "raygeneration", .stage = PE_SHADER_STAGE_RAYGEN_KHR, .defines = defines});
        Shader *closestHit = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/RayTracing/RayTrace.hlsl", .entryPoint = "closesthit", .stage = PE_SHADER_STAGE_CLOSEST_HIT_KHR, .defines = defines});
        Shader *anyHit = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/RayTracing/RayTrace.hlsl", .entryPoint = "anyhit", .stage = PE_SHADER_STAGE_ANY_HIT_KHR, .defines = defines});
        Shader *miss = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/RayTracing/RayTrace.hlsl", .entryPoint = "miss", .stage = PE_SHADER_STAGE_MISS_KHR, .defines = defines});

        m_passInfo->name = m_probeTracing ? "GI_ProbeTracing" : "RayTracingPipeline";
        m_passInfo->acceleration.rayGen = rayGen;
        m_passInfo->acceleration.miss = {miss};
        m_passInfo->acceleration.hitGroups = {{.closestHit = closestHit, .anyHit = anyHit}};
        m_passInfo->acceleration.maxRecursionDepth = 4;
        m_passInfo->Update();
    }

    void RayTracingPass::CreateUniforms(CommandBuffer *cmd)
    {
        for (auto &uniform : m_uniforms)
        {
            uniform = Buffer::Create({
                .size = RHII.AlignUniform(sizeof(RayTracingPassUBO)),
                .usage = PE_BUFFER_USAGE_UNIFORM_BUFFER,
                .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU,
                .name = "RayTracing_uniform_buffer",
            });
            uniform->Map();
            uniform->Zero();
            uniform->Flush();
            uniform->Unmap();
        }

        if (!m_probeTracing)
        {
            m_giFallbackUniform = Buffer::Create({.size = RHII.AlignUniform(128), .usage = PE_BUFFER_USAGE_UNIFORM_BUFFER, .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU, .name = "GI_DisabledUniform"});
            m_giFallbackUniform->Map();
            m_giFallbackUniform->Zero();
            m_giFallbackUniform->Flush();
            m_giFallbackUniform->Unmap();
        }
        UpdateDescriptorSets();
    }

    void RayTracingPass::UpdateDescriptorSets()
    {
        Scene &scene = *GetActiveScene();
        if (!scene.GetTLAS())
            return;

        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
        {
            auto &descriptors = m_passInfo->GetDescriptors(i);

            // All bindings go in Set 0 (raytracing shaders share the same descriptor set layout)
            if (descriptors.size() > 0 && descriptors[0])
            {
                auto *rs = &RequireActiveSceneRendererHost();
                auto *skybox = rs->GetSkyBox().GetCubeMap();
                auto *desc = descriptors[0];
                desc->SetAccelerationStructure(0, scene.GetTLAS());
                desc->SetImageView(1, m_display->GetUAV(0));
                desc->SetBuffer(2, scene.GetUniforms(i));
                desc->SetBuffer(3, scene.GetMeshConstants());
                desc->SetSampler(4, m_display->GetSampler());
                desc->SetBuffer(6, scene.GetBuffer());
                desc->SetBuffer(7, scene.GetMeshInfoBuffer());
                desc->SetImageView(8, skybox->GetSRV());
                auto *ibl_brdf_lut = rs->GetIBL_LUT();
                desc->SetImageView(9, ibl_brdf_lut->GetSRV());
                desc->SetImageView(10, rs->GetDepthStencilRT()->GetSRV());
                desc->SetBuffer(11, scene.GetMaterialTable());
                desc->SetImageViews(12, scene.GetImageViews());
                desc->Update();
            }

            // Set 1
            if (descriptors.size() > 1 && descriptors[1])
            {
                auto *desc = descriptors[1];
                desc->SetBuffer(0, scene.GetLightUniform(i));
                desc->SetBuffer(1, m_uniforms[i]);
                desc->SetBuffer(2, scene.GetLightStorage(i));
                m_boundLightStorage.resize(RHII.GetSwapchainImageCount());
                m_boundLightStorage[i] = scene.GetLightStorage(i);
                if (desc->HasBinding(3))
                    desc->SetImageView(3, m_rtDepth->GetUAV(0));
                desc->Update();
            }
            if (!m_probeTracing && descriptors.size() > 2 && descriptors[2])
            {
                auto *desc = descriptors[2];
                auto *fallback = RequireActiveSceneRendererHost().GetIBL_LUT();
                desc->SetBuffer(0, m_giFallbackUniform);
                desc->SetImageView(1, fallback->GetSRV());
                desc->SetImageView(2, fallback->GetSRV());
                desc->SetSampler(3, fallback->GetSampler());
                desc->Update();
            }
        }
    }

    void RayTracingPass::Update()
    {
        Camera *camera = GetActiveScene()->GetActiveCamera();
        auto &gSettings = Settings::Get<SceneSettings>();
        auto &pp = ActivePostProcessProfile();

        RayTracingPassUBO ubo{};
        ubo.invViewProj = camera->GetInvViewProjection();
        ubo.invView = camera->GetInvView();
        ubo.invProj = camera->GetInvProjection();
        ubo.camPos = vec4(camera->GetPosition(), 1.0f);
        ubo.shadows = gSettings.shadows ? 1 : 0;
        ubo.use_Disney_PBR = gSettings.use_Disney_PBR ? 1 : 0;
        // Scale by the per-effect volume blend factor (matches LightPass) so a trigger zone fades IBL
        // in/out smoothly instead of snapping when its IBL toggle flips.
        ubo.ibl_intensity = pp.IBL_intensity * std::clamp(ActivePostProcessBlend().IBL, 0.0f, 1.0f);
        ubo.IBL = pp.IBL ? 1 : 0;
        ubo.renderMode = m_probeTracing ? 2u : static_cast<uint32_t>(gSettings.render_mode);
        if (m_probeTracing && !pp.IBL)
            ubo.ibl_intensity = 0.f;
        ubo.orthographicCamera = camera->IsOrthographic() ? 1u : 0u;

        BufferRange range{};
        range.data = &ubo;
        range.size = sizeof(ubo);
        range.offset = 0;
        m_uniforms[RHII.GetFrameIndex()]->Copy(1, &range, false);

        Scene &scene = *GetActiveScene();

        // Check if geometry changed (new model loaded) or TLAS changed
        AccelerationStructure *tlas = scene.GetTLAS();
        uint64_t geoVersion = scene.GetGeometryVersion();
        bool tlasChanged = tlas && m_tlas != tlas;
        bool geoChanged = geoVersion != m_lastGeometryVersion;
        const uint32_t frame = RHII.GetFrameIndex();
        if (!tlasChanged && frame < m_boundLightStorage.size() && m_boundLightStorage[frame] != scene.GetLightStorage(frame))
            UpdateDescriptorSets(); // the enabled lights outgrew this frame's buffer

        if (tlasChanged || geoChanged)
        {
            if (tlasChanged)
            {
                UpdateDescriptorSets();
                m_tlas = tlas;
            }
            if (geoChanged)
            {
                m_lastGeometryVersion = geoVersion;

                // Update ALL frames' descriptors since buffers changed
                for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
                {
                    const auto &rtSets = m_passInfo->GetDescriptors(i);
                    if (rtSets.size() > 0 && rtSets[0])
                    {
                        Descriptor *rtSet0 = rtSets[0];
                        rtSet0->SetAccelerationStructure(0, scene.GetTLAS());
                        rtSet0->SetBuffer(2, scene.GetUniforms(i));
                        rtSet0->SetBuffer(3, scene.GetMeshConstants());
                        rtSet0->SetSampler(4, m_display->GetSampler());
                        rtSet0->SetBuffer(6, scene.GetBuffer());
                        rtSet0->SetBuffer(7, scene.GetMeshInfoBuffer());
                        rtSet0->SetBuffer(11, scene.GetMaterialTable());
                        rtSet0->SetImageViews(12, scene.GetImageViews());
                        rtSet0->Update();
                    }
                }
            }
        }
        if (!m_probeTracing)
        {
            const auto &sets = m_passInfo->GetDescriptors(frame);
            auto *gi = GetGlobalComponent<GlobalIlluminationPass>();
            const bool activeGI = gSettings.global_illumination && gi && gi->GetProbeUniform(frame);
            auto *fallback = RequireActiveSceneRendererHost().GetIBL_LUT();
            if (sets.size() > 2 && sets[2])
            {
                auto *uniform = activeGI ? gi->GetProbeUniform(frame) : m_giFallbackUniform;
                const auto &bound = sets[2]->GetBoundResources();
                if (std::any_of(bound.begin(), bound.end(), [uniform](const auto &info)
                                { return info.binding == 0 && !info.buffers.empty() && info.buffers[0] == uniform; }))
                    return;
                sets[2]->SetBuffer(0, uniform);
                sets[2]->SetImageView(1, activeGI ? gi->GetIrradianceHistory()->GetSRV() : fallback->GetSRV());
                sets[2]->SetImageView(2, activeGI ? gi->GetDistanceHistory()->GetSRV() : fallback->GetSRV());
                sets[2]->SetSampler(3, fallback->GetSampler());
                sets[2]->Update();
            }
        }
    }

    void RayTracingPass::DeclareInputs(RGBuilder &builder)
    {
        if (!m_scene || !m_scene->GetTLAS())
            return;

        Image *depth = RequireActiveSceneRendererHost().GetDepthStencilRT();
        builder.WriteRayTracing(m_display);
        builder.WriteRayTracing(m_rtDepth);
        builder.ReadRayTracing(depth);
        auto *gi = GetGlobalComponent<GlobalIlluminationPass>();
        if (!m_probeTracing && Settings::Get<SceneSettings>().global_illumination && gi && gi->GetIrradianceHistory())
        {
            builder.ReadRayTracing(gi->GetIrradianceHistory());
            builder.ReadRayTracing(gi->GetDistanceHistory());
        }
    }

    void RayTracingPass::DeclareOutputs(RGBuilder &builder)
    {
        if (!m_scene || !m_scene->GetTLAS())
            return;

        builder.OutputCustom(m_display, PE_IMAGE_LAYOUT_GENERAL,
                             PE_STAGE_RAY_TRACING_SHADER_KHR,
                             PE_ACCESS_SHADER_WRITE);
        builder.OutputCustom(m_rtDepth, PE_IMAGE_LAYOUT_GENERAL,
                             PE_STAGE_RAY_TRACING_SHADER_KHR,
                             PE_ACCESS_SHADER_WRITE);
    }

    void RayTracingPass::ExecutePass(CommandBuffer *cmd)
    {
        if (!m_scene)
            return;

        AccelerationStructure *tlas = m_scene->GetTLAS();
        if (!tlas)
            return;

        // Update TLAS if any transform changed
        m_scene->UpdateTLASTransformations(cmd);

        cmd->BeginDebugRegion("RayTracingPass");
        cmd->BindPipeline(*m_passInfo);
        Scene &scene = *GetActiveScene();
        const uint32_t frame = RHII.GetFrameIndex();

        std::vector<BufferBarrierInfo> readBarriers;
        readBarriers.reserve(8);
        auto addReadBarrier = [&](Buffer *buffer, PeAccessFlags accessMask)
        {
            if (!buffer)
                return;

            BufferBarrierInfo barrier{};
            barrier.buffer = buffer;
            barrier.stageMask = PE_STAGE_RAY_TRACING_SHADER_KHR;
            barrier.accessMask = accessMask;
            readBarriers.push_back(barrier);
        };

        addReadBarrier(scene.GetUniforms(frame), PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_STORAGE_READ);
        addReadBarrier(scene.GetMeshConstants(), PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_STORAGE_READ);
        addReadBarrier(scene.GetBuffer(), PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_STORAGE_READ);
        addReadBarrier(scene.GetMeshInfoBuffer(), PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_STORAGE_READ);
        addReadBarrier(scene.GetMaterialTable(), PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_STORAGE_READ);
        addReadBarrier(scene.GetLightUniform(frame), PE_ACCESS_UNIFORM_READ);
        addReadBarrier(m_uniforms[frame], PE_ACCESS_UNIFORM_READ);
        addReadBarrier(scene.GetLightStorage(frame), PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_STORAGE_READ);
        if (!readBarriers.empty())
            cmd->BufferBarriers(readBarriers);

        cmd->SetConstantAt(0, (uint32_t)scene.GetPointLights().size());
        cmd->SetConstantAt(1, (uint32_t)scene.GetSpotLights().size());
        cmd->SetConstantAt(2, (uint32_t)scene.GetAreaLights().size());
        cmd->SetConstantAt(3, (uint32_t)scene.GetMaxJointCount());
        cmd->PushConstants();
        cmd->TraceRays(m_display->GetWidth(), m_display->GetHeight(), 1);
        cmd->EndDebugRegion();

        m_scene = nullptr;
    }

    void RayTracingPass::Resize(uint32_t width, uint32_t height)
    {
        Init();
        UpdateDescriptorSets();
    }

    void RayTracingPass::Destroy()
    {
        for (auto &uniform : m_uniforms)
            Buffer::Destroy(uniform);
        Buffer::Destroy(m_giFallbackUniform);
        m_giFallbackUniform = nullptr;
    }

} // namespace pe
