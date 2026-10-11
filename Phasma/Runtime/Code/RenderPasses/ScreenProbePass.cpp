#include "ScreenProbePass.h"
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
    ScreenProbePass::ScreenProbePass()
        : m_placePass(std::make_shared<PassInfo>()),
          m_adaptivePass(std::make_shared<PassInfo>()),
          m_integratePass(std::make_shared<PassInfo>()),
          m_filterPass(std::make_shared<PassInfo>()),
          m_resolvePass(std::make_shared<PassInfo>())
    {
        m_probeTracing = true;
        m_screenProbes = true;
    }

    void ScreenProbePass::Init()
    {
        auto &rs = RequireActiveSceneRendererHost();
        m_depth = rs.GetDepthStencilRT();
        m_normal = rs.GetRenderTarget("normal");
        m_velocity = rs.GetRenderTarget("velocity");
        m_rtDepth = nullptr;
        CreateImages();
        m_uniforms.resize(RHII.GetSwapchainImageCount());
        m_spUniforms.resize(RHII.GetSwapchainImageCount());
        m_boundGIUniforms.assign(RHII.GetSwapchainImageCount(), nullptr);
    }

    void ScreenProbePass::CreateImages()
    {
        const uint32_t width = m_normal->GetWidth();
        const uint32_t height = m_normal->GetHeight();
        if (m_irradiance && m_data.width == width && m_data.height == height)
            return;
        DestroyImages();
        m_data.width = width;
        m_data.height = height;
        m_data.tilesX = (width + 15) / 16;
        m_data.tilesY = (height + 15) / 16;
        m_data.adaptiveRows = (m_data.tilesY + 1) / 2; // Lumen's default: up to half as many adaptive probes as uniform
        auto create = [](const char *name, uint32_t w, uint32_t h, ::PeFormat format)
        {
            ImageDesc desc{};
            desc.width = w;
            desc.height = h;
            desc.format = format;
            desc.usage = PE_IMAGE_USAGE_SAMPLED | PE_IMAGE_USAGE_STORAGE | PE_IMAGE_USAGE_TRANSFER_SRC | PE_IMAGE_USAGE_TRANSFER_DST;
            desc.clearColor = vec4(0.f);
            desc.name = name;
            Image *image = Image::Create(desc);
            image->CreateSRV(PE_IMAGE_VIEW_TYPE_2D);
            image->CreateUAV(PE_IMAGE_VIEW_TYPE_2D, 0);
            return image;
        };
        const uint32_t tx = m_data.tilesX;
        const uint32_t ty = m_data.tilesY + m_data.adaptiveRows;
        m_positions = create("SP_ProbePositions", tx, ty, PE_FORMAT_R32G32B32A32_SFLOAT);
        m_positionsHistory = create("SP_ProbePositionsHistory", tx, ty, PE_FORMAT_R32G32B32A32_SFLOAT);
        m_normals = create("SP_ProbeNormals", tx, ty, PE_FORMAT_R16G16B16A16_SFLOAT);
        m_normalsHistory = create("SP_ProbeNormalsHistory", tx, ty, PE_FORMAT_R16G16B16A16_SFLOAT);
        m_directions = create("SP_RayDirections", tx * 8, ty * 8, PE_FORMAT_R16G16B16A16_SFLOAT);
        m_display = create("SP_Rays", tx * 8, ty * 8, PE_FORMAT_R16G16B16A16_SFLOAT);
        m_display->SetSampler(Sampler::Create(Sampler::CreateInfoInit(), "SP_MaterialSampler"));
        m_binRadiance = create("SP_BinRadiance", tx * 8, ty * 8, PE_FORMAT_R16G16B16A16_SFLOAT);
        m_binRadianceHistory = create("SP_BinRadianceHistory", tx * 8, ty * 8, PE_FORMAT_R16G16B16A16_SFLOAT);
        m_binPrior = create("SP_BinPrior", tx * 8, ty * 8, PE_FORMAT_R16G16B16A16_SFLOAT);
        m_shFiltered = create("SP_SHFiltered", tx * 9, ty, PE_FORMAT_R32G32B32A32_SFLOAT); // ScreenProbeCommon.hlsl SCREEN_PROBE_SH_TEXELS
        m_irradiance = create("SP_Irradiance", width, height, PE_FORMAT_R16G16B16A16_SFLOAT);
        m_irradianceHistory = create("SP_IrradianceHistory", width, height, PE_FORMAT_R16G16B16A16_SFLOAT);
        m_geometry = create("SP_Geometry", width, height, PE_FORMAT_R32_UINT);
        m_geometryHistory = create("SP_GeometryHistory", width, height, PE_FORMAT_R32_UINT);
        m_adaptiveMap = create("SP_AdaptiveMap", m_data.tilesX * 2, m_data.tilesY * 2, PE_FORMAT_R32_UINT);
        m_lastUpdate = 0; // new images hold no history
    }

    void ScreenProbePass::DestroyImages()
    {
        for (Image **image : {&m_positions, &m_positionsHistory, &m_normals, &m_normalsHistory, &m_directions, &m_display,
                              &m_binRadiance, &m_binRadianceHistory, &m_binPrior, &m_shFiltered, &m_irradiance,
                              &m_irradianceHistory, &m_geometry, &m_geometryHistory, &m_adaptiveMap})
        {
            Image::Destroy(*image);
            *image = nullptr;
        }
    }

    void ScreenProbePass::UpdatePassInfo()
    {
        RayTracingPass::UpdatePassInfo();
        auto compute = [](PassInfo &pass, const char *name, const char *file)
        {
            pass.name = name;
            pass.pCompShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/GlobalIllumination/" + file, .entryPoint = "main", .stage = PE_SHADER_STAGE_COMPUTE});
            pass.Update();
        };
        compute(*m_placePass, "SP_Place", "ScreenProbePlace.hlsl");
        compute(*m_adaptivePass, "SP_Adaptive", "ScreenProbeAdaptive.hlsl");
        compute(*m_integratePass, "SP_Integrate", "ScreenProbeIntegrate.hlsl");
        compute(*m_filterPass, "SP_Filter", "ScreenProbeFilter.hlsl");
        compute(*m_resolvePass, "SP_Resolve", "ScreenProbeResolve.hlsl");
    }

    void ScreenProbePass::CreateUniforms(CommandBuffer *cmd)
    {
        for (auto &uniform : m_spUniforms)
            uniform = Buffer::Create({.size = RHII.AlignUniform(sizeof(ScreenProbeUniforms)), .usage = PE_BUFFER_USAGE_UNIFORM_BUFFER, .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU, .name = "SP_Uniforms"});
        m_adaptiveCount = Buffer::Create({.size = sizeof(uint32_t), .usage = PE_BUFFER_USAGE_STORAGE_BUFFER, .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY, .name = "SP_AdaptiveCount"});
        RayTracingPass::CreateUniforms(cmd);
    }

    void ScreenProbePass::UpdateDescriptorSets()
    {
        RayTracingPass::UpdateDescriptorSets();
        auto *gi = GetGlobalComponent<GlobalIlluminationPass>();
        for (uint32_t frame = 0; frame < RHII.GetSwapchainImageCount(); ++frame)
        {
            auto &raySets = m_passInfo->GetDescriptors(frame);
            Buffer *giUniform = gi ? gi->GetProbeUniform(frame) : nullptr;
            if (raySets.size() > 2 && raySets[2] && giUniform)
            {
                auto *set = raySets[2];
                set->SetBuffer(0, giUniform);
                set->SetImageView(1, gi->GetIrradianceHistory()->GetSRV());
                set->SetImageView(2, gi->GetDistanceHistory()->GetSRV());
                set->SetSampler(3, m_display->GetSampler());
                set->SetImageView(4, m_directions->GetSRV());
                set->SetImageView(5, gi->GetFarRadianceHistory()->GetSRV());
                set->SetImageView(6, m_positions->GetSRV());
                set->SetImageView(7, m_normals->GetSRV());
                set->Update();
                m_boundGIUniforms[frame] = giUniform;
            }
            auto *place = m_placePass->GetDescriptors(frame)[0];
            place->SetBuffer(14, m_spUniforms[frame]);
            place->SetImageView(3, m_depth->GetSRV());
            place->SetImageView(4, m_normal->GetSRV());
            place->SetImageView(5, m_positionsHistory->GetSRV());
            place->SetImageView(6, m_normalsHistory->GetSRV());
            place->SetImageView(7, m_binRadianceHistory->GetSRV());
            place->SetImageView(10, m_positions->GetUAV(0));
            place->SetImageView(11, m_normals->GetUAV(0));
            place->SetImageView(12, m_directions->GetUAV(0));
            place->SetImageView(13, m_binPrior->GetUAV(0));
            place->SetImageView(15, (m_velocity ? m_velocity : m_normal)->GetSRV());
            place->SetImageView(16, m_adaptiveMap->GetUAV(0));
            place->SetBuffer(17, m_adaptiveCount);
            place->Update();
            auto *adaptive = m_adaptivePass->GetDescriptors(frame)[0];
            adaptive->SetBuffer(14, m_spUniforms[frame]);
            adaptive->SetImageView(3, m_depth->GetSRV());
            adaptive->SetImageView(4, m_normal->GetSRV());
            adaptive->SetImageView(10, m_positions->GetUAV(0));
            adaptive->SetImageView(11, m_normals->GetUAV(0));
            adaptive->SetImageView(12, m_directions->GetUAV(0));
            adaptive->SetImageView(13, m_binPrior->GetUAV(0));
            adaptive->SetImageView(16, m_adaptiveMap->GetUAV(0));
            adaptive->SetBuffer(17, m_adaptiveCount);
            adaptive->Update();
            auto *integrate = m_integratePass->GetDescriptors(frame)[0];
            integrate->SetImageView(3, m_display->GetSRV());
            integrate->SetImageView(4, m_directions->GetSRV());
            integrate->SetImageView(5, m_positions->GetSRV());
            integrate->SetImageView(10, m_normals->GetSRV());
            integrate->SetImageView(11, m_binPrior->GetSRV());
            integrate->SetImageView(7, m_binRadiance->GetUAV(0));
            integrate->Update();
            auto *filter = m_filterPass->GetDescriptors(frame)[0];
            filter->SetBuffer(14, m_spUniforms[frame]);
            filter->SetImageView(3, m_binRadiance->GetSRV());
            filter->SetImageView(4, m_positions->GetSRV());
            filter->SetImageView(5, m_normals->GetSRV());
            filter->SetImageView(6, m_shFiltered->GetUAV(0));
            filter->Update();
            auto *resolve = m_resolvePass->GetDescriptors(frame)[0];
            resolve->SetBuffer(14, m_spUniforms[frame]);
            resolve->SetImageView(3, m_depth->GetSRV());
            resolve->SetImageView(4, m_normal->GetSRV());
            resolve->SetImageView(5, m_shFiltered->GetSRV());
            resolve->SetImageView(6, m_positions->GetSRV());
            resolve->SetImageView(7, m_normals->GetSRV());
            resolve->SetImageView(10, m_irradianceHistory->GetSRV());
            resolve->SetImageView(11, m_geometryHistory->GetSRV());
            resolve->SetImageView(12, m_irradiance->GetUAV(0));
            resolve->SetImageView(13, m_geometry->GetUAV(0));
            resolve->SetImageView(15, (m_velocity ? m_velocity : m_normal)->GetSRV());
            resolve->SetImageView(16, m_adaptiveMap->GetSRV());
            resolve->Update();
        }
    }

    void ScreenProbePass::Update()
    {
        RayTracingPass::Update();
        Scene *scene = GetActiveScene();
        Camera *camera = scene->GetActiveCamera();
        const uint32_t frame = RHII.GetFrameIndex();
        auto *gi = GetGlobalComponent<GlobalIlluminationPass>();
        if (gi && gi->GetProbeUniform(frame) && m_boundGIUniforms[frame] != gi->GetProbeUniform(frame))
            UpdateDescriptorSets();
        // The velocity target comes and goes with the settings that need it (SceneNeedsVelocityRT).
        if (Image *velocity = RequireActiveSceneRendererHost().GetRenderTarget("velocity"); velocity != m_velocity)
        {
            m_velocity = velocity;
            UpdateDescriptorSets();
        }
        m_data.hasVelocity = m_velocity ? 1u : 0u;
        // A frame rendered without this pass (GI toggled off, no TLAS) leaves history that no longer matches the view.
        const bool reset = m_historyScene != scene || m_historyGeneration != scene->GetGeneration() || !m_lastUpdate ||
                           RequireActiveSceneRendererHost().GetRenderedFrames() > m_lastUpdate;
        m_data.invViewProj = camera->GetInvViewProjection();
        m_data.prevViewProj = m_viewProj;
        m_data.camPos = vec4(camera->GetPosition(), 1.f);
        m_data.prevCamPos = m_camPos;
        m_data.frame = static_cast<uint32_t>(RHII.GetFrameCounter());
        m_data.reset = reset ? 1u : 0u;
        m_data.viewProj = camera->GetProjectionNoJitter() * camera->GetView();
        m_pendingViewProj = m_data.viewProj;
        m_pendingCamPos = m_data.camPos;
        BufferRange range{};
        range.data = &m_data;
        range.size = sizeof(m_data);
        m_spUniforms[frame]->Copy(1, &range, false);
    }

    void ScreenProbePass::DeclareInputs(RGBuilder &builder)
    {
        builder.ReadCompute(m_depth);
        builder.ReadCompute(m_normal);
        if (m_velocity)
            builder.ReadCompute(m_velocity);
        builder.ReadCompute(m_positionsHistory);
        builder.ReadCompute(m_normalsHistory);
        builder.ReadCompute(m_binRadianceHistory);
        builder.WriteCompute(m_positions);
        builder.WriteCompute(m_normals);
        builder.WriteCompute(m_directions);
        builder.WriteCompute(m_binPrior);
        builder.WriteCompute(m_adaptiveMap);
        builder.WriteBuffer(m_adaptiveCount);
        builder.WriteRayTracing(m_display);
        if (auto *gi = GetGlobalComponent<GlobalIlluminationPass>(); gi && gi->GetIrradianceHistory())
        {
            builder.ReadRayTracing(gi->GetIrradianceHistory());
            builder.ReadRayTracing(gi->GetDistanceHistory());
            builder.ReadRayTracing(gi->GetFarRadianceHistory());
        }
        builder.WriteCompute(m_binRadiance);
        builder.WriteCompute(m_shFiltered);
        builder.ReadCompute(m_irradianceHistory);
        builder.ReadCompute(m_geometryHistory);
        builder.WriteCompute(m_irradiance);
        builder.WriteCompute(m_geometry);
    }

    void ScreenProbePass::DeclareOutputs(RGBuilder &builder)
    {
        for (Image *image : {m_directions, m_binPrior, m_display, m_shFiltered, m_adaptiveMap})
            builder.OutputCustom(image, PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, PE_STAGE_COMPUTE_SHADER, PE_ACCESS_SHADER_READ);
        for (Image *image : {m_positions, m_normals, m_binRadiance, m_irradiance, m_geometry})
            builder.OutputCustom(image, PE_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, PE_STAGE_TRANSFER, PE_ACCESS_TRANSFER_READ);
        for (Image *image : {m_positionsHistory, m_normalsHistory, m_binRadianceHistory, m_irradianceHistory, m_geometryHistory})
            builder.OutputCustom(image, PE_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, PE_STAGE_TRANSFER, PE_ACCESS_TRANSFER_WRITE);
    }

    void ScreenProbePass::ExecutePass(CommandBuffer *cmd)
    {
        if (!m_scene || !m_scene->GetTLAS())
            return;
        Scene *scene = m_scene;
        cmd->BeginDebugRegion("SP_Place");
        cmd->BindPipeline(*m_placePass);
        cmd->Dispatch(m_data.tilesX, m_data.tilesY, 1);
        // ScreenProbeAdaptive reads the uniform probes and writes the slots, map and counter the place pass cleared.
        cmd->MemoryBarrier({.srcStageMask = PE_STAGE_COMPUTE_SHADER, .srcAccessMask = PE_ACCESS_SHADER_WRITE, .dstStageMask = PE_STAGE_COMPUTE_SHADER, .dstAccessMask = PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_WRITE});
        cmd->BindPipeline(*m_adaptivePass);
        cmd->Dispatch(m_data.tilesX * 2, m_data.tilesY * 2, 1);
        cmd->EndDebugRegion();
        for (Image *image : {m_positions, m_normals, m_directions})
            cmd->ImageBarrier({.image = image, .layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .stageFlags = PE_STAGE_RAY_TRACING_SHADER_KHR, .accessMask = PE_ACCESS_SHADER_READ});
        RayTracingPass::ExecutePass(cmd);
        cmd->BeginDebugRegion("ScreenProbes");
        for (Image *image : {m_display, m_directions, m_positions, m_normals, m_binPrior})
            cmd->ImageBarrier({.image = image, .layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .stageFlags = PE_STAGE_COMPUTE_SHADER, .accessMask = PE_ACCESS_SHADER_READ});
        const uint32_t probeRows = m_data.tilesY + m_data.adaptiveRows;
        cmd->BindPipeline(*m_integratePass);
        cmd->Dispatch(m_data.tilesX, probeRows, 1);
        cmd->ImageBarrier({.image = m_binRadiance, .layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .stageFlags = PE_STAGE_COMPUTE_SHADER, .accessMask = PE_ACCESS_SHADER_READ});
        cmd->BindPipeline(*m_filterPass);
        cmd->Dispatch(m_data.tilesX, probeRows, 1);
        for (Image *image : {m_shFiltered, m_adaptiveMap})
            cmd->ImageBarrier({.image = image, .layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .stageFlags = PE_STAGE_COMPUTE_SHADER, .accessMask = PE_ACCESS_SHADER_READ});
        cmd->BindPipeline(*m_resolvePass);
        cmd->Dispatch((m_data.width + 7) / 8, (m_data.height + 7) / 8, 1);
        cmd->CopyImage(m_irradiance, m_irradianceHistory);
        cmd->CopyImage(m_geometry, m_geometryHistory);
        cmd->CopyImage(m_positions, m_positionsHistory);
        cmd->CopyImage(m_normals, m_normalsHistory);
        cmd->CopyImage(m_binRadiance, m_binRadianceHistory);
        cmd->EndDebugRegion();
        m_historyScene = scene;
        m_historyGeneration = scene->GetGeneration();
        m_viewProj = m_pendingViewProj;
        m_camPos = m_pendingCamPos;
        m_lastUpdate = RequireActiveSceneRendererHost().GetRenderedFrames() + 1; // this frame's number
    }

    void ScreenProbePass::Resize(uint32_t width, uint32_t height)
    {
        auto &rs = RequireActiveSceneRendererHost();
        m_depth = rs.GetDepthStencilRT();
        m_normal = rs.GetRenderTarget("normal");
        m_velocity = rs.GetRenderTarget("velocity");
        CreateImages();
        UpdateDescriptorSets();
    }

    void ScreenProbePass::Destroy()
    {
        RayTracingPass::Destroy();
        for (auto *uniform : m_spUniforms)
            Buffer::Destroy(uniform);
        m_spUniforms.clear();
        Buffer::Destroy(m_adaptiveCount);
        DestroyImages();
        m_rtDepth = nullptr;
        m_historyScene = nullptr;
    }

    std::vector<PassInfo *> ScreenProbePass::GetPassInfos() noexcept
    {
        return {m_passInfo.get(), m_placePass.get(), m_adaptivePass.get(), m_integratePass.get(), m_filterPass.get(), m_resolvePass.get()};
    }
} // namespace pe
