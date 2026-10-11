#include "GlobalIlluminationPass.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/RenderGraph.h"
#include "API/Shader.h"
#include "Camera/Camera.h"
#include "Render/SceneRendererHost.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"

namespace pe
{
    GlobalIlluminationPass::GlobalIlluminationPass()
        : m_updatePass(std::make_shared<PassInfo>()),
          m_samplePass(std::make_shared<PassInfo>()),
          m_radiancePass(std::make_shared<PassInfo>())
    {
        m_probeTracing = true;
    }

    void GlobalIlluminationPass::Init()
    {
        auto create = [](const char *name, uint32_t width, uint32_t height, ::PeFormat format)
        {
            ImageDesc desc{};
            desc.width = width;
            desc.height = height;
            desc.format = format;
            desc.usage = PE_IMAGE_USAGE_SAMPLED | PE_IMAGE_USAGE_STORAGE | PE_IMAGE_USAGE_TRANSFER_SRC | PE_IMAGE_USAGE_TRANSFER_DST;
            desc.clearColor = vec4(0.f);
            desc.name = name;
            Image *image = Image::Create(desc);
            image->CreateSRV(PE_IMAGE_VIEW_TYPE_2D);
            image->CreateUAV(PE_IMAGE_VIEW_TYPE_2D, 0);
            return image;
        };
        // Every atlas holds kCascades bands of 512 probes, one per cascade, stacked downward.
        m_display = create("GI_Rays", 256, 512 * kCascades, PE_FORMAT_R16G16B16A16_SFLOAT);
        m_rayDirections = create("GI_RayDirections", 256, 512 * kCascades, PE_FORMAT_R32G32B32A32_SFLOAT);
        // 16x16 octahedral radiance per probe (rgb, a = samples): the cache the next frame's rays are drawn from.
        m_radiance = create("GI_Radiance", 128, 1024 * kCascades, PE_FORMAT_R32G32B32A32_SFLOAT);
        m_radianceHistory = create("GI_RadianceHistory", 128, 1024 * kCascades, PE_FORMAT_R32G32B32A32_SFLOAT);
        // Far-field radiance, what each ray sees beyond the probe's interpolation footprint: the cache screen probes
        // take over from where their own short rays end (Lumen's radiance cache), so it never shows a thin wall's far side.
        m_farRays = create("GI_FarRays", 256, 512 * kCascades, PE_FORMAT_R16G16B16A16_SFLOAT);
        m_farRadiance = create("GI_FarRadiance", 128, 1024 * kCascades, PE_FORMAT_R32G32B32A32_SFLOAT);
        m_farRadianceHistory = create("GI_FarRadianceHistory", 128, 1024 * kCascades, PE_FORMAT_R32G32B32A32_SFLOAT);
        m_display->SetSampler(Sampler::Create(Sampler::CreateInfoInit(), "GI_MaterialSampler"));
        m_rtDepth = nullptr;
        // 32-bit: a 0.999 history moves each texel by 1/1000 of the difference, below half precision.
        m_irradiance = create("GI_Irradiance", 64, 512 * kCascades, PE_FORMAT_R32G32B32A32_SFLOAT);
        m_distance = create("GI_Distance", 64, 512 * kCascades, PE_FORMAT_R32G32B32A32_SFLOAT);
        m_irradianceHistory = create("GI_IrradianceHistory", 64, 512 * kCascades, PE_FORMAT_R32G32B32A32_SFLOAT);
        m_distanceHistory = create("GI_DistanceHistory", 64, 512 * kCascades, PE_FORMAT_R32G32B32A32_SFLOAT);
        m_uniforms.resize(RHII.GetSwapchainImageCount());
        m_probeUniforms.resize(RHII.GetSwapchainImageCount());
        m_probeData.reset = 1;
        m_lastUpdate = 0;
    }

    void GlobalIlluminationPass::UpdatePassInfo()
    {
        RayTracingPass::UpdatePassInfo();
        m_updatePass->name = "GI_ProbeUpdate";
        m_updatePass->pCompShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/GlobalIllumination/ProbeUpdate.hlsl", .entryPoint = "main", .stage = PE_SHADER_STAGE_COMPUTE});
        m_updatePass->Update();
        m_samplePass->name = "GI_ProbeSample";
        m_samplePass->pCompShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/GlobalIllumination/ProbeSample.hlsl", .entryPoint = "main", .stage = PE_SHADER_STAGE_COMPUTE});
        m_samplePass->Update();
        m_radiancePass->name = "GI_ProbeRadiance";
        m_radiancePass->pCompShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/GlobalIllumination/ProbeRadiance.hlsl", .entryPoint = "main", .stage = PE_SHADER_STAGE_COMPUTE});
        m_radiancePass->Update();
    }

    void GlobalIlluminationPass::CreateUniforms(CommandBuffer *cmd)
    {
        for (auto &uniform : m_probeUniforms)
            uniform = Buffer::Create({.size = RHII.AlignUniform(sizeof(ProbeUniforms)), .usage = PE_BUFFER_USAGE_UNIFORM_BUFFER, .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU, .name = "GI_ProbeUniforms"});
        RayTracingPass::CreateUniforms(cmd);
    }

    void GlobalIlluminationPass::UpdateDescriptorSets()
    {
        RayTracingPass::UpdateDescriptorSets();
        for (uint32_t frame = 0; frame < RHII.GetSwapchainImageCount(); ++frame)
        {
            auto &raySets = m_passInfo->GetDescriptors(frame);
            if (raySets.size() > 2 && raySets[2])
            {
                auto *set = raySets[2];
                set->SetBuffer(0, m_probeUniforms[frame]);
                set->SetImageView(1, m_irradianceHistory->GetSRV());
                set->SetImageView(2, m_distanceHistory->GetSRV());
                set->SetSampler(3, m_display->GetSampler());
                set->SetImageView(4, m_rayDirections->GetSRV());
                set->SetImageView(5, m_farRays->GetUAV(0));
                set->Update();
            }
            auto *update = m_updatePass->GetDescriptors(frame)[0];
            update->SetBuffer(8, m_probeUniforms[frame]);
            update->SetImageView(1, m_irradianceHistory->GetSRV());
            update->SetImageView(2, m_distanceHistory->GetSRV());
            update->SetImageView(3, m_display->GetSRV());
            update->SetImageView(4, m_irradiance->GetUAV(0));
            update->SetImageView(5, m_distance->GetUAV(0));
            update->SetImageView(6, m_rayDirections->GetSRV());
            update->Update();
            auto *sample = m_samplePass->GetDescriptors(frame)[0];
            sample->SetBuffer(8, m_probeUniforms[frame]);
            sample->SetImageView(4, m_rayDirections->GetUAV(0));
            sample->SetImageView(5, m_radianceHistory->GetSRV());
            sample->Update();
            auto *radiance = m_radiancePass->GetDescriptors(frame)[0];
            radiance->SetBuffer(8, m_probeUniforms[frame]);
            radiance->SetImageView(3, m_display->GetSRV());
            radiance->SetImageView(6, m_rayDirections->GetSRV());
            radiance->SetImageView(7, m_radianceHistory->GetSRV());
            radiance->SetImageView(10, m_radiance->GetUAV(0));
            radiance->SetImageView(4, m_farRays->GetSRV());
            radiance->SetImageView(5, m_farRadianceHistory->GetSRV());
            radiance->SetImageView(11, m_farRadiance->GetUAV(0));
            radiance->Update();
        }
    }

    void GlobalIlluminationPass::Update()
    {
        RayTracingPass::Update();
        Scene *scene = GetActiveScene();
        Camera *camera = scene->GetActiveCamera();
        const auto &settings = Settings::Get<SceneSettings>();
        const float requested = std::isfinite(settings.gi_probe_spacing) ? std::clamp(settings.gi_probe_spacing, 0.25f, 64.f) : 2.f;
        if (m_boundsScene != scene || m_boundsGeneration != scene->GetGeneration() || m_boundsVersion != scene->GetGeometryVersion())
        {
            // Grow-only within a scene, so meshes spawned inside the box never move or reset it.
            // ponytail: refit on geometry changes only; static meshes moved later keep the previous fit.
            if (m_boundsScene != scene || m_boundsGeneration != scene->GetGeneration())
                m_sceneBounds = {vec3(FLT_MAX), vec3(-FLT_MAX)};
            m_boundsScene = scene;
            m_boundsGeneration = scene->GetGeneration();
            m_boundsVersion = scene->GetGeometryVersion();
            for (uint32_t i = 0; i < scene->GetNodeCount(); ++i)
            {
                const NodeId *node = scene->GetNodeId(i);
                if (!node || !(scene->GetComponentFlags(node) & Component_Mesh) || !scene->IsNodeHierarchyEnabled(node) ||
                    !scene->IsNodeRenderVisible(node))
                    continue;
                const AABB &bounds = scene->GetWorldAABB(node);
                const vec3 size = bounds.max - bounds.min;
                if (!std::isfinite(size.x) || !std::isfinite(size.y) || !std::isfinite(size.z) || size.x < 0.f || size.y < 0.f || size.z < 0.f)
                    continue;
                m_sceneBounds.min = glm::min(m_sceneBounds.min, bounds.min);
                m_sceneBounds.max = glm::max(m_sceneBounds.max, bounds.max);
            }
            m_sceneBoundsValid = m_sceneBounds.min.x <= m_sceneBounds.max.x;
        }
        // A scene spanning at most 7 cells per axis at 4x the requested spacing gets one volume fitted to its bounds:
        // it never scrolls and has no edge in view, so no surface falls back to unoccluded sky light. Otherwise
        // kCascades cascades follow the camera, each snapped to its own spacing (requested x 1, 2, 4, 8: +-7 to +-56 m
        // at 2 m), and only the last fades into environment lighting at its edges, as Lumen's radiance cache clipmaps.
        constexpr float kPad = 0.5f;
        const vec3 fitted = glm::max((m_sceneBounds.max - m_sceneBounds.min + vec3(2.f * kPad)) / 7.f, vec3(0.25f));
        const bool fit = m_sceneBoundsValid && std::max({fitted.x, fitted.y, fitted.z}) <= requested * 4.f;
        m_probeData.cascadeCount = fit ? 1u : kCascades;
        bool sameLayout = m_historyCascades == m_probeData.cascadeCount;
        for (uint32_t c = 0; c < kCascades; ++c)
        {
            const float cascadeSpacing = requested * static_cast<float>(1u << c);
            const vec3 spacing = fit ? fitted : vec3(cascadeSpacing);
            const vec3 origin = fit ? m_sceneBounds.min - vec3(kPad)
                                    : glm::floor(camera->GetPosition() / cascadeSpacing) * cascadeSpacing - vec3(3.f * cascadeSpacing);
            m_probeData.spacing[c] = vec4(spacing, fit ? 0.f : 1.f);
            m_probeData.originSpacing[c] = vec4(origin, std::min({spacing.x, spacing.y, spacing.z}));
            m_probeData.historyOriginSpacing[c] = m_historyOriginSpacing[c];
            sameLayout = sameLayout && m_historySpacing[c] == m_probeData.spacing[c];
        }
        // A frame rendered without this pass (GI toggled off, no TLAS) leaves history that no longer matches the scene.
        bool reset = m_historyScene != scene || m_historyGeneration != scene->GetGeneration() || !sameLayout || !m_lastUpdate ||
                     RequireActiveSceneRendererHost().GetRenderedFrames() > m_lastUpdate;
        m_probeData.intensity = std::isfinite(settings.gi_intensity) ? std::clamp(settings.gi_intensity, 0.f, 16.f) : 1.f;
        m_probeData.hysteresis = std::isfinite(settings.gi_hysteresis) ? std::clamp(settings.gi_hysteresis, 0.f, 0.999f) : 0.999f;
        // Probe texels detect lighting changes on their own (ProbeUpdate.hlsl). A known change to anything the probe
        // rays see (lights, ray-traced transforms, geometry and materials, the settings that shade ray hits) also caps
        // every texel's average at 200 frames, so changes too small to stand out of the noise still settle in a few
        // seconds without restarting every probe. Camera movement does not count.
        Hash inputs(static_cast<size_t>(scene->GetGiInputVersion()));
        inputs.Combine(static_cast<size_t>(scene->GetGeometryVersion()));
        const auto &pp = ActivePostProcessProfile();
        for (float value : {pp.IBL ? 1.f : 0.f, pp.IBL_intensity * ActivePostProcessBlend().IBL,
                            settings.shadows ? 1.f : 0.f, settings.use_Disney_PBR ? 1.f : 0.f})
            inputs.Combine(value);
        const bool changed = static_cast<size_t>(inputs) != m_historyInputs;
        m_historyInputs = static_cast<size_t>(inputs);
        m_probeData.reset = (reset ? 1u : 0u) | (changed ? 2u : 0u); // bit 0: discard history, bit 1: known change
        m_probeData.frame = RHII.GetFrameCounter();
        m_traceHeight = kCascadeProbes * m_probeData.cascadeCount; // one row of rays per probe
        BufferRange range{};
        range.data = &m_probeData;
        range.size = sizeof(m_probeData);
        m_probeUniforms[RHII.GetFrameIndex()]->Copy(1, &range, false);
    }

    void GlobalIlluminationPass::DeclareInputs(RGBuilder &builder)
    {
        builder.WriteCompute(m_rayDirections);
        builder.ReadCompute(m_radianceHistory);
        builder.WriteCompute(m_radiance);
        builder.WriteRayTracing(m_display);
        builder.WriteRayTracing(m_farRays);
        builder.ReadCompute(m_farRadianceHistory);
        builder.WriteCompute(m_farRadiance);
        builder.ReadRayTracing(m_irradianceHistory);
        builder.ReadRayTracing(m_distanceHistory);
        builder.ReadCompute(m_irradianceHistory);
        builder.ReadCompute(m_distanceHistory);
        builder.WriteCompute(m_irradiance);
        builder.WriteCompute(m_distance);
    }

    void GlobalIlluminationPass::DeclareOutputs(RGBuilder &builder)
    {
        builder.OutputCustom(m_rayDirections, PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, PE_STAGE_COMPUTE_SHADER, PE_ACCESS_SHADER_READ);
        builder.OutputCustom(m_display, PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, PE_STAGE_COMPUTE_SHADER, PE_ACCESS_SHADER_READ);
        builder.OutputCustom(m_farRays, PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, PE_STAGE_COMPUTE_SHADER, PE_ACCESS_SHADER_READ);
        builder.OutputCustom(m_farRadiance, PE_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, PE_STAGE_TRANSFER, PE_ACCESS_TRANSFER_READ);
        builder.OutputCustom(m_farRadianceHistory, PE_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, PE_STAGE_TRANSFER, PE_ACCESS_TRANSFER_WRITE);
        builder.OutputCustom(m_irradiance, PE_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, PE_STAGE_TRANSFER, PE_ACCESS_TRANSFER_READ);
        builder.OutputCustom(m_distance, PE_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, PE_STAGE_TRANSFER, PE_ACCESS_TRANSFER_READ);
        builder.OutputCustom(m_radiance, PE_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, PE_STAGE_TRANSFER, PE_ACCESS_TRANSFER_READ);
        builder.OutputCustom(m_radianceHistory, PE_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, PE_STAGE_TRANSFER, PE_ACCESS_TRANSFER_WRITE);
        builder.OutputCustom(m_irradianceHistory, PE_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, PE_STAGE_TRANSFER, PE_ACCESS_TRANSFER_WRITE);
        builder.OutputCustom(m_distanceHistory, PE_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, PE_STAGE_TRANSFER, PE_ACCESS_TRANSFER_WRITE);
    }

    void GlobalIlluminationPass::ExecutePass(CommandBuffer *cmd)
    {
        if (!m_scene || !m_scene->GetTLAS())
            return;
        Scene *scene = m_scene;
        cmd->BeginDebugRegion("GI_ProbeSample");
        cmd->BindPipeline(*m_samplePass);
        const uint32_t probes = kCascadeProbes * m_probeData.cascadeCount;
        cmd->Dispatch(probes, 1, 1);
        cmd->EndDebugRegion();
        cmd->ImageBarrier({.image = m_rayDirections, .layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .stageFlags = PE_STAGE_RAY_TRACING_SHADER_KHR, .accessMask = PE_ACCESS_SHADER_READ});
        RayTracingPass::ExecutePass(cmd);
        cmd->BeginDebugRegion("GlobalIllumination");
        cmd->ImageBarrier({.image = m_display, .layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .stageFlags = PE_STAGE_COMPUTE_SHADER, .accessMask = PE_ACCESS_SHADER_READ});
        cmd->ImageBarrier({.image = m_farRays, .layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .stageFlags = PE_STAGE_COMPUTE_SHADER, .accessMask = PE_ACCESS_SHADER_READ});
        cmd->ImageBarrier({.image = m_rayDirections, .layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .stageFlags = PE_STAGE_COMPUTE_SHADER, .accessMask = PE_ACCESS_SHADER_READ});
        cmd->BindPipeline(*m_updatePass);
        cmd->Dispatch(8, probes / 8, 1);
        cmd->BindPipeline(*m_radiancePass);
        cmd->Dispatch(8, probes / 8, 1); // a 16x16 group per probe
        cmd->CopyImage(m_irradiance, m_irradianceHistory);
        cmd->CopyImage(m_distance, m_distanceHistory);
        cmd->CopyImage(m_radiance, m_radianceHistory);
        cmd->CopyImage(m_farRadiance, m_farRadianceHistory);
        cmd->EndDebugRegion();
        m_historyScene = scene;
        m_historyGeneration = scene->GetGeneration();
        std::copy(std::begin(m_probeData.originSpacing), std::end(m_probeData.originSpacing), m_historyOriginSpacing);
        std::copy(std::begin(m_probeData.spacing), std::end(m_probeData.spacing), m_historySpacing);
        m_historyCascades = m_probeData.cascadeCount;
        m_lastUpdate = RequireActiveSceneRendererHost().GetRenderedFrames() + 1; // this frame's number
    }

    void GlobalIlluminationPass::Resize(uint32_t width, uint32_t height)
    {
        UpdateDescriptorSets();
    }

    void GlobalIlluminationPass::Destroy()
    {
        RayTracingPass::Destroy();
        for (auto *uniform : m_probeUniforms)
            Buffer::Destroy(uniform);
        m_probeUniforms.clear();
        for (Image **image : {&m_display, &m_rayDirections, &m_radiance, &m_radianceHistory, &m_farRays, &m_farRadiance, &m_farRadianceHistory, &m_irradiance, &m_distance, &m_irradianceHistory, &m_distanceHistory})
        {
            Image::Destroy(*image);
            *image = nullptr;
        }
        m_rtDepth = nullptr;
        m_historyScene = nullptr;
        m_boundsScene = nullptr;
    }

    std::vector<PassInfo *> GlobalIlluminationPass::GetPassInfos() noexcept
    {
        return {m_passInfo.get(), m_updatePass.get(), m_samplePass.get(), m_radiancePass.get()};
    }
} // namespace pe
