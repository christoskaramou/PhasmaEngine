#include "RendererSystem.h"
#ifdef PE_TRACY
#include <tracy/TracyVulkan.hpp>
#endif
#include "PhasmaMCP/Utils.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Framebuffer.h"
#include "API/Image.h"
#include "API/Vulkan/VulkanCommandBufferImpl.h"
#include "API/Vulkan/VulkanImageImpl.h"
#include "API/Vulkan/Helpers_Vulkan.h"
#include "API/Vulkan/VulkanSamplerImpl.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Semaphore.h"
#include "API/Shader.h"
#include "API/StagingManager.h"
#include "API/Surface.h"
#include "API/Swapchain.h"
#include "RenderPasses/AabbsPass.h"
#include "RenderPasses/CullingPass.h"
#include "RenderPasses/BloomPass.h"
#include "RenderPasses/DOFPass.h"
#include "RenderPasses/DepthPass.h"
#include "RenderPasses/FXAAPass.h"
#include "RenderPasses/GbufferPass.h"
#include "RenderPasses/GridPass.h"
#include "RenderPasses/LightPass.h"
#include "RenderPasses/MotionBlurPass.h"
#include "RenderPasses/ParticleComputePass.h"
#include "RenderPasses/ParticlePass.h"
#include "RenderPasses/RayTracingPass.h"
#include "RenderPasses/SSAOPass.h"
#include "RenderPasses/SSRPass.h"
#include "RenderPasses/ShadowPass.h"
#include "RenderPasses/SharpenPass.h"
#include "RenderPasses/TAAPass.h"
#include "RenderPasses/TonemapPass.h"

namespace pe
{
    namespace
    {
        ::PeFormat GetSwapchainSurfaceFormat()
        {
            if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
            {
                Swapchain *swapchain = RHII.GetSwapchain();
                if (swapchain && swapchain->GetImageCount() > 0 && swapchain->GetImage(0))
                    return swapchain->GetImage(0)->GetFormat();
                return PE_FORMAT_R8G8B8A8_UNORM;
            }

            return pe::FromVkFormat(RHII.GetSurface()->GetFormat());
        }
    } // namespace

    void RendererSystem::LoadResources(CommandBuffer *cmd)
    {
        m_skyBoxDay.LoadSkyBox(cmd, Path::Assets + "Skyboxes/golden_gate_hills/golden_gate_hills_4k.hdr");
        m_skyBoxNight.LoadSkyBox(cmd, Path::Assets + "Skyboxes/rogland_clear_night/rogland_clear_night_4k.hdr");

        Image::LoadRawParams loadImageParams = {256, 256, PE_FORMAT_R16G16_SFLOAT, false, true, 0.0f};
        m_ibl_brdf_lut = Image::LoadRaw(cmd, Path::Assets + "Objects/ibl_brdf_lut_rg16f_256.bin", loadImageParams);
    }

    void RendererSystem::Init(CommandBuffer *cmd)
    {
        const bool isDx12 = RHII.GetApi() == PE_GRAPHICS_API_DX12;

        // Set Window Title
        std::string title = "PhasmaEngine";
        title += " - Device: " + RHII.GetGpuName();
        title += isDx12 ? " - API: DX12" : " - API: Vulkan";
        title += " - Present Mode: " + std::string(RHII.PresentModeToString(RHII.GetSwapchain()->GetPresentMode()));
#if PE_DEBUG
        title += " - Debug";
#elif PE_RELEASE
        title += " - Release";
#elif PE_MINSIZEREL
        title += " - MinSizeRel";
#elif PE_RELWITHDEBINFO
        title += " - RelWithDebInfo";
#endif

        EventSystem::DispatchEvent(EventType::SetWindowTitle, title);

        Queue *queue = RHII.GetMainQueue();
        CommandBuffer *initCmd = cmd;
        const bool ownsInitCmd = isDx12 && initCmd == nullptr;
        if (ownsInitCmd)
        {
            initCmd = queue->AcquireCommandBuffer();
            initCmd->Begin();
        }

        CreateRenderTargets();

        // Skybox / IBL are consumed by the Light pass. DX12 reaches that slice in 14c.
        LoadResources(initCmd);

        // 14c: DX12 wires the raster lighting and particle passes. Post/TAA/Sharpen/RT stay Vulkan-only.
        m_renderPassComponents[ID::GetTypeID<CullingPass>()] = CreateGlobalComponent<CullingPass>();
        m_renderPassComponents[ID::GetTypeID<ShadowPass>()] = CreateGlobalComponent<ShadowPass>();
        m_renderPassComponents[ID::GetTypeID<DepthPass>()] = CreateGlobalComponent<DepthPass>();
        m_renderPassComponents[ID::GetTypeID<GbufferOpaquePass>()] = CreateGlobalComponent<GbufferOpaquePass>();
        m_renderPassComponents[ID::GetTypeID<GbufferTransparentPass>()] = CreateGlobalComponent<GbufferTransparentPass>();
        m_renderPassComponents[ID::GetTypeID<AabbsPass>()] = CreateGlobalComponent<AabbsPass>();
        m_renderPassComponents[ID::GetTypeID<GridPass>()] = CreateGlobalComponent<GridPass>();
        m_renderPassComponents[ID::GetTypeID<LightOpaquePass>()] = CreateGlobalComponent<LightOpaquePass>();
        m_renderPassComponents[ID::GetTypeID<LightTransparentPass>()] = CreateGlobalComponent<LightTransparentPass>();
        m_renderPassComponents[ID::GetTypeID<ParticleComputePass>()] = CreateGlobalComponent<ParticleComputePass>();
        m_renderPassComponents[ID::GetTypeID<ParticlePass>()] = CreateGlobalComponent<ParticlePass>();
        if (isDx12)
            m_renderPassComponents[ID::GetTypeID<SSRPass>()] = CreateGlobalComponent<SSRPass>();
        if (!isDx12)
        {
            m_renderPassComponents[ID::GetTypeID<TAAPass>()] = CreateGlobalComponent<TAAPass>();
            m_renderPassComponents[ID::GetTypeID<SharpenPass>()] = CreateGlobalComponent<SharpenPass>();
            m_renderPassComponents[ID::GetTypeID<RayTracingPass>()] = CreateGlobalComponent<RayTracingPass>();
        }

        for (auto &renderPassComponent : m_renderPassComponents)
        {
            renderPassComponent->Init();
            renderPassComponent->UpdatePassInfo();
            renderPassComponent->CreateUniforms(initCmd);
        }

        // Init GUI
        if (!isDx12)
            m_gui.Init();
        else
            m_gui.InitAgentServices();

        uint32_t imageCount = RHII.GetSwapchainImageCount();
        m_cmds.resize(imageCount, nullptr);
        if (!isDx12)
        {
            for (uint32_t i = 0; i < imageCount; i++)
            {
                ImageBarrierInfo barrierInfo{};
                barrierInfo.image = RHII.GetSwapchain()->GetImage(i);
                barrierInfo.layout = PE_IMAGE_LAYOUT_PRESENT_SRC;
                barrierInfo.stageFlags = PE_STAGE_ALL_COMMANDS;
                barrierInfo.accessMask = PE_ACCESS_NONE;
                initCmd->ImageBarrier(barrierInfo); // transition from undefined to present
            }
        }

        m_scene.UploadBuffers(initCmd);

        // On DX12 PostProcessSystem isn't initialized yet, so cache pointers and build
        // the render graph here. On Vulkan PostProcessSystem::Init does this.
        if (isDx12)
        {
            CacheGlobalComponents();
            BuildRenderGraph();
        }

        m_acquireSemaphores.reserve(imageCount);
        m_submitSemaphores.reserve(imageCount);
        for (uint32_t i = 0; i < imageCount; i++)
        {
            Semaphore *acquireSemaphore = Semaphore::Create(false, "AcquireSemaphore_" + std::to_string(i));
            if (!isDx12)
                acquireSemaphore->SetStageFlags(vk::PipelineStageFlagBits2::eColorAttachmentOutput | vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eRayTracingShaderKHR | vk::PipelineStageFlagBits2::eTransfer);
            m_acquireSemaphores.push_back(acquireSemaphore);

            Semaphore *submitSemaphore = Semaphore::Create(false, "SubmitSemaphore_" + std::to_string(i));
            if (!isDx12)
                submitSemaphore->SetStageFlags(vk::PipelineStageFlagBits2::eAllCommands);
            m_submitSemaphores.push_back(submitSemaphore);
        }

        if (ownsInitCmd)
        {
            initCmd->End();
            queue->Submit(1, &initCmd, nullptr, nullptr);
            initCmd->Wait();
            initCmd->Return();
        }
    }

    void RendererSystem::CacheGlobalComponents()
    {
        Entity *world = Context::Get()->GetWorldEntity();

        m_cullingPass = world->GetComponent<CullingPass>();
        m_shadowPass = world->GetComponent<ShadowPass>();
        m_depthPass = world->GetComponent<DepthPass>();
        m_gbufferOpaquePass = world->GetComponent<GbufferOpaquePass>();
        m_gbufferTransparentPass = world->GetComponent<GbufferTransparentPass>();
        m_ssaoPass = world->GetComponent<SSAOPass>();
        m_lightOpaquePass = world->GetComponent<LightOpaquePass>();
        m_lightTransparentPass = world->GetComponent<LightTransparentPass>();
        m_rayTracingPass = world->GetComponent<RayTracingPass>();
        m_particleComputePass = world->GetComponent<ParticleComputePass>();
        m_particlePass = world->GetComponent<ParticlePass>();
        m_ssrPass = world->GetComponent<SSRPass>();
        m_fxaaPass = world->GetComponent<FXAAPass>();
        m_aabbsPass = world->GetComponent<AabbsPass>();
        m_taaPass = world->GetComponent<TAAPass>();
        m_sharpenPass = world->GetComponent<SharpenPass>();
        m_tonemapPass = world->GetComponent<TonemapPass>();
        m_bloomBrightFilterPass = world->GetComponent<BloomBrightFilterPass>();
        m_bloomGaussianBlurHorizontalPass = world->GetComponent<BloomGaussianBlurHorizontalPass>();
        m_bloomGaussianBlurVerticalPass = world->GetComponent<BloomGaussianBlurVerticalPass>();
        m_dofPass = world->GetComponent<DOFPass>();
        m_motionBlurPass = world->GetComponent<MotionBlurPass>();
        m_gridPass = world->GetComponent<GridPass>();
    }

    void RendererSystem::Update()
    {
        const bool isVulkan = RHII.GetApi() == PE_GRAPHICS_API_VULKAN;

        // GUI is Vulkan-only until Task 14e wires the DX12 ImGui backend
        if (isVulkan)
        {
            PE_PROFILE_SCOPE("GUI");
            m_gui.Update();
        }
        else
        {
            PE_PROFILE_SCOPE("GUI Agent Actions");
            m_gui.PumpMainThreadActions();
        }

        // Flush any deferred GPU work (primitive batching, async load completion)
        // MUST happen before Scene::Update() so that rebuilt buffers exist
        // before UpdateUniformData/UpdateIndirectData populate them.
        if (m_scene.IsGeometryDirty())
            WaitAllFramesCommands();
        m_scene.FlushPendingGpuWork();

        // Scene (populates uniforms/indirects into the current buffers)
        {
            PE_PROFILE_SCOPE("Scene");
            m_scene.Update();
        }

        {
            PE_PROFILE_SCOPE("Render Graph Pass States");
            UpdateRenderGraphPassStates();
        }

        const auto isPassEnabled = [&](RenderGraphPassId passId) -> bool
        {
            return m_renderGraphPassEnabled[static_cast<size_t>(passId)];
        };

        auto shouldUpdate = [&](IRenderPassComponent *rc)
        {
            if (!rc || !rc->IsEnabled())
                return false;

            if (rc == m_shadowPass)
                return isPassEnabled(RenderGraphPassId::Shadow);
            if (rc == m_depthPass)
                return isPassEnabled(RenderGraphPassId::Depth);
            if (rc == m_gbufferOpaquePass)
                return isPassEnabled(RenderGraphPassId::GBufferOpaque);
            if (rc == m_gbufferTransparentPass)
                return isPassEnabled(RenderGraphPassId::GBufferTransparent);
            if (rc == m_lightOpaquePass)
                return isPassEnabled(RenderGraphPassId::LightOpaque);
            if (rc == m_lightTransparentPass)
                return isPassEnabled(RenderGraphPassId::LightTransparent);
            if (rc == m_aabbsPass)
                return isPassEnabled(RenderGraphPassId::Aabbs);
            if (rc == m_particleComputePass || rc == m_particlePass)
                return true;
            if (rc == m_ssrPass)
                return isPassEnabled(RenderGraphPassId::SSR);
            if (rc == m_taaPass)
                return isPassEnabled(RenderGraphPassId::TAA);
            if (rc == m_sharpenPass)
                return isPassEnabled(RenderGraphPassId::Sharpen);
            if (rc == m_rayTracingPass)
                return isPassEnabled(RenderGraphPassId::RayTracing);
            if (rc == m_gridPass)
                return isPassEnabled(RenderGraphPassId::Grid);

            return true;
        };

        // Render Components
        {
            PE_PROFILE_SCOPE("Render Pass Updates");
            std::vector<std::shared_future<void>> futures;
            futures.reserve(m_renderPassComponents.size());
            for (auto &rc : m_renderPassComponents)
            {
                if (!shouldUpdate(rc))
                    continue;

                futures.push_back(ThreadPool::Update.Enqueue([rc]()
                                                             { rc->Update(); }));
            }

            for (auto &future : futures)
                future.wait();
        }
    }

    void RendererSystem::UpdateRenderGraphPassStates()
    {
        const auto &gs = Settings::Get<GlobalSettings>();

        // No TLAS: fall back to raster so m_display still gets painted.
        const bool hasRTGeom = m_scene.GetTLAS() != nullptr;
        const bool renderRaster = (gs.render_mode != RenderMode::RayTracing) || !hasRTGeom;
        const bool renderRayTracing = (gs.render_mode != RenderMode::Raster) && hasRTGeom;
        const bool needVelocity = gs.taa || gs.motion_blur;
        const bool renderSSR = gs.ssr && renderRaster;
        const bool renderSSAO = gs.ssao && renderRaster;
        const bool needDepth = renderRaster || needVelocity || gs.dof || gs.motion_blur || gs.draw_aabbs || gs.draw_grid;
        const bool needGBuffer = renderRaster || needVelocity || renderSSR || renderSSAO;

        if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
        {
            // DX12 Phase 1 keeps ray tracing and post/ImGui disabled, but falls back to
            // the raster path even when a user setting requests RT-only rendering.
            const bool dx12RayTracing = RHII.GetCaps().rayTracing && renderRayTracing;
            const bool dx12RenderRaster = renderRaster || !dx12RayTracing;
            const bool dx12NeedDepth = dx12RenderRaster || gs.draw_aabbs || gs.draw_grid;
            const bool dx12NeedGBuffer = dx12RenderRaster;

            m_renderGraphPassEnabled.fill(false);
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::Culling)] = true;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::Shadow)] = gs.shadows && dx12RenderRaster;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::Depth)] = dx12NeedDepth;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::GBufferOpaque)] = dx12NeedGBuffer;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::LightOpaque)] = dx12RenderRaster;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::GBufferTransparent)] = dx12RenderRaster;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::LightTransparent)] = dx12RenderRaster;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::RayTracing)] = dx12RayTracing;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::ParticleCompute)] = dx12RenderRaster;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::Particle)] = dx12RenderRaster;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::SSR)] = gs.ssr && dx12RenderRaster;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::Upsample)] = dx12RenderRaster;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::Aabbs)] = gs.draw_aabbs;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::Grid)] = gs.draw_grid;
            return;
        }

        m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::Culling)] = true;
        m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::Shadow)] = gs.shadows && renderRaster;
        m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::Depth)] = needDepth;
        m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::GBufferOpaque)] = needGBuffer;
        m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::SSAO)] = renderSSAO;
        m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::LightOpaque)] = renderRaster;
        m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::GBufferTransparent)] = gs.render_mode == RenderMode::Raster;
        m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::LightTransparent)] = renderRaster;
        m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::RayTracing)] = renderRayTracing;
        m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::ParticleCompute)] = true;
        m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::Particle)] = true;
        m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::SSR)] = renderSSR;
        m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::FXAA)] = gs.fxaa;
        m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::Aabbs)] = gs.draw_aabbs;
        m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::TAA)] = gs.taa;
        m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::Sharpen)] = gs.taa && gs.cas_sharpening;
        m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::Upsample)] = !gs.taa;
        m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::Tonemap)] = gs.tonemapping;
        m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::BloomBF)] = gs.bloom;
        m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::BloomH)] = gs.bloom;
        m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::BloomV)] = gs.bloom;
        m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::DOF)] = gs.dof;
        m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::MotionBlur)] = gs.motion_blur;
        m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::Grid)] = gs.draw_grid;
        m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::GUI)] = RHII.GetApi() == PE_GRAPHICS_API_VULKAN && m_gui.Render();
    }

    void RendererSystem::WaitPreviousFrameCommands()
    {
        uint32_t frame = RHII.GetFrameIndex();

        auto &frameCmd = m_cmds[frame];
        if (frameCmd)
        {
            PE_PROFILE_SCOPE("Cmd Wait");
            frameCmd->Wait();
            frameCmd->Return();
            frameCmd = nullptr;
        }

        {
            PE_PROFILE_SCOPE("Flush Deletion Queue");
            RHII.FlushDeletionQueue(frame);
        }
        {
            PE_PROFILE_SCOPE("Staging Cleanup");
            RHII.GetStagingManager()->RemoveUnused();
        }
    }

    void RendererSystem::ResetTAAHistory()
    {
        if (m_taaPass)
            m_taaPass->RequestHistoryReset();
    }

    void RendererSystem::WaitAllFramesCommands()
    {
        for (auto &frameCmd : m_cmds)
        {
            if (frameCmd)
            {
                frameCmd->Wait();
                frameCmd->Return();
                frameCmd = nullptr;
            }
        }

        RHII.GetStagingManager()->RemoveUnused();
    }

    void RendererSystem::BuildRenderGraph()
    {
        m_renderGraph.Clear();

        UpdateRenderGraphPassStates();

        auto isPassEnabled = [this](RenderGraphPassId passId)
        {
            return [this, passId]()
            {
                return m_renderGraphPassEnabled[static_cast<size_t>(passId)];
            };
        };

        auto addPass = [&](RenderGraphPassId passId, uint32_t order, const std::string &name, IRenderPassComponent *component)
        {
            m_renderGraph.AddPass(static_cast<RenderGraph::PassID>(passId), order, name, isPassEnabled(passId), component);
        };

        addPass(RenderGraphPassId::Culling, 50, "Culling", m_cullingPass);
        addPass(RenderGraphPassId::Shadow, 100, "Shadow", m_shadowPass);
        addPass(RenderGraphPassId::Depth, 200, "Depth", m_depthPass);
        addPass(RenderGraphPassId::GBufferOpaque, 300, "GBufferOpaque", m_gbufferOpaquePass);
        addPass(RenderGraphPassId::SSAO, 400, "SSAO", m_ssaoPass);
        addPass(RenderGraphPassId::LightOpaque, 500, "LightOpaque", m_lightOpaquePass);
        addPass(RenderGraphPassId::GBufferTransparent, 600, "GBufferTransparent", m_gbufferTransparentPass);
        addPass(RenderGraphPassId::LightTransparent, 700, "LightTransparent", m_lightTransparentPass);
        addPass(RenderGraphPassId::RayTracing, 800, "RayTracing", m_rayTracingPass);
        addPass(RenderGraphPassId::ParticleCompute, 900, "ParticleCompute", m_particleComputePass);
        addPass(RenderGraphPassId::SSR, 1000, "SSR", m_ssrPass);
        addPass(RenderGraphPassId::FXAA, 1100, "FXAA", m_fxaaPass);
        addPass(RenderGraphPassId::Aabbs, 1200, "Aabbs", m_aabbsPass);
        addPass(RenderGraphPassId::TAA, 1300, "TAA", m_taaPass);
        addPass(RenderGraphPassId::Sharpen, 1400, "Sharpen", m_sharpenPass);
        m_renderGraph.AddPass(static_cast<RenderGraph::PassID>(RenderGraphPassId::Upsample), 1500, "Upsample", isPassEnabled(RenderGraphPassId::Upsample),
                              [this](CommandBuffer *cmd)
                              { Upsample(cmd, PE_FILTER_LINEAR); });
        addPass(RenderGraphPassId::Tonemap, 1600, "Tonemap", m_tonemapPass);
        addPass(RenderGraphPassId::BloomBF, 1700, "BloomBF", m_bloomBrightFilterPass);
        addPass(RenderGraphPassId::BloomH, 1800, "BloomH", m_bloomGaussianBlurHorizontalPass);
        addPass(RenderGraphPassId::BloomV, 1900, "BloomV", m_bloomGaussianBlurVerticalPass);
        addPass(RenderGraphPassId::DOF, 2000, "DOF", m_dofPass);
        addPass(RenderGraphPassId::MotionBlur, 2100, "MotionBlur", m_motionBlurPass);
        addPass(RenderGraphPassId::Grid, 2200, "Grid", m_gridPass);
        addPass(RenderGraphPassId::Particle, 2300, "Particle", m_particlePass);
        m_renderGraph.AddPass(static_cast<RenderGraph::PassID>(RenderGraphPassId::GUI), 10000, "GUI", isPassEnabled(RenderGraphPassId::GUI),
                              [this](CommandBuffer *cmd)
                              { m_gui.ExecutePass(cmd); });
        m_renderGraph.Compile();
    }

    CommandBuffer *RendererSystem::RecordPasses(uint32_t imageIndex)
    {
        CommandBuffer *cmd = RHII.GetMainQueue()->AcquireCommandBuffer();
        const bool isDx12 = RHII.GetApi() == PE_GRAPHICS_API_DX12;
        Image *dx12SwapchainImage = isDx12 ? RHII.GetSwapchain()->GetImage(imageIndex) : nullptr;

        // Set scene on all scene-dependent passes before execution.
        auto setScene = [this](auto *pass)
        {
            if (pass)
                pass->SetScene(&m_scene);
        };
        setScene(m_cullingPass);
        setScene(m_shadowPass);
        setScene(m_depthPass);
        setScene(m_gbufferOpaquePass);
        setScene(m_gbufferTransparentPass);
        setScene(m_rayTracingPass);
        setScene(m_particleComputePass);
        setScene(m_particlePass);
        setScene(m_gridPass);
        setScene(m_aabbsPass);

        cmd->Begin();
        {
            PE_PROFILE_SCOPE("Render Graph Execute");
            m_renderGraph.Execute(cmd);
        }

        if (isDx12)
        {
            const bool displayProduced =
                m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::Upsample)] ||
                m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::Grid)];
            const bool viewportProduced =
                m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::LightOpaque)] ||
                m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::LightTransparent)];

            if (displayProduced)
            {
                BlitToSwapchain(cmd, m_displayRT, imageIndex);
            }
            else if (viewportProduced)
            {
                BlitToSwapchain(cmd, m_viewportRT, imageIndex);
            }
            else
            {
                Attachment attachment{};
                attachment.image = dx12SwapchainImage;
                attachment.loadOp = PE_LOAD_OP_CLEAR;
                attachment.storeOp = PE_STORE_OP_STORE;
                cmd->BeginPass(1, &attachment, "DX12FinalClear");
                cmd->EndPass();

                ImageBarrierInfo presentBarrier{};
                presentBarrier.image = dx12SwapchainImage;
                presentBarrier.layout = PE_IMAGE_LAYOUT_PRESENT_SRC;
                presentBarrier.stageFlags = PE_STAGE_ALL_COMMANDS;
                presentBarrier.accessMask = PE_ACCESS_NONE;
                cmd->ImageBarrier(presentBarrier);
            }
        }
        else
        {
            {
                PE_PROFILE_SCOPE("Blit To Swapchain");
                BlitToSwapchain(cmd, m_displayRT, imageIndex);
            }

            EventSystem::QueuedEvent screenshotEvt;
            if (EventSystem::PeekAndPop(EventType::Screenshot, screenshotEvt))
            {
                m_screenshotPath = screenshotEvt.payload.has_value()
                                       ? std::any_cast<std::string>(screenshotEvt.payload)
                                       : std::string();

                cmd->CopyImage(m_displayRT, m_screenshotRT);

                uint32_t w = m_screenshotRT->GetWidth();
                uint32_t h = m_screenshotRT->GetHeight();
                size_t bufferSize = static_cast<size_t>(w) * h * 4;

                Buffer::Destroy(m_screenshotBuffer);
                m_screenshotBuffer = Buffer::Create({
                    .size = bufferSize,
                    .usage = PE_BUFFER_USAGE_TRANSFER_DST,
                    .memoryUsage = PE_MEMORY_USAGE_GPU_TO_CPU,
                    .name = "ScreenshotStaging",
                });

                cmd->CopyImageToBuffer(m_screenshotRT, m_screenshotBuffer);
                m_screenshotPending = true;
            }
        }

#ifdef PE_TRACY
        if (RHII.GetApi() == PE_GRAPHICS_API_VULKAN)
            TracyVkCollect(VulkanRhi::TracyContext(), static_cast<VkCommandBuffer>(GetVulkanCommandBuffer(cmd)));
#endif

        cmd->End();

        return cmd;
    }

    void RendererSystem::Draw()
    {
        try
        {
            uint32_t frame = RHII.GetFrameIndex();

            Semaphore *acquireSemaphore = m_acquireSemaphores[frame];
            Swapchain *swapchain = RHII.GetSwapchain();
            uint32_t imageIndex;
            {
                PE_PROFILE_SCOPE("Acquire Image");
                imageIndex = swapchain->AquireNextImage(acquireSemaphore);
            }

            auto &frameCmd = m_cmds[frame];
            {
                PE_PROFILE_SCOPE("Record Passes");
                frameCmd = RecordPasses(imageIndex);
            }

            Semaphore *submitSemaphore = m_submitSemaphores[imageIndex];
            Queue *queue = RHII.GetMainQueue();
            {
                PE_PROFILE_SCOPE("Queue Submit");
                queue->Submit(1, &frameCmd, acquireSemaphore, submitSemaphore);
            }

            {
                PE_PROFILE_SCOPE("Present");
                queue->Present(swapchain, imageIndex, submitSemaphore);
            }

            if (m_screenshotPending)
            {
                frameCmd->Wait();
                SaveScreenshot();
                m_screenshotPending = false;
            }
        }
        catch (vk::OutOfDateKHRError &)
        {
            // Just ignore and try again
        }
    }

    void RendererSystem::SaveScreenshot()
    {
        if (!m_screenshotBuffer)
            return;

        uint32_t w = m_screenshotRT->GetWidth();
        uint32_t h = m_screenshotRT->GetHeight();

        m_screenshotBuffer->Map();
        const uint8_t *pixels = static_cast<const uint8_t *>(m_screenshotBuffer->Data());

        // Generate path
        std::string path = m_screenshotPath;
        if (path.empty())
        {
            std::string dir = Path::Executable + "Screenshots/";
            std::filesystem::create_directories(dir);

            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::tm tm{};
#if defined(PE_WIN32)
            localtime_s(&tm, &time);
#else
            localtime_r(&time, &tm);
#endif
            char buf[64];
            std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
            path = dir + "screenshot_" + buf + ".png";
        }

        // Convert BGRA to RGBA for PNG encoding
        size_t pixelCount = static_cast<size_t>(w) * h;
        std::vector<uint8_t> rgba(pixelCount * 4);
        for (size_t i = 0; i < pixelCount; i++)
        {
            rgba[i * 4 + 0] = pixels[i * 4 + 2]; // R
            rgba[i * 4 + 1] = pixels[i * 4 + 1]; // G
            rgba[i * 4 + 2] = pixels[i * 4 + 0]; // B
            rgba[i * 4 + 3] = pixels[i * 4 + 3]; // A
        }

        auto pngData = pmcp::EncodeRGBA_PNG(rgba.data(), static_cast<int>(w), static_cast<int>(h));

        std::ofstream file(path, std::ios::binary);
        if (file.is_open())
        {
            file.write(reinterpret_cast<const char *>(pngData.data()), pngData.size());
            file.close();

            PE_INFO("Screenshot saved: %s", path.c_str());
            m_screenshotSavedPath = path;
        }
        else
        {
            PE_ERROR("[Renderer] Failed to save screenshot: %s", path.c_str());
        }

        m_screenshotBuffer->Unmap();
        Buffer::Destroy(m_screenshotBuffer);
    }

    void RendererSystem::DrawPlatformWindows()
    {
        m_gui.DrawPlatformWindows();
    }

    void RendererSystem::Destroy()
    {
        RHII.WaitDeviceIdle();

        for (auto &cmd : m_cmds)
        {
            if (cmd)
            {
                cmd->Wait();
                cmd->Return();
                cmd = nullptr;
            }
        }

        for (auto &rc : m_renderPassComponents)
            rc->Destroy();

        m_skyBoxDay.Destroy();
        m_skyBoxNight.Destroy();
        m_skyBoxWhite.Destroy();
        Image::Destroy(m_ibl_brdf_lut);

        for (auto &rt : m_renderTargets)
            Image::Destroy(rt.second);

        for (auto &dt : m_depthStencilTargets)
            Image::Destroy(dt.second);

        for (auto &semaphore : m_acquireSemaphores)
            Semaphore::Destroy(semaphore);

        for (auto &semaphore : m_submitSemaphores)
            Semaphore::Destroy(semaphore);

        Buffer::Destroy(m_screenshotBuffer);
    }

    void RendererSystem::Upsample(CommandBuffer *cmd, PeFilter filter)
    {
        ImageBlit region{};
        region.srcOffsets[1] = {static_cast<int32_t>(m_viewportRT->GetWidth()), static_cast<int32_t>(m_viewportRT->GetHeight()), 1};
        region.srcSubresource.aspectMask = PE_IMAGE_ASPECT_COLOR;
        region.srcSubresource.layerCount = 1;
        region.dstOffsets[1] = {static_cast<int32_t>(m_displayRT->GetWidth()), static_cast<int32_t>(m_displayRT->GetHeight()), 1};
        region.dstSubresource.aspectMask = PE_IMAGE_ASPECT_COLOR;
        region.dstSubresource.layerCount = 1;

        cmd->BlitImage(m_viewportRT, m_displayRT, region, filter);
    }

    Image *RendererSystem::CreateRenderTarget(const std::string &name,
                                              ::PeFormat format,
                                              PeImageUsageFlags usage,
                                              bool useRenderTergetScale,
                                              bool useMips,
                                              vec4 clearColor)
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        // DX12 currently only has same-size image blits. Keep render targets full-size
        // until the shader blit path can handle scaled viewport/display copies.
        const bool canUseRenderTargetScale = RHII.GetApi() != PE_GRAPHICS_API_DX12;
        float rtScale = useRenderTergetScale && canUseRenderTargetScale ? gSettings.render_scale : 1.f;

        uint32_t width = static_cast<uint32_t>(RHII.GetWidthf() * rtScale);
        uint32_t heigth = static_cast<uint32_t>(RHII.GetHeightf() * rtScale);

        ImageDesc desc{};
        desc.format = format;
        desc.width = width;
        desc.height = heigth;
        desc.usage = usage | PE_IMAGE_USAGE_COLOR_ATTACHMENT | PE_IMAGE_USAGE_SAMPLED | PE_IMAGE_USAGE_STORAGE | PE_IMAGE_USAGE_TRANSFER_DST;
        if (useMips)
            desc.mipLevels = static_cast<uint32_t>(std::floor(std::log2(width > heigth ? width : heigth))) + 1;
        desc.name = name;
        Image *rt = Image::Create(desc);
        rt->SetClearColor(clearColor);

        rt->CreateRTV();
        rt->CreateSRV(PE_IMAGE_VIEW_TYPE_2D);
        rt->CreateUAV(PE_IMAGE_VIEW_TYPE_2D, 0);

        SamplerDesc samplerInfo = Sampler::CreateInfoInit();
        samplerInfo.anisotropyEnable = false;
        Sampler *sampler = Sampler::Create(samplerInfo, name + "_sampler");
        rt->SetSampler(sampler);

        gSettings.rendering_images.push_back(rt);
        m_renderTargets[StringHash(name)] = rt;

        return rt;
    }

    Image *RendererSystem::CreateDepthStencilTarget(const std::string &name,
                                                    ::PeFormat format,
                                                    PeImageUsageFlags usage,
                                                    bool useRenderTergetScale,
                                                    float clearDepth,
                                                    uint32_t clearStencil)
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        // Keep DX12 color/depth attachments at one size so the current copy/blit path
        // never requires a scaling blit.
        const bool canUseRenderTargetScale = RHII.GetApi() != PE_GRAPHICS_API_DX12;
        float rtScale = useRenderTergetScale && canUseRenderTargetScale ? gSettings.render_scale : 1.f;

        ImageDesc desc{};
        desc.width = static_cast<uint32_t>(RHII.GetWidthf() * rtScale);
        desc.height = static_cast<uint32_t>(RHII.GetHeightf() * rtScale);
        desc.usage = usage | PE_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT | PE_IMAGE_USAGE_SAMPLED | PE_IMAGE_USAGE_TRANSFER_DST;
        desc.format = format;
        desc.name = name;
        Image *depth = Image::Create(desc);
        depth->SetClearColor(vec4(clearDepth, static_cast<float>(clearStencil), 0.f, 0.f));

        depth->CreateRTV();
        depth->CreateSRV(PE_IMAGE_VIEW_TYPE_2D);

        SamplerDesc samplerInfo = Sampler::CreateInfoInit();
        samplerInfo.addressModeU = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = false;
        samplerInfo.borderColor = PE_SAMPLER_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        samplerInfo.compareEnable = true;
        Sampler *sampler = Sampler::Create(samplerInfo, name + "_sampler");
        depth->SetSampler(sampler);

        gSettings.rendering_images.push_back(depth);
        m_depthStencilTargets[StringHash(name)] = depth;

        return depth;
    }

    Image *RendererSystem::GetRenderTarget(const std::string &name)
    {
        auto it = m_renderTargets.find(StringHash(name));
        if (it != m_renderTargets.end())
            return it->second;
        return nullptr;
    }

    Image *RendererSystem::GetRenderTarget(size_t hash)
    {
        auto it = m_renderTargets.find(hash);
        if (it != m_renderTargets.end())
            return it->second;
        return nullptr;
    }

    Image *RendererSystem::GetDepthStencilTarget(const std::string &name)
    {
        auto it = m_depthStencilTargets.find(StringHash(name));
        if (it != m_depthStencilTargets.end())
            return it->second;
        return nullptr;
    }

    Image *RendererSystem::GetDepthStencilTarget(size_t hash)
    {
        auto it = m_depthStencilTargets.find(hash);
        if (it != m_depthStencilTargets.end())
            return it->second;
        return nullptr;
    }

    Image *RendererSystem::CreateFSSampledImage(bool useRenderTergetScale)
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        const bool canUseRenderTargetScale = RHII.GetApi() != PE_GRAPHICS_API_DX12;
        float rtScale = useRenderTergetScale && canUseRenderTargetScale ? gSettings.render_scale : 1.f;

        ImageDesc desc{};
        desc.format = GetSwapchainSurfaceFormat();
        desc.width = static_cast<uint32_t>(RHII.GetWidthf() * rtScale);
        desc.height = static_cast<uint32_t>(RHII.GetHeightf() * rtScale);
        desc.usage = PE_IMAGE_USAGE_TRANSFER_DST | PE_IMAGE_USAGE_SAMPLED;
        desc.name = "FSSampledImage";
        Image *sampledImage = Image::Create(desc);

        sampledImage->CreateSRV(PE_IMAGE_VIEW_TYPE_2D);

        SamplerDesc samplerInfo = Sampler::CreateInfoInit();
        Sampler *sampler = Sampler::Create(samplerInfo, "FSSampledImage_sampler");
        sampledImage->SetSampler(sampler);

        return sampledImage;
    }

    void RendererSystem::CreateRenderTargets()
    {
        for (auto &framebuffer : CommandBuffer::GetFramebuffers())
            Framebuffer::Destroy(framebuffer.second);
        CommandBuffer::GetFramebuffers().clear();

        for (auto &rt : m_renderTargets)
            Image::Destroy(rt.second);
        m_renderTargets.clear();

        for (auto &rt : m_depthStencilTargets)
            Image::Destroy(rt.second);
        m_depthStencilTargets.clear();

        Settings::Get<GlobalSettings>().rendering_images.clear();

        const ::PeFormat surfaceFormat = GetSwapchainSurfaceFormat();
        m_depthStencil = CreateDepthStencilTarget("depthStencil", RHII.GetDepthFormat(), PE_IMAGE_USAGE_TRANSFER_DST);
        m_viewportRT = CreateRenderTarget("viewport", surfaceFormat, PE_IMAGE_USAGE_TRANSFER_SRC | PE_IMAGE_USAGE_TRANSFER_DST);
        m_displayRT = CreateRenderTarget("display", surfaceFormat, PE_IMAGE_USAGE_TRANSFER_SRC | PE_IMAGE_USAGE_TRANSFER_DST, false);
        m_screenshotRT = CreateRenderTarget("screenshot", surfaceFormat, PE_IMAGE_USAGE_TRANSFER_SRC | PE_IMAGE_USAGE_TRANSFER_DST, false);
        CreateRenderTarget("normal", PE_FORMAT_R16G16B16A16_SFLOAT);
        CreateRenderTarget("albedo", surfaceFormat);
        CreateRenderTarget("srm", surfaceFormat); // Specular Roughness Metallic
        CreateRenderTarget("ssao", PE_FORMAT_R8_UNORM);
        CreateRenderTarget("ssr", surfaceFormat);
        CreateRenderTarget("velocity", PE_FORMAT_R16G16_SFLOAT);
        CreateRenderTarget("emissive", surfaceFormat);
        CreateRenderTarget("brightFilter", surfaceFormat, PE_IMAGE_USAGE_NONE, false);
        CreateRenderTarget("gaussianBlurHorizontal", surfaceFormat, PE_IMAGE_USAGE_NONE, false);
        CreateRenderTarget("gaussianBlurVertical", surfaceFormat, PE_IMAGE_USAGE_NONE, false);
        CreateRenderTarget("transparency", PE_FORMAT_R8_UNORM, PE_IMAGE_USAGE_NONE, true, false, Color::Black);
    }

    void RendererSystem::Resize(uint32_t width, uint32_t height)
    {
        // Wait idle, we dont want to destroy objects in use
        RHII.WaitDeviceIdle();
        RHII.GetSurface()->SetActualExtent({0, 0, width, height});

        Swapchain *swapchain = RHII.GetSwapchain();
        Swapchain::Destroy(swapchain);

        Surface *surface = RHII.GetSurface();
        RHII.CreateSwapchain(surface);

        CreateRenderTargets();

        for (auto &rc : m_renderPassComponents)
            rc->Resize(width, height);
    }

    void RendererSystem::BlitToSwapchain(CommandBuffer *cmd, Image *src, uint32_t imageIndex)
    {
        Image *swapchainImage = RHII.GetSwapchain()->GetImage(imageIndex);

        ImageBlit region{};
        region.srcOffsets[1] = {static_cast<int32_t>(src->GetWidth()), static_cast<int32_t>(src->GetHeight()), 1};
        region.srcSubresource.aspectMask = PE_IMAGE_ASPECT_COLOR;
        region.srcSubresource.layerCount = 1;
        region.dstOffsets[1] = {static_cast<int32_t>(src->GetWidth()), static_cast<int32_t>(src->GetHeight()), 1};
        region.dstSubresource.aspectMask = PE_IMAGE_ASPECT_COLOR;
        region.dstSubresource.layerCount = 1;

        ImageBarrierInfo barrier{};
        barrier.image = swapchainImage;
        barrier.layout = PE_IMAGE_LAYOUT_PRESENT_SRC;
        barrier.stageFlags = PE_STAGE_ALL_COMMANDS;
        barrier.accessMask = PE_ACCESS_NONE;

        // with 1:1 ratio we can use nearest filter
        PeFilter filter = src->GetWidth() == swapchainImage->GetWidth() && src->GetHeight() == swapchainImage->GetHeight() ? PE_FILTER_NEAREST : PE_FILTER_LINEAR;
        cmd->BlitImage(src, swapchainImage, region, filter);
        cmd->ImageBarrier(barrier);
    }

    void RendererSystem::PollShaders(std::optional<size_t> hash)
    {
        RHII.WaitDeviceIdle();

        for (auto &rc : m_renderPassComponents)
        {
            std::shared_ptr<PassInfo> info = rc->GetPassInfo();

            bool match = false;
            if (!hash.has_value())
            {
                match = true;
            }
            else
            {
                if (info->pCompShader && info->pCompShader->GetPathID() == hash.value())
                    match = true;
                if (info->pVertShader && info->pVertShader->GetPathID() == hash.value())
                    match = true;
                if (info->pFragShader && info->pFragShader->GetPathID() == hash.value())
                    match = true;
            }

            if (match)
            {
                auto oldComp = info->pCompShader;
                auto oldVert = info->pVertShader;
                auto oldFrag = info->pFragShader;

                try
                {
                    rc->UpdatePassInfo();
                    rc->UpdateDescriptorSets();

                    Shader::Destroy(oldComp);
                    Shader::Destroy(oldVert);
                    Shader::Destroy(oldFrag);
                }
                catch (const std::exception &e)
                {
                    if (info->pCompShader != oldComp)
                        Shader::Destroy(info->pCompShader);
                    if (info->pVertShader != oldVert)
                        Shader::Destroy(info->pVertShader);
                    if (info->pFragShader != oldFrag)
                        Shader::Destroy(info->pFragShader);

                    info->pCompShader = oldComp;
                    info->pVertShader = oldVert;
                    info->pFragShader = oldFrag;
                }
            }
        }
    }
} // namespace pe
