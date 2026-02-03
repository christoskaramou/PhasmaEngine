#include "RayTracingPass.h"
#include "API/AccelerationStructure.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/Shader.h"
#include "Camera/Camera.h"
#include "Scene/Scene.h"
#include "Systems/LightSystem.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    void RayTracingPass::Init()
    {
        m_scene = nullptr;
        m_display = Context::Get()->GetSystem<RendererSystem>()->GetViewportRT();
        m_uniforms.resize(RHII.GetSwapchainImageCount());
    }

    void RayTracingPass::UpdatePassInfo()
    {
        std::vector<Define> defines = {
            Define{"MAX_POINT_LIGHTS", std::to_string(MAX_POINT_LIGHTS)},
            Define{"MAX_SPOT_LIGHTS", std::to_string(MAX_SPOT_LIGHTS)},
            Define{"MAX_AREA_LIGHTS", std::to_string(MAX_AREA_LIGHTS)},
        };

        // Shaders
        Shader *rayGen = Shader::Create(Path::Assets + "Shaders/RayTracing/RayTrace.hlsl", vk::ShaderStageFlagBits::eRaygenKHR, "raygeneration", defines, ShaderCodeType::HLSL);
        Shader *closestHit = Shader::Create(Path::Assets + "Shaders/RayTracing/RayTrace.hlsl", vk::ShaderStageFlagBits::eClosestHitKHR, "closesthit", defines, ShaderCodeType::HLSL);
        Shader *anyHit = Shader::Create(Path::Assets + "Shaders/RayTracing/RayTrace.hlsl", vk::ShaderStageFlagBits::eAnyHitKHR, "anyhit", defines, ShaderCodeType::HLSL);
        Shader *miss = Shader::Create(Path::Assets + "Shaders/RayTracing/RayTrace.hlsl", vk::ShaderStageFlagBits::eMissKHR, "miss", defines, ShaderCodeType::HLSL);

        m_passInfo->name = "RayTracingPipeline";
        m_passInfo->acceleration.rayGen = rayGen;
        m_passInfo->acceleration.miss = {miss};
        m_passInfo->acceleration.hitGroups = {{.closestHit = closestHit, .anyHit = anyHit}};
        m_passInfo->acceleration.maxRecursionDepth = 3;
        m_passInfo->Update();
    }

    void RayTracingPass::CreateUniforms(CommandBuffer *cmd)
    {
        for (auto &uniform : m_uniforms)
        {
            uniform = Buffer::Create(
                RHII.AlignUniform(sizeof(RayTracingPassUBO)),
                vk::BufferUsageFlagBits2::eUniformBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                "RayTracing_uniform_buffer");
            uniform->Map();
            uniform->Zero();
            uniform->Flush();
            uniform->Unmap();
        }

        UpdateDescriptorSets();
    }

    void RayTracingPass::UpdateDescriptorSets()
    {
        Scene &scene = GetGlobalSystem<RendererSystem>()->GetScene();
        if (!scene.GetTLAS())
            return;

        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
        {
            auto &descriptors = m_passInfo->GetDescriptors(i);

            // All bindings go in Set 0 (raytracing shaders share the same descriptor set layout)
            if (descriptors.size() > 0 && descriptors[0])
            {
                auto *rs = GetGlobalSystem<RendererSystem>();
                bool day = Settings::Get<GlobalSettings>().day;
                auto *skybox = day ? rs->GetSkyBoxDay().GetCubeMap() : rs->GetSkyBoxNight().GetCubeMap();
                auto *desc = descriptors[0];
                desc->SetAccelerationStructure(0, scene.GetTLAS()->ApiHandle());
                desc->SetImageView(1, m_display->GetUAV(0));
                desc->SetBuffer(2, scene.GetUniforms(i));
                desc->SetBuffer(3, scene.GetMeshConstants());
                desc->SetSampler(4, m_display->GetSampler());
                desc->SetImageViews(5, scene.GetImageViews());
                desc->SetBuffer(6, scene.GetBuffer());
                desc->SetBuffer(7, scene.GetMeshInfoBuffer());
                desc->SetImageView(8, skybox->GetSRV());
                auto *ibl_brdf_lut = rs->GetIBL_LUT();
                desc->SetImageView(9, ibl_brdf_lut->GetSRV());
                desc->SetImageView(10, rs->GetDepthStencilRT()->GetSRV());
                desc->Update();
            }

            // Set 1
            if (descriptors.size() > 1 && descriptors[1])
            {
                auto *desc = descriptors[1];
                desc->SetBuffer(0, GetGlobalSystem<LightSystem>()->GetUniform(i));
                desc->SetBuffer(1, m_uniforms[i]);
                desc->Update();
            }
        }
    }

    void RayTracingPass::Update()
    {
        Camera *camera = GetGlobalSystem<RendererSystem>()->GetScene().GetActiveCamera();
        auto &gSettings = Settings::Get<GlobalSettings>();

        RayTracingPassUBO ubo{};
        ubo.invViewProj = camera->GetInvViewProjection();
        ubo.invView = camera->GetInvView();
        ubo.invProj = camera->GetInvProjection();
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

        Scene &scene = GetGlobalSystem<RendererSystem>()->GetScene();

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
                        rtSet0->SetSampler(4, scene.GetDefaultSampler());
                        rtSet0->SetImageViews(5, scene.GetImageViews());
                        rtSet0->Update();
                    }
                }
            }
        }
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

        ImageBarrierInfo barrierDisplay{};
        barrierDisplay.image = m_display;
        barrierDisplay.layout = vk::ImageLayout::eGeneral;
        barrierDisplay.stageFlags = vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
        barrierDisplay.accessMask = vk::AccessFlagBits2::eShaderWrite;

        Image *depth = GetGlobalSystem<RendererSystem>()->GetDepthStencilRT();
        ImageBarrierInfo barrierDepth{};
        barrierDepth.image = depth;
        barrierDepth.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrierDepth.stageFlags = vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
        barrierDepth.accessMask = vk::AccessFlagBits2::eShaderRead;

        cmd->BeginDebugRegion("RayTracingPass");
        cmd->ImageBarriers({barrierDisplay, barrierDepth});
        cmd->BindPipeline(*m_passInfo);
        cmd->SetConstantAt(0, MAX_POINT_LIGHTS);
        cmd->SetConstantAt(1, MAX_SPOT_LIGHTS);
        cmd->SetConstantAt(2, MAX_AREA_LIGHTS);
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
