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
#include "GbufferPass.h"
#include "Scene/Scene.h"
#include "Systems/LightSystem.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    void RayTracingPass::Init()
    {
        m_scene = nullptr;
        m_display = Context::Get()->GetSystem<RendererSystem>()->GetViewportRT(); // Write to Viewport for PostProc
        m_uniforms.resize(RHII.GetSwapchainImageCount());
    }

    void RayTracingPass::UpdatePassInfo()
    {
        std::vector<Define> defines = {
            Define{"MAX_POINT_LIGHTS", std::to_string(MAX_POINT_LIGHTS)},
            Define{"MAX_SPOT_LIGHTS", std::to_string(MAX_SPOT_LIGHTS)},
        };

        // Shaders
        Shader *rayGen = Shader::Create(Path::Assets + "Shaders/RayTracing/RayTrace.hlsl", vk::ShaderStageFlagBits::eRaygenKHR, "raygeneration", defines, ShaderCodeType::HLSL);
        Shader *closestHit = Shader::Create(Path::Assets + "Shaders/RayTracing/RayTrace.hlsl", vk::ShaderStageFlagBits::eClosestHitKHR, "closesthit", defines, ShaderCodeType::HLSL);
        Shader *miss = Shader::Create(Path::Assets + "Shaders/RayTracing/RayTrace.hlsl", vk::ShaderStageFlagBits::eMissKHR, "miss", defines, ShaderCodeType::HLSL);

        m_passInfo->name = "RayTracingPipeline";
        m_passInfo->acceleration.rayGen = rayGen;
        m_passInfo->acceleration.miss = {miss};
        m_passInfo->acceleration.hitGroups = {{.closestHit = closestHit}};
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

        auto *gbuffer = GetGlobalComponent<GbufferOpaquePass>();

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
                desc->SetBuffer(3, gbuffer->GetConstants());
                desc->SetSampler(4, m_display->GetSampler());
                desc->SetImageViews(5, scene.GetImageViews());
                desc->SetBuffer(6, scene.GetBuffer());
                desc->SetBuffer(7, scene.GetMeshInfoBuffer());
                desc->SetImageView(8, skybox->GetSRV());
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
        Camera *camera = GetGlobalSystem<RendererSystem>()->GetScene().GetCamera(0);
        auto &gSettings = Settings::Get<GlobalSettings>();

        RayTracingPassUBO ubo{};
        ubo.invViewProj = camera->GetInvViewProjection();
        ubo.invView = camera->GetInvView();
        ubo.invProj = camera->GetInvProjection();
        ubo.lights_intensity = gSettings.lights_intensity;
        ubo.lights_range = gSettings.lights_range;
        ubo.shadows = gSettings.shadows ? 1 : 0;

        BufferRange range{};
        range.data = &ubo;
        range.size = sizeof(ubo);
        range.offset = 0;
        m_uniforms[RHII.GetFrameIndex()]->Copy(1, &range, false);
    }

    void RayTracingPass::ExecutePass(CommandBuffer *cmd)
    {
        if (!m_scene)
            return;

        AccelerationStructure *tlas = m_scene->GetTLAS();
        if (!tlas)
            return;
        if (m_tlas != tlas)
        {
            UpdateDescriptorSets();
            m_tlas = tlas;
        }

        ImageBarrierInfo barrier{};
        barrier.image = m_display;
        barrier.layout = vk::ImageLayout::eGeneral;
        barrier.stageFlags = vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
        barrier.accessMask = vk::AccessFlagBits2::eShaderWrite;

        cmd->BeginDebugRegion("RayTracingPass");
        cmd->ImageBarrier(barrier);
        cmd->BindPipeline(*m_passInfo);
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
