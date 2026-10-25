#include "RayTracingPass.h"
#include "GlobalIlluminationPass.h"
#include "API/AccelerationStructure.h"
#include "Render/SceneRenderTargets.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/RenderGraph.h"
#include "API/Shader.h"
#include "Camera/Camera.h"
#include "Scene/OpenPBRMaterial.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Render/SceneRendererHost.h"
#include "Skybox/Skybox.h"

namespace pe
{
    namespace
    {
        // What a trace pipeline build reads from its pass, copied so a worker thread can build it.
        struct TraceDesc
        {
            std::string name;
            std::vector<Define> defines;
            std::vector<Define> hitDefines;
            std::vector<const OpenPBRModule *> hitModules; // the registry never frees a module
        };

        TraceDesc MakeTraceDesc(bool hdr,
                                bool probeTracing,
                                bool screenProbes,
                                bool reflections,
                                const std::vector<uint32_t> &modules)
        {
            TraceDesc desc;
            if (hdr)
                desc.defines.push_back({"HDR_SCENE", "1"});
            if (probeTracing)
                desc.defines.push_back({"GI_PROBES", "1"});
            else
                desc.defines.push_back({"GI_SHADING", "1"});
            if (screenProbes)
                desc.defines.push_back({"SCREEN_PROBES", "1"});
            if (reflections)
                desc.defines.push_back({"REFLECTIONS", "1"});

            if (screenProbes)
                desc.name = "SP_ProbeTracing";
            else if (reflections)
                desc.name = "LR_Tracing";
            else
                desc.name = probeTracing ? "GI_ProbeTracing" : "RayTracingPipeline";

            // Each MaterialX module runs its graph at the hit; instances select a group by their SBT offset
            // (WriteRTInstances). Module hits read only these defines, so passes that agree on them share one compiled
            // shader (a closest-hit compiles cold in about 4 s to DXIL and 22 s to SPIR-V, 2026-10-21); every pass shares
            // the any-hit. SCREEN_PROBES and REFLECTIONS also select this frame's probe atlas (ProbeVolume.hlsl
            // GI_AFTER_UPDATE), as their legacy hits do.
            desc.hitDefines = {{"PE_MATERIAL_HIT", "1"}, {probeTracing ? "GI_PROBES" : "GI_SHADING", "1"}};
            if (screenProbes)
                desc.hitDefines.push_back({"SCREEN_PROBES", "1"});

            if (reflections)
                desc.hitDefines.push_back({"REFLECTIONS", "1"});

            for (uint32_t module : modules)
                desc.hitModules.push_back(&GetOpenPBRModule(module));

            return desc;
        }

        // False, with `info` left unbuilt, when a module's hits do not compile: the module is rejected.
        bool BuildTracePassInfo(PassInfo &info, const TraceDesc &desc)
        {
            const std::string rayTrace = Path::RuntimeAssets + "Shaders/RayTracing/RayTrace.hlsl";
            auto legacy = [&](const char *entry, PeShaderStageFlags stage)
            {
                return Shader::Create({.sourcePath = rayTrace,
                                       .entryPoint = entry,
                                       .stage = stage,
                                       .defines = desc.defines});
            };
            Shader *rayGen = legacy("raygeneration", PE_SHADER_STAGE_RAYGEN_KHR);
            Shader *closestHit = legacy("closesthit", PE_SHADER_STAGE_CLOSEST_HIT_KHR);
            Shader *anyHit = legacy("anyhit", PE_SHADER_STAGE_ANY_HIT_KHR);
            Shader *miss = legacy("miss", PE_SHADER_STAGE_MISS_KHR);

            info.name = desc.name;
            info.acceleration.rayGen = rayGen;
            info.acceleration.miss = {miss};
            info.acceleration.hitGroups = {{.closestHit = closestHit, .anyHit = anyHit}};

            const std::vector<Define> maskDefines = {{"PE_MATERIAL_HIT", "1"}, {"GI_PROBES", "1"}};
            // Not skipped when another pass rejected the module: an adopted module's hits still compile (from the cache)
            // when a resize rebuilds this pipeline in place, before the scene's next list drops it.
            for (const OpenPBRModule *module : desc.hitModules)
            {
                HitGroup &group = info.acceleration.hitGroups.emplace_back(); // info frees what compiled
                try
                {
                    group.closestHit = Shader::Create({.sourcePath = module->hitSource,
                                                       .entryPoint = "pe_hit_closest",
                                                       .stage = PE_SHADER_STAGE_CLOSEST_HIT_KHR,
                                                       .defines = desc.hitDefines});
                    group.anyHit = Shader::Create({.sourcePath = module->hitSource,
                                                   .entryPoint = "pe_hit_any",
                                                   .stage = PE_SHADER_STAGE_ANY_HIT_KHR,
                                                   .defines = maskDefines});
                }
                catch (const std::exception &failure)
                {
                    RejectOpenPBRModule(*module, failure.what());
                    return false;
                }
            }

            info.acceleration.maxRecursionDepth = 4;
            info.Update();
            return true;
        }

        // In-flight frames still use the old pipeline and its descriptor sets: retire both with the frame, as module
        // passes do. A later list with the same shaders must build a pipeline of its own, not reuse one that
        // references the retired pass info.
        void RetirePassInfo(const std::shared_ptr<PassInfo> &info)
        {
            CommandBuffer::RetirePipelines(*info);
            RHII.AddToDeletionQueue([retired = info] {});
        }
    } // namespace

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
        BuildTracePipeline();
    }

    void RayTracingPass::BuildTracePipeline()
    {
        m_hitGeneration++;
        const bool hdr = m_display->GetFormat() == PE_FORMAT_R16G16B16A16_SFLOAT;
        const TraceDesc desc = MakeTraceDesc(hdr, m_probeTracing, m_screenProbes, m_reflections, m_hitModules);
        // Throws so shader hot reload restores the previous pipeline.
        PE_ERROR_IF(!BuildTracePassInfo(*m_passInfo, desc), "[RT] %s: a MaterialX module was rejected", desc.name.c_str());
    }

    void RayTracingPass::SyncHitModules(const std::vector<uint32_t> &modules)
    {
        m_hitGeneration++; // a build prepared for another list is stale now
        if (modules == m_hitModules)
            return;

        m_hitModules = modules;
        RetirePassInfo(m_passInfo);
        m_passInfo = std::make_shared<PassInfo>();
        BuildTracePipeline();
        UpdateDescriptorSets();
    }

    std::function<std::shared_ptr<PassInfo>()>
    RayTracingPass::PrepareHitBuild(const std::vector<uint32_t> &modules) const
    {
        const bool hdr = m_display->GetFormat() == PE_FORMAT_R16G16B16A16_SFLOAT;
        return [desc = MakeTraceDesc(hdr, m_probeTracing, m_screenProbes, m_reflections, modules)]
        {
            // Not adopted: the scene drops a rejected module at its next material repack, and that list builds anew.
            auto info = std::make_shared<PassInfo>();
            if (!BuildTracePassInfo(*info, desc))
                return std::shared_ptr<PassInfo>();

            // Ray tracing binds outside render passes: the frame's bind finds this pipeline in the cache.
            CommandBuffer::GetPipeline(nullptr, *info);
            return info;
        };
    }

    bool RayTracingPass::AdoptHitBuild(const std::shared_ptr<PassInfo> &info,
                                       const std::vector<uint32_t> &modules,
                                       uint64_t generation)
    {
        if (generation != m_hitGeneration)
        {
            RetirePassInfo(info);
            return false;
        }

        RetirePassInfo(m_passInfo);
        m_passInfo = info;
        m_hitModules = modules;
        m_hitGeneration++;
        UpdateDescriptorSets();
        return true;
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
            m_giFallbackUniform = Buffer::Create({.size = RHII.AlignUniform(GlobalIlluminationPass::kUniformBytes), .usage = PE_BUFFER_USAGE_UNIFORM_BUFFER, .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU, .name = "GI_DisabledUniform"});
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
        // Scene loads and pipeline edits refresh this pass even while it is released (no uniforms).
        if (!scene.GetTLAS() || m_uniforms.empty())
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
                if (!m_probeTracing) // the probe raygen never reads depth, so its layout has no binding 10
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

                // MaterialX hit groups (MaterialHitHead.slang): each binding is in the layout only if a module hit reads it.
                const SkyBox &sky = RequireActiveSceneRendererHost().GetSkyBox();
                m_boundMaterialBytes = scene.GetMaterialByteBuffer();
                m_boundEnvRadiance = sky.GetOpenPBRRadiance();
                if (desc->HasBinding(4))
                    desc->SetBuffer(4, m_boundMaterialBytes);

                if (desc->HasBinding(6))
                    desc->SetSamplers(6, scene.GetOpenPBRSamplers());

                // A rebuild before the frame's sync can still carry module groups after openpbr destroyed the sky; the
                // sync drops them before any ray is traced.
                Image *irradiance = sky.GetOpenPBRIrradiance();
                if (m_boundEnvRadiance && irradiance)
                {
                    if (desc->HasBinding(5))
                        desc->SetSampler(5, m_boundEnvRadiance->GetSampler());

                    if (desc->HasBinding(7))
                        desc->SetImageView(7, m_boundEnvRadiance->GetSRV());

                    if (desc->HasBinding(8))
                        desc->SetImageView(8, irradiance->GetSRV());
                }

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
        ubo.linear_color = SceneUsesLinearColor() ? 1u : 0u;
        ubo.env_samples = static_cast<uint32_t>(std::clamp(gSettings.openpbr_env_samples, 1, 512));

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

        // Every set, for all frames: a geometry change can come with a texture-table resize, which leaves set 0 empty.
        if (tlasChanged || geoChanged)
        {
            UpdateDescriptorSets();
            m_tlas = tlas;
            m_lastGeometryVersion = geoVersion;
        }

        const bool hitBindingsMoved = !m_hitModules.empty() &&
                                      (m_boundMaterialBytes != scene.GetMaterialByteBuffer() ||
                                       m_boundEnvRadiance != RequireActiveSceneRendererHost().GetSkyBox().GetOpenPBRRadiance());
        if (hitBindingsMoved)
            UpdateDescriptorSets();

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

    // No m_scene guard: RenderGraph::Compile runs while m_scene is null (it is set only for recording),
    // and the pass is enabled only while the scene has a TLAS anyway.
    void RayTracingPass::DeclareInputs(RGBuilder &builder)
    {
        const auto &gSettings = Settings::Get<SceneSettings>();
        builder.WriteRayTracing(m_display);
        builder.WriteRayTracing(m_rtDepth);

        // Only hybrid clamps rays by raster depth; full RT may run no depth pass at all.
        if (gSettings.render_mode != RenderMode::RayTracing)
            builder.ReadRayTracing(RequireActiveSceneRendererHost().GetDepthStencilRT());

        auto *gi = GetGlobalComponent<GlobalIlluminationPass>();
        if (!m_probeTracing && gSettings.global_illumination && gi && gi->GetIrradianceHistory())
        {
            builder.ReadRayTracing(gi->GetIrradianceHistory());
            builder.ReadRayTracing(gi->GetDistanceHistory());
        }
    }

    void RayTracingPass::DeclareOutputs(RGBuilder &builder)
    {
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
        cmd->TraceRays(m_display->GetWidth(), m_traceHeight ? m_traceHeight : m_display->GetHeight(), 1);
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
        m_uniforms.clear();
        Buffer::Destroy(m_giFallbackUniform);
        m_giFallbackUniform = nullptr;

        // Only this pass creates the target (the probe passes deriving from it null m_rtDepth in their Init).
        SceneRendererHost *rs = GetActiveSceneRendererHost();
        if (m_rtDepth && rs)
            rs->DestroyRenderTarget("rtDepth");

        m_rtDepth = nullptr;

        // A released pass re-initializes legacy-only; SceneRendererCore::SyncRTHitModules builds the modules back.
        m_hitModules.clear();
        m_hitGeneration++;
        m_boundMaterialBytes = nullptr;
        m_boundEnvRadiance = nullptr;
    }

} // namespace pe
