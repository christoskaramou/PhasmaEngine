#include "RayTracingPass.h"
#include "API/AccelerationStructure.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/Shader.h"
#include "Scene/Scene.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    void RayTracingPass::Init()
    {
        m_scene = nullptr;
        m_display = Context::Get()->GetSystem<RendererSystem>()->GetDisplayRT();
    }

    void RayTracingPass::UpdatePassInfo()
    {
        // Shaders
        Shader *rayGen = Shader::Create(Path::Assets + "Shaders/RayTracing/RayGen.hlsl", vk::ShaderStageFlagBits::eRaygenKHR, "main", std::vector<Define>{}, ShaderCodeType::HLSL);
        Shader *miss = Shader::Create(Path::Assets + "Shaders/RayTracing/Miss.hlsl", vk::ShaderStageFlagBits::eMissKHR, "main", std::vector<Define>{}, ShaderCodeType::HLSL);
        Shader *closestHit = Shader::Create(Path::Assets + "Shaders/RayTracing/ClosestHit.hlsl", vk::ShaderStageFlagBits::eClosestHitKHR, "main", std::vector<Define>{}, ShaderCodeType::HLSL);

        m_passInfo->name = "RayTracingPipeline";
        m_passInfo->acceleration.rayGen = rayGen;
        m_passInfo->acceleration.miss = {miss};
        m_passInfo->acceleration.hitGroups = {{.closestHit = closestHit}};
        m_passInfo->Update();
    }

    void RayTracingPass::CreateUniforms(CommandBuffer *cmd)
    {
        UpdateDescriptorSets();
    }

    void RayTracingPass::UpdateDescriptorSets()
    {
        Scene &scene = GetGlobalSystem<RendererSystem>()->GetScene();
        if (!scene.GetTLAS())
            return;

        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
        {
            auto *DSet = m_passInfo->GetDescriptors(i)[0];
            DSet->SetAccelerationStructure(0, scene.GetTLAS()->ApiHandle());
            DSet->SetImageView(1, m_display->GetUAV(0), m_display->GetSampler()->ApiHandle());
            DSet->SetBuffer(2, scene.GetUniforms(i));
            DSet->SetBuffer(3, scene.GetMeshInfoBuffer());
            DSet->SetBuffer(4, scene.GetBuffer());
            DSet->SetBuffer(5, scene.GetBuffer(), scene.GetVerticesOffset());
            DSet->SetSampler(6, m_display->GetSampler()->ApiHandle());
            DSet->SetImageViews(6, scene.GetImageViews(), {});
            DSet->SetBuffer(7, scene.GetBuffer(), scene.GetPositionsOffset());
            DSet->Update();
        }
    }

    void RayTracingPass::Update()
    {
    }

    void RayTracingPass::Draw(CommandBuffer *cmd)
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
    }

} // namespace pe
