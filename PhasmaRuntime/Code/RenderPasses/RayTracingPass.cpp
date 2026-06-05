#include "RayTracingPass.h"
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
        m_display = RequireActiveSceneRendererHost().GetViewportRT();
        m_uniforms.resize(RHII.GetSwapchainImageCount());
    }

    void RayTracingPass::UpdatePassInfo()
    {
        std::vector<Define> defines = {};

        // Shaders
        Shader *rayGen = Shader::Create({.sourcePath = Path::Assets + "Shaders/RayTracing/RayTrace.hlsl", .entryPoint = "raygeneration", .stage = PE_SHADER_STAGE_RAYGEN_KHR, .defines = defines});
        Shader *closestHit = Shader::Create({.sourcePath = Path::Assets + "Shaders/RayTracing/RayTrace.hlsl", .entryPoint = "closesthit", .stage = PE_SHADER_STAGE_CLOSEST_HIT_KHR, .defines = defines});
        Shader *anyHit = Shader::Create({.sourcePath = Path::Assets + "Shaders/RayTracing/RayTrace.hlsl", .entryPoint = "anyhit", .stage = PE_SHADER_STAGE_ANY_HIT_KHR, .defines = defines});
        Shader *miss = Shader::Create({.sourcePath = Path::Assets + "Shaders/RayTracing/RayTrace.hlsl", .entryPoint = "miss", .stage = PE_SHADER_STAGE_MISS_KHR, .defines = defines});

        m_passInfo->name = "RayTracingPipeline";
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
                desc->Update();
            }
        }
    }

    void RayTracingPass::Update()
    {
        Camera *camera = GetActiveScene()->GetActiveCamera();
        auto &gSettings = Settings::Get<GlobalSettings>();

        RayTracingPassUBO ubo{};
        ubo.invViewProj = camera->GetInvViewProjection();
        ubo.invView = camera->GetInvView();
        ubo.invProj = camera->GetInvProjection();
        ubo.camPos = vec4(camera->GetPosition(), 1.0f);
        ubo.lights_intensity = gSettings.lights_intensity;
        ubo.shadows = gSettings.shadows ? 1 : 0;
        ubo.use_Disney_PBR = gSettings.use_Disney_PBR ? 1 : 0;
        ubo.ibl_intensity = gSettings.IBL_intensity;
        ubo.renderMode = static_cast<uint32_t>(gSettings.render_mode);

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
                        rtSet0->SetBuffer(2, scene.GetUniforms(i));
                        rtSet0->SetBuffer(3, scene.GetMeshConstants());
                        rtSet0->SetSampler(4, m_display->GetSampler());
                        rtSet0->SetBuffer(11, scene.GetMaterialTable());
                        rtSet0->SetImageViews(12, scene.GetImageViews());
                        rtSet0->Update();
                    }
                }
            }
        }
    }

    void RayTracingPass::DeclareInputs(RGBuilder &builder)
    {
        if (!m_scene || !m_scene->GetTLAS())
            return;

        Image *depth = RequireActiveSceneRendererHost().GetDepthStencilRT();
        builder.WriteRayTracing(m_display);
        builder.ReadRayTracing(depth);
    }

    void RayTracingPass::DeclareOutputs(RGBuilder &builder)
    {
        if (!m_scene || !m_scene->GetTLAS())
            return;

        builder.OutputCustom(m_display, PE_IMAGE_LAYOUT_GENERAL,
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
    }

} // namespace pe
