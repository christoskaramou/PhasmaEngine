#include "ReflectionPass.h"
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
    namespace
    {
        std::vector<Image *> ScreenProbeImages()
        {
            auto *screenProbes = GetGlobalComponent<ScreenProbePass>();
            if (!screenProbes || !screenProbes->GetFilteredSH())
                return {};
            return {screenProbes->GetFilteredSH(), screenProbes->GetProbePositions(), screenProbes->GetProbeNormals(), screenProbes->GetGeometry(),
                    screenProbes->GetAdaptiveMap()};
        }
    } // namespace

    ReflectionPass::ReflectionPass() : m_resolvePass(std::make_shared<PassInfo>())
    {
        m_probeTracing = true;
        m_reflections = true;
    }

    void ReflectionPass::Init()
    {
        auto &rs = RequireActiveSceneRendererHost();
        m_depth = rs.GetDepthStencilRT();
        m_normal = rs.GetRenderTarget("normal");
        m_velocity = rs.GetRenderTarget("velocity");
        m_material = rs.GetRenderTarget("srm");
        m_rtDepth = nullptr;
        CreateImages();
        m_uniforms.resize(RHII.GetSwapchainImageCount());
        m_lrUniforms.resize(RHII.GetSwapchainImageCount());
        m_boundGIUniforms.assign(RHII.GetSwapchainImageCount(), nullptr);
    }

    void ReflectionPass::CreateImages()
    {
        const uint32_t width = m_normal->GetWidth();
        const uint32_t height = m_normal->GetHeight();
        if (m_specular && m_data.width == width && m_data.height == height)
            return;
        DestroyImages();
        m_data.width = width;
        m_data.height = height;
        m_data.tilesX = (width + 15) / 16;
        m_data.tilesY = (height + 15) / 16;
        auto create = [](const char *name, uint32_t w, uint32_t h)
        {
            ImageDesc desc{};
            desc.width = w;
            desc.height = h;
            desc.format = PE_FORMAT_R16G16B16A16_SFLOAT;
            desc.usage = PE_IMAGE_USAGE_SAMPLED | PE_IMAGE_USAGE_STORAGE | PE_IMAGE_USAGE_TRANSFER_SRC | PE_IMAGE_USAGE_TRANSFER_DST;
            desc.clearColor = vec4(0.f);
            desc.name = name;
            Image *image = Image::Create(desc);
            image->CreateSRV(PE_IMAGE_VIEW_TYPE_2D);
            image->CreateUAV(PE_IMAGE_VIEW_TYPE_2D, 0);
            return image;
        };
        m_display = create("LR_Rays", width, height); // one ray per pixel
        m_display->SetSampler(Sampler::Create(Sampler::CreateInfoInit(), "LR_MaterialSampler"));
        m_specular = create("LR_Specular", width, height);
        m_history = create("LR_SpecularHistory", width, height);
        m_lastUpdate = 0; // new images hold no history
    }

    void ReflectionPass::DestroyImages()
    {
        for (Image **image : {&m_display, &m_specular, &m_history})
        {
            Image::Destroy(*image);
            *image = nullptr;
        }
    }

    void ReflectionPass::UpdatePassInfo()
    {
        RayTracingPass::UpdatePassInfo();
        m_resolvePass->name = "LR_Resolve";
        m_resolvePass->pCompShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/GlobalIllumination/ReflectionResolve.hlsl", .entryPoint = "main", .stage = PE_SHADER_STAGE_COMPUTE});
        m_resolvePass->Update();
    }

    void ReflectionPass::CreateUniforms(CommandBuffer *cmd)
    {
        for (auto &uniform : m_lrUniforms)
            uniform = Buffer::Create({.size = RHII.AlignUniform(sizeof(ScreenProbeUniforms)), .usage = PE_BUFFER_USAGE_UNIFORM_BUFFER, .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU, .name = "LR_Uniforms"});
        RayTracingPass::CreateUniforms(cmd);
    }

    void ReflectionPass::UpdateDescriptorSets()
    {
        RayTracingPass::UpdateDescriptorSets();
        auto *gi = GetGlobalComponent<GlobalIlluminationPass>();
        const std::vector<Image *> probes = ScreenProbeImages();
        m_boundProbeImages = probes;
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
                set->SetImageView(6, m_depth->GetSRV());
                set->SetImageView(7, m_normal->GetSRV());
                set->SetImageView(8, m_material->GetSRV());
                set->Update();
                m_boundGIUniforms[frame] = giUniform;
            }
            if (probes.empty())
                continue;
            auto *resolve = m_resolvePass->GetDescriptors(frame)[0];
            resolve->SetBuffer(14, m_lrUniforms[frame]);
            resolve->SetImageView(3, m_display->GetSRV());
            resolve->SetImageView(4, m_depth->GetSRV());
            resolve->SetImageView(5, m_normal->GetSRV());
            resolve->SetImageView(6, m_material->GetSRV());
            resolve->SetImageView(7, probes[0]->GetSRV());
            resolve->SetImageView(10, probes[1]->GetSRV());
            resolve->SetImageView(11, probes[2]->GetSRV());
            resolve->SetImageView(12, probes[3]->GetSRV());
            resolve->SetImageView(13, m_history->GetSRV());
            resolve->SetImageView(15, m_specular->GetUAV(0));
            resolve->SetImageView(16, (m_velocity ? m_velocity : m_normal)->GetSRV());
            resolve->SetImageView(17, probes[4]->GetSRV());
            resolve->Update();
        }
    }

    void ReflectionPass::Update()
    {
        RayTracingPass::Update();
        Scene *scene = GetActiveScene();
        Camera *camera = scene->GetActiveCamera();
        const uint32_t frame = RHII.GetFrameIndex();
        auto *gi = GetGlobalComponent<GlobalIlluminationPass>();
        if ((gi && gi->GetProbeUniform(frame) && m_boundGIUniforms[frame] != gi->GetProbeUniform(frame)) || m_boundProbeImages != ScreenProbeImages())
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
        m_data.frame = static_cast<uint32_t>(RHII.GetFrameCounter()); // the GI uniform's frame, which the rays use
        m_data.reset = reset ? 1u : 0u;
        m_data.viewProj = camera->GetProjectionNoJitter() * camera->GetView();
        m_pendingViewProj = m_data.viewProj;
        m_pendingCamPos = m_data.camPos;
        BufferRange range{};
        range.data = &m_data;
        range.size = sizeof(m_data);
        m_lrUniforms[frame]->Copy(1, &range, false);
    }

    void ReflectionPass::DeclareInputs(RGBuilder &builder)
    {
        for (Image *image : {m_depth, m_normal, m_material})
        {
            builder.ReadRayTracing(image);
            builder.ReadCompute(image);
        }
        builder.WriteRayTracing(m_display);
        if (auto *gi = GetGlobalComponent<GlobalIlluminationPass>(); gi && gi->GetIrradianceHistory())
        {
            builder.ReadRayTracing(gi->GetIrradianceHistory());
            builder.ReadRayTracing(gi->GetDistanceHistory());
        }
        for (Image *image : ScreenProbeImages())
            builder.ReadCompute(image);
        builder.ReadCompute(m_history);
        if (m_velocity)
            builder.ReadCompute(m_velocity);
        builder.WriteCompute(m_specular);
    }

    void ReflectionPass::DeclareOutputs(RGBuilder &builder)
    {
        builder.OutputCustom(m_display, PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, PE_STAGE_COMPUTE_SHADER, PE_ACCESS_SHADER_READ);
        builder.OutputCustom(m_specular, PE_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, PE_STAGE_TRANSFER, PE_ACCESS_TRANSFER_READ);
        builder.OutputCustom(m_history, PE_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, PE_STAGE_TRANSFER, PE_ACCESS_TRANSFER_WRITE);
    }

    void ReflectionPass::ExecutePass(CommandBuffer *cmd)
    {
        if (!m_scene || !m_scene->GetTLAS() || m_boundProbeImages.empty())
            return;
        Scene *scene = m_scene;
        RayTracingPass::ExecutePass(cmd);
        cmd->BeginDebugRegion("Reflections");
        cmd->ImageBarrier({.image = m_display, .layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .stageFlags = PE_STAGE_COMPUTE_SHADER, .accessMask = PE_ACCESS_SHADER_READ});
        cmd->BindPipeline(*m_resolvePass);
        cmd->Dispatch((m_data.width + 7) / 8, (m_data.height + 7) / 8, 1);
        cmd->CopyImage(m_specular, m_history);
        cmd->EndDebugRegion();
        m_historyScene = scene;
        m_historyGeneration = scene->GetGeneration();
        m_viewProj = m_pendingViewProj;
        m_camPos = m_pendingCamPos;
        m_lastUpdate = RequireActiveSceneRendererHost().GetRenderedFrames() + 1; // this frame's number
    }

    void ReflectionPass::Resize(uint32_t width, uint32_t height)
    {
        auto &rs = RequireActiveSceneRendererHost();
        m_depth = rs.GetDepthStencilRT();
        m_normal = rs.GetRenderTarget("normal");
        m_velocity = rs.GetRenderTarget("velocity");
        m_material = rs.GetRenderTarget("srm");
        CreateImages();
        UpdateDescriptorSets();
    }

    void ReflectionPass::Destroy()
    {
        RayTracingPass::Destroy();
        for (auto *uniform : m_lrUniforms)
            Buffer::Destroy(uniform);
        m_lrUniforms.clear();
        DestroyImages();
        m_rtDepth = nullptr;
        m_historyScene = nullptr;
        m_boundProbeImages.clear();
    }

    std::vector<PassInfo *> ReflectionPass::GetPassInfos() noexcept
    {
        return {m_passInfo.get(), m_resolvePass.get()};
    }
} // namespace pe
