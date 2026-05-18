#include "Render/RuntimeSceneRenderer.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Debug.h"
#include "API/Framebuffer.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/Semaphore.h"
#include "API/Shader.h"
#include "API/StagingManager.h"
#include "API/Surface.h"
#include "API/Swapchain.h"
#include "Render/RenderPassShaderReload.h"
#include "RenderPasses/AabbsPass.h"
#include "RenderPasses/BloomPass.h"
#include "RenderPasses/CullingPass.h"
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
#include "RenderPasses/UpsamplePass.h"
#include "Render/ScreenshotWriter.h"
#include "UI/RuntimeUi.h"

namespace pe
{
    namespace
    {
        bool UsesDx12RenderOrchestration()
        {
            return RHII.GetApi() == PE_GRAPHICS_API_DX12;
        }

        bool SupportsRayTracingPass()
        {
            return RHII.GetCaps().rayTracing;
        }

        size_t GetScreenshotRowPitch(uint32_t width)
        {
            const size_t rowBytes = static_cast<size_t>(width) * 4;
            return RHII.AlignTextureRowPitch(rowBytes);
        }
    } // namespace

    RuntimeSceneRenderer::RuntimeSceneRenderer(Scene &scene) : m_scene(scene)
    {
    }

    RuntimeSceneRenderer::~RuntimeSceneRenderer()
    {
        Destroy();
    }

    void RuntimeSceneRenderer::ApplyRuntimeRenderSettings()
    {
        auto &settings = Settings::Get<GlobalSettings>();

        // These are editor overlays, not standalone player output.
        settings.draw_grid = false;
        settings.draw_aabbs = false;
    }

    void RuntimeSceneRenderer::LoadResources(CommandBuffer *cmd)
    {
        m_skyBoxDay.LoadSkyBox(cmd, Path::Assets + "Skyboxes/golden_gate_hills/golden_gate_hills_4k.hdr");
        m_skyBoxNight.LoadSkyBox(cmd, Path::Assets + "Skyboxes/rogland_clear_night/rogland_clear_night_4k.hdr");

        Image::LoadRawParams loadImageParams = {256, 256, PE_FORMAT_R16G16_SFLOAT, false, true, 0.0f};
        m_ibl_brdf_lut = Image::LoadRaw(cmd, Path::Assets + "Objects/ibl_brdf_lut_rg16f_256.bin", loadImageParams);
    }

    void RuntimeSceneRenderer::Init(CommandBuffer *cmd)
    {
        if (m_initialized)
            return;

        ApplyRuntimeRenderSettings();
        m_initialized = true;
        SetActiveSceneRendererHost(this);

        try
        {
            Queue *queue = RHII.GetMainQueue();
            CommandBuffer *initCmd = cmd;
            const bool ownsInitCmd = initCmd == nullptr;
            if (ownsInitCmd)
            {
                initCmd = queue->AcquireCommandBuffer();
                initCmd->Begin();
            }

            CreateRenderTargets();
            LoadResources(initCmd);

            m_renderPassComponents[ID::GetTypeID<CullingPass>()] = CreateGlobalComponent<CullingPass>();
            m_renderPassComponents[ID::GetTypeID<ShadowPass>()] = CreateGlobalComponent<ShadowPass>();
            m_renderPassComponents[ID::GetTypeID<DepthPass>()] = CreateGlobalComponent<DepthPass>();
            m_renderPassComponents[ID::GetTypeID<GbufferOpaquePass>()] = CreateGlobalComponent<GbufferOpaquePass>();
            m_renderPassComponents[ID::GetTypeID<GbufferTransparentPass>()] =
                CreateGlobalComponent<GbufferTransparentPass>();
            m_renderPassComponents[ID::GetTypeID<SSAOPass>()] = CreateGlobalComponent<SSAOPass>();
            m_renderPassComponents[ID::GetTypeID<LightOpaquePass>()] = CreateGlobalComponent<LightOpaquePass>();
            m_renderPassComponents[ID::GetTypeID<LightTransparentPass>()] =
                CreateGlobalComponent<LightTransparentPass>();
            m_renderPassComponents[ID::GetTypeID<ParticleComputePass>()] = CreateGlobalComponent<ParticleComputePass>();
            m_renderPassComponents[ID::GetTypeID<ParticlePass>()] = CreateGlobalComponent<ParticlePass>();
            m_renderPassComponents[ID::GetTypeID<SSRPass>()] = CreateGlobalComponent<SSRPass>();
            m_renderPassComponents[ID::GetTypeID<FXAAPass>()] = CreateGlobalComponent<FXAAPass>();
            m_renderPassComponents[ID::GetTypeID<AabbsPass>()] = CreateGlobalComponent<AabbsPass>();
            m_renderPassComponents[ID::GetTypeID<TAAPass>()] = CreateGlobalComponent<TAAPass>();
            m_renderPassComponents[ID::GetTypeID<SharpenPass>()] = CreateGlobalComponent<SharpenPass>();
            m_renderPassComponents[ID::GetTypeID<UpsamplePass>()] = CreateGlobalComponent<UpsamplePass>();
            m_renderPassComponents[ID::GetTypeID<TonemapPass>()] = CreateGlobalComponent<TonemapPass>();
            m_renderPassComponents[ID::GetTypeID<BloomBrightFilterPass>()] = CreateGlobalComponent<BloomBrightFilterPass>();
            m_renderPassComponents[ID::GetTypeID<BloomGaussianBlurHorizontalPass>()] =
                CreateGlobalComponent<BloomGaussianBlurHorizontalPass>();
            m_renderPassComponents[ID::GetTypeID<BloomGaussianBlurVerticalPass>()] =
                CreateGlobalComponent<BloomGaussianBlurVerticalPass>();
            m_renderPassComponents[ID::GetTypeID<DOFPass>()] = CreateGlobalComponent<DOFPass>();
            m_renderPassComponents[ID::GetTypeID<MotionBlurPass>()] = CreateGlobalComponent<MotionBlurPass>();
            m_renderPassComponents[ID::GetTypeID<GridPass>()] = CreateGlobalComponent<GridPass>();
            if (SupportsRayTracingPass())
                m_renderPassComponents[ID::GetTypeID<RayTracingPass>()] = CreateGlobalComponent<RayTracingPass>();

            for (auto &renderPassComponent : m_renderPassComponents)
            {
                renderPassComponent->Init();
                renderPassComponent->UpdatePassInfo();
                renderPassComponent->CreateUniforms(initCmd);
            }

            const uint32_t imageCount = RHII.GetSwapchainImageCount();
            CreateFrameResources(imageCount);
            TransitionSwapchainImagesToPresent(initCmd);

            m_scene.UploadBuffers(initCmd);
            CacheGlobalComponents();
            BuildRenderGraph();
            if (ownsInitCmd)
            {
                initCmd->End();
                queue->Submit(1, &initCmd, nullptr, nullptr);
                initCmd->Wait();
                initCmd->Return();
            }
        }
        catch (...)
        {
            Destroy();
            throw;
        }
    }

    void RuntimeSceneRenderer::CacheGlobalComponents()
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
        m_upsamplePass = world->GetComponent<UpsamplePass>();
        m_tonemapPass = world->GetComponent<TonemapPass>();
        m_bloomBrightFilterPass = world->GetComponent<BloomBrightFilterPass>();
        m_bloomGaussianBlurHorizontalPass = world->GetComponent<BloomGaussianBlurHorizontalPass>();
        m_bloomGaussianBlurVerticalPass = world->GetComponent<BloomGaussianBlurVerticalPass>();
        m_dofPass = world->GetComponent<DOFPass>();
        m_motionBlurPass = world->GetComponent<MotionBlurPass>();
        m_gridPass = world->GetComponent<GridPass>();
    }

    void RuntimeSceneRenderer::Update()
    {
        ApplyRuntimeRenderSettings();

        if (m_scene.IsGeometryDirty())
            WaitAllFramesCommands();
        m_scene.FlushPendingGpuWork();

        {
            PE_PROFILE_SCOPE("Runtime Scene");
            m_scene.Update();
        }

        {
            PE_PROFILE_SCOPE("Runtime Render Graph Pass States");
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
            if (rc == m_ssaoPass)
                return isPassEnabled(RenderGraphPassId::SSAO);
            if (rc == m_lightOpaquePass)
                return isPassEnabled(RenderGraphPassId::LightOpaque);
            if (rc == m_lightTransparentPass)
                return isPassEnabled(RenderGraphPassId::LightTransparent);
            if (rc == m_rayTracingPass)
                return isPassEnabled(RenderGraphPassId::RayTracing);
            if (rc == m_particleComputePass || rc == m_particlePass)
                return isPassEnabled(RenderGraphPassId::ParticleCompute) || isPassEnabled(RenderGraphPassId::Particle);
            if (rc == m_ssrPass)
                return isPassEnabled(RenderGraphPassId::SSR);
            if (rc == m_fxaaPass)
                return isPassEnabled(RenderGraphPassId::FXAA);
            if (rc == m_aabbsPass)
                return isPassEnabled(RenderGraphPassId::Aabbs);
            if (rc == m_taaPass)
                return isPassEnabled(RenderGraphPassId::TAA);
            if (rc == m_sharpenPass)
                return isPassEnabled(RenderGraphPassId::Sharpen);
            if (rc == m_upsamplePass)
                return isPassEnabled(RenderGraphPassId::Upsample);
            if (rc == m_tonemapPass)
                return isPassEnabled(RenderGraphPassId::Tonemap);
            if (rc == m_bloomBrightFilterPass || rc == m_bloomGaussianBlurHorizontalPass || rc == m_bloomGaussianBlurVerticalPass)
                return isPassEnabled(RenderGraphPassId::BloomBF) ||
                       isPassEnabled(RenderGraphPassId::BloomH) ||
                       isPassEnabled(RenderGraphPassId::BloomV);
            if (rc == m_dofPass)
                return isPassEnabled(RenderGraphPassId::DOF);
            if (rc == m_motionBlurPass)
                return isPassEnabled(RenderGraphPassId::MotionBlur);
            if (rc == m_gridPass)
                return isPassEnabled(RenderGraphPassId::Grid);

            return true;
        };

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

    void RuntimeSceneRenderer::UpdateRenderGraphPassStates()
    {
        const auto &gs = Settings::Get<GlobalSettings>();

        const bool hasRTGeom = SupportsRayTracingPass() && m_scene.GetTLAS() != nullptr;
        const bool renderRaster = (gs.render_mode != RenderMode::RayTracing) || !hasRTGeom;
        const bool renderRayTracing = (gs.render_mode != RenderMode::Raster) && hasRTGeom;
        const bool needVelocity = gs.taa || gs.motion_blur;
        const bool renderSSR = gs.ssr && renderRaster;
        const bool renderSSAO = gs.ssao && renderRaster;
        const bool needDepth = renderRaster || needVelocity || gs.dof || gs.motion_blur || gs.draw_aabbs || gs.draw_grid;
        const bool needGBuffer = renderRaster || needVelocity || renderSSR || renderSSAO;

        m_renderGraphPassEnabled.fill(false);

        if (UsesDx12RenderOrchestration())
        {
            const bool dx12RayTracing = renderRayTracing;
            const bool dx12RenderRaster = renderRaster || !dx12RayTracing;
            const bool dx12RtOnly = dx12RayTracing && !dx12RenderRaster;
            const bool dx12RenderTAA = gs.taa && (dx12RenderRaster || dx12RtOnly);
            const bool dx12NeedVelocity = dx12RenderTAA || (gs.motion_blur && dx12RenderRaster);
            const bool dx12NeedDepth = dx12RenderRaster || dx12NeedVelocity || gs.draw_aabbs || gs.draw_grid;
            const bool dx12NeedGBuffer = dx12RenderRaster || dx12NeedVelocity;

            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::Culling)] = true;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::Shadow)] = gs.shadows && dx12RenderRaster;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::Depth)] = dx12NeedDepth;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::GBufferOpaque)] = dx12NeedGBuffer;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::SSAO)] = gs.ssao && dx12RenderRaster;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::LightOpaque)] = dx12RenderRaster;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::GBufferTransparent)] = gs.render_mode == RenderMode::Raster;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::LightTransparent)] = dx12RenderRaster;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::RayTracing)] = dx12RayTracing;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::ParticleCompute)] = dx12RenderRaster;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::Particle)] = dx12RenderRaster;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::SSR)] = gs.ssr && dx12RenderRaster;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::FXAA)] = gs.fxaa && dx12RenderRaster;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::Aabbs)] = gs.draw_aabbs;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::TAA)] = dx12RenderTAA;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::Sharpen)] = dx12RenderTAA && gs.cas_sharpening;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::Upsample)] = (!gs.taa && dx12RenderRaster) || (dx12RtOnly && !dx12RenderTAA);
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::Tonemap)] = gs.tonemapping && dx12RenderRaster;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::BloomBF)] = gs.bloom && dx12RenderRaster;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::BloomH)] = gs.bloom && dx12RenderRaster;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::BloomV)] = gs.bloom && dx12RenderRaster;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::DOF)] = gs.dof && dx12RenderRaster;
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::MotionBlur)] = gs.motion_blur && dx12RenderRaster;
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
    }

    void RuntimeSceneRenderer::WaitPreviousFrameCommands()
    {
        if (!m_initialized)
            return;

        const uint32_t frame = RHII.GetFrameIndex();
        auto &frameCmd = m_cmds[frame];
        if (frameCmd)
        {
            PE_PROFILE_SCOPE("Runtime Cmd Wait");
            frameCmd->Wait();
            frameCmd->Return();
            frameCmd = nullptr;
        }

        RHII.FlushDeletionQueue(frame);
        RHII.GetStagingManager()->RemoveUnused();
    }

    void RuntimeSceneRenderer::WaitAllFramesCommands()
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

    void RuntimeSceneRenderer::RequestScreenshot(std::string path)
    {
        m_screenshotPath = std::move(path);
        m_screenshotRequested = true;
    }

    void RuntimeSceneRenderer::PollShaders(std::optional<size_t> hash)
    {
        RHII.WaitDeviceIdle();

        for (auto &rc : m_renderPassComponents)
            ReloadRenderPassShaders(*rc, hash);
    }

    void RuntimeSceneRenderer::BuildRenderGraph()
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
        addPass(RenderGraphPassId::Upsample, 1500, "Upsample", m_upsamplePass);
        addPass(RenderGraphPassId::Tonemap, 1600, "Tonemap", m_tonemapPass);
        addPass(RenderGraphPassId::BloomBF, 1700, "BloomBF", m_bloomBrightFilterPass);
        addPass(RenderGraphPassId::BloomH, 1800, "BloomH", m_bloomGaussianBlurHorizontalPass);
        addPass(RenderGraphPassId::BloomV, 1900, "BloomV", m_bloomGaussianBlurVerticalPass);
        addPass(RenderGraphPassId::DOF, 2000, "DOF", m_dofPass);
        addPass(RenderGraphPassId::MotionBlur, 2100, "MotionBlur", m_motionBlurPass);
        addPass(RenderGraphPassId::Grid, 2200, "Grid", m_gridPass);
        addPass(RenderGraphPassId::Particle, 2300, "Particle", m_particlePass);
        m_renderGraph.Compile();
    }

    CommandBuffer *RuntimeSceneRenderer::RecordPasses(uint32_t imageIndex)
    {
        CommandBuffer *cmd = RHII.GetMainQueue()->AcquireCommandBuffer();

        {
            PE_PROFILE_SCOPE("Runtime Record Set Pass Scenes");
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
        }

        cmd->Begin();
        m_renderGraph.Execute(cmd);
        if (m_runtimeUi)
            m_runtimeUi->Render(cmd, m_displayRT);
        BlitToSwapchain(cmd, m_displayRT, imageIndex);
        QueueScreenshotReadback(cmd, m_displayRT);
        Debug::CollectGpuTrace(cmd);
        cmd->End();

        return cmd;
    }

    void RuntimeSceneRenderer::Draw()
    {
        if (!m_initialized)
            return;

        try
        {
            const uint32_t frame = RHII.GetFrameIndex();

            Semaphore *acquireSemaphore = m_acquireSemaphores[frame];
            Swapchain *swapchain = RHII.GetSwapchain();
            uint32_t imageIndex = 0;
            {
                PE_PROFILE_SCOPE("Runtime Acquire Image");
                imageIndex = swapchain->AquireNextImage(acquireSemaphore);
            }

            auto &frameCmd = m_cmds[frame];
            {
                PE_PROFILE_SCOPE("Runtime Record Passes");
                frameCmd = RecordPasses(imageIndex);
            }

            Semaphore *submitSemaphore = m_submitSemaphores[imageIndex];
            Queue *queue = RHII.GetMainQueue();
            {
                PE_PROFILE_SCOPE("Runtime Queue Submit");
                queue->Submit(1, &frameCmd, acquireSemaphore, submitSemaphore);
            }

            {
                PE_PROFILE_SCOPE("Runtime Present");
                queue->Present(swapchain, imageIndex, submitSemaphore);
            }

            if (m_screenshotPending)
            {
                frameCmd->Wait();
                SaveScreenshot();
                m_screenshotPending = false;
            }
        }
        catch (const SwapchainOutOfDateError &)
        {
        }
    }

    void RuntimeSceneRenderer::Destroy()
    {
        if (!m_initialized)
            return;

        RHII.WaitDeviceIdle();
        DestroyFrameResources();

        for (auto &rc : m_renderPassComponents)
            rc->Destroy();

        m_skyBoxDay.Destroy();
        m_skyBoxNight.Destroy();
        Image::Destroy(m_ibl_brdf_lut);
        Buffer::Destroy(m_screenshotBuffer);

        for (auto &rt : m_renderTargets)
            Image::Destroy(rt.second);
        m_renderTargets.clear();

        for (auto &dt : m_depthStencilTargets)
            Image::Destroy(dt.second);
        m_depthStencilTargets.clear();

        if (GetActiveSceneRendererHost() == this)
            SetActiveSceneRendererHost(nullptr);

        m_initialized = false;
    }

    Image *RuntimeSceneRenderer::CreateRenderTarget(const std::string &name,
                                                    ::PeFormat format,
                                                    PeImageUsageFlags usage,
                                                    bool useRenderTergetScale,
                                                    bool useMips,
                                                    vec4 clearColor)
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        const float rtScale = useRenderTergetScale ? gSettings.render_scale : 1.f;

        const uint32_t width = static_cast<uint32_t>(RHII.GetWidthf() * rtScale);
        const uint32_t height = static_cast<uint32_t>(RHII.GetHeightf() * rtScale);

        ImageDesc desc{};
        desc.format = format;
        desc.width = width;
        desc.height = height;
        desc.usage = usage | PE_IMAGE_USAGE_COLOR_ATTACHMENT | PE_IMAGE_USAGE_SAMPLED | PE_IMAGE_USAGE_STORAGE | PE_IMAGE_USAGE_TRANSFER_DST;
        if (useMips)
            desc.mipLevels = static_cast<uint32_t>(std::floor(std::log2(width > height ? width : height))) + 1;
        desc.clearColor = clearColor;
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

    Image *RuntimeSceneRenderer::CreateDepthStencilTarget(const std::string &name,
                                                          ::PeFormat format,
                                                          PeImageUsageFlags usage,
                                                          bool useRenderTergetScale,
                                                          float clearDepth,
                                                          uint32_t clearStencil)
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        const float rtScale = useRenderTergetScale ? gSettings.render_scale : 1.f;

        ImageDesc desc{};
        desc.width = static_cast<uint32_t>(RHII.GetWidthf() * rtScale);
        desc.height = static_cast<uint32_t>(RHII.GetHeightf() * rtScale);
        desc.usage = usage | PE_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT | PE_IMAGE_USAGE_SAMPLED | PE_IMAGE_USAGE_TRANSFER_DST;
        desc.format = format;
        desc.clearColor = vec4(clearDepth, static_cast<float>(clearStencil), 0.f, 0.f);
        desc.name = name;
        Image *depth = Image::Create(desc);
        depth->SetClearColor(desc.clearColor);

        depth->CreateRTV();
        depth->CreateSRV(PE_IMAGE_VIEW_TYPE_2D);

        SamplerDesc samplerInfo = Sampler::CreateInfoInit();
        samplerInfo.addressModeU = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = false;
        samplerInfo.borderColor = PE_SAMPLER_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        Sampler *sampler = Sampler::Create(samplerInfo, name + "_sampler");
        depth->SetSampler(sampler);

        gSettings.rendering_images.push_back(depth);
        m_depthStencilTargets[StringHash(name)] = depth;

        return depth;
    }

    Image *RuntimeSceneRenderer::GetRenderTarget(const std::string &name)
    {
        auto it = m_renderTargets.find(StringHash(name));
        return it != m_renderTargets.end() ? it->second : nullptr;
    }

    Image *RuntimeSceneRenderer::GetRenderTarget(size_t hash)
    {
        auto it = m_renderTargets.find(hash);
        return it != m_renderTargets.end() ? it->second : nullptr;
    }

    Image *RuntimeSceneRenderer::GetDepthStencilTarget(const std::string &name)
    {
        auto it = m_depthStencilTargets.find(StringHash(name));
        return it != m_depthStencilTargets.end() ? it->second : nullptr;
    }

    Image *RuntimeSceneRenderer::GetDepthStencilTarget(size_t hash)
    {
        auto it = m_depthStencilTargets.find(hash);
        return it != m_depthStencilTargets.end() ? it->second : nullptr;
    }

    Image *RuntimeSceneRenderer::CreateFSSampledImage(bool useRenderTergetScale)
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        const float rtScale = useRenderTergetScale ? gSettings.render_scale : 1.f;

        ImageDesc desc{};
        desc.format = RHII.GetSwapchainFormat();
        desc.width = static_cast<uint32_t>(RHII.GetWidthf() * rtScale);
        desc.height = static_cast<uint32_t>(RHII.GetHeightf() * rtScale);
        desc.usage = PE_IMAGE_USAGE_TRANSFER_DST | PE_IMAGE_USAGE_SAMPLED;
        desc.name = "RuntimeFSSampledImage";
        Image *sampledImage = Image::Create(desc);

        sampledImage->CreateSRV(PE_IMAGE_VIEW_TYPE_2D);

        SamplerDesc samplerInfo = Sampler::CreateInfoInit();
        Sampler *sampler = Sampler::Create(samplerInfo, "RuntimeFSSampledImage_sampler");
        sampledImage->SetSampler(sampler);

        return sampledImage;
    }

    void RuntimeSceneRenderer::CreateRenderTargets()
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

        const ::PeFormat surfaceFormat = RHII.GetSwapchainFormat();
        m_depthStencil = CreateDepthStencilTarget("depthStencil", RHII.GetDepthFormat(), PE_IMAGE_USAGE_TRANSFER_DST);
        m_viewportRT = CreateRenderTarget("viewport", surfaceFormat, PE_IMAGE_USAGE_TRANSFER_SRC | PE_IMAGE_USAGE_TRANSFER_DST);
        m_displayRT = CreateRenderTarget("display", surfaceFormat, PE_IMAGE_USAGE_TRANSFER_SRC | PE_IMAGE_USAGE_TRANSFER_DST, false);
        m_screenshotRT = CreateRenderTarget("screenshot", surfaceFormat, PE_IMAGE_USAGE_TRANSFER_SRC | PE_IMAGE_USAGE_TRANSFER_DST, false);
        CreateRenderTarget("normal", PE_FORMAT_R16G16B16A16_SFLOAT);
        CreateRenderTarget("albedo", surfaceFormat);
        CreateRenderTarget("srm", surfaceFormat);
        CreateRenderTarget("ssao", PE_FORMAT_R8_UNORM);
        CreateRenderTarget("ssr", surfaceFormat);
        CreateRenderTarget("velocity", PE_FORMAT_R16G16_SFLOAT);
        CreateRenderTarget("emissive", surfaceFormat);
        CreateRenderTarget("brightFilter", surfaceFormat, PE_IMAGE_USAGE_NONE, false);
        CreateRenderTarget("gaussianBlurHorizontal", surfaceFormat, PE_IMAGE_USAGE_NONE, false);
        CreateRenderTarget("gaussianBlurVertical", surfaceFormat, PE_IMAGE_USAGE_NONE, false);
        CreateRenderTarget("transparency", PE_FORMAT_R8_UNORM, PE_IMAGE_USAGE_NONE, true, false, Color::Black);
    }

    void RuntimeSceneRenderer::CreateFrameResources(uint32_t imageCount)
    {
        const bool isDx12 = UsesDx12RenderOrchestration();

        m_cmds.assign(imageCount, nullptr);
        m_acquireSemaphores.reserve(imageCount);
        m_submitSemaphores.reserve(imageCount);
        for (uint32_t i = 0; i < imageCount; i++)
        {
            Semaphore *acquireSemaphore = Semaphore::Create(false, "RuntimeAcquireSemaphore_" + std::to_string(i));
            if (!isDx12)
                acquireSemaphore->SetStageFlags(PE_STAGE_ALL_COMMANDS);
            m_acquireSemaphores.push_back(acquireSemaphore);

            Semaphore *submitSemaphore = Semaphore::Create(false, "RuntimeSubmitSemaphore_" + std::to_string(i));
            if (!isDx12)
                submitSemaphore->SetStageFlags(PE_STAGE_ALL_COMMANDS);
            m_submitSemaphores.push_back(submitSemaphore);
        }
    }

    void RuntimeSceneRenderer::DestroyFrameResources()
    {
        for (auto &cmd : m_cmds)
        {
            if (cmd)
            {
                cmd->Wait();
                cmd->Return();
                cmd = nullptr;
            }
        }
        m_cmds.clear();

        for (auto &semaphore : m_acquireSemaphores)
            Semaphore::Destroy(semaphore);
        m_acquireSemaphores.clear();

        for (auto &semaphore : m_submitSemaphores)
            Semaphore::Destroy(semaphore);
        m_submitSemaphores.clear();
    }

    void RuntimeSceneRenderer::TransitionSwapchainImagesToPresent(CommandBuffer *cmd)
    {
        if (!cmd || UsesDx12RenderOrchestration())
            return;

        const uint32_t imageCount = RHII.GetSwapchainImageCount();
        for (uint32_t i = 0; i < imageCount; i++)
        {
            ImageBarrierInfo barrierInfo{};
            barrierInfo.image = RHII.GetSwapchain()->GetImage(i);
            barrierInfo.layout = PE_IMAGE_LAYOUT_PRESENT_SRC;
            barrierInfo.stageFlags = PE_STAGE_ALL_COMMANDS;
            barrierInfo.accessMask = PE_ACCESS_NONE;
            cmd->ImageBarrier(barrierInfo);
        }
    }

    void RuntimeSceneRenderer::Resize(uint32_t width, uint32_t height)
    {
        if (!m_initialized)
            return;

        RHII.WaitDeviceIdle();
        DestroyFrameResources();
        RHII.GetSurface()->SetActualExtent({0, 0, width, height});

        Swapchain *swapchain = RHII.GetSwapchain();
        Swapchain::Destroy(swapchain);

        Surface *surface = RHII.GetSurface();
        RHII.CreateSwapchain(surface);

        CreateRenderTargets();
        CreateFrameResources(RHII.GetSwapchainImageCount());

        Queue *queue = RHII.GetMainQueue();
        CommandBuffer *resizeCmd = queue->AcquireCommandBuffer();
        resizeCmd->Begin();
        TransitionSwapchainImagesToPresent(resizeCmd);
        resizeCmd->End();
        queue->Submit(1, &resizeCmd, nullptr, nullptr);
        resizeCmd->Wait();
        resizeCmd->Return();

        for (auto &rc : m_renderPassComponents)
            rc->Resize(width, height);

        BuildRenderGraph();
    }

    void RuntimeSceneRenderer::BlitToSwapchain(CommandBuffer *cmd, Image *src, uint32_t imageIndex)
    {
        Image *swapchainImage = RHII.GetSwapchain()->GetImage(imageIndex);

        ImageBlit region{};
        region.srcOffsets[1] = {static_cast<int32_t>(src->GetWidth()), static_cast<int32_t>(src->GetHeight()), 1};
        region.srcSubresource.aspectMask = PE_IMAGE_ASPECT_COLOR;
        region.srcSubresource.layerCount = 1;
        region.dstOffsets[1] = {static_cast<int32_t>(swapchainImage->GetWidth()), static_cast<int32_t>(swapchainImage->GetHeight()), 1};
        region.dstSubresource.aspectMask = PE_IMAGE_ASPECT_COLOR;
        region.dstSubresource.layerCount = 1;

        ImageBarrierInfo barrier{};
        barrier.image = swapchainImage;
        barrier.layout = PE_IMAGE_LAYOUT_PRESENT_SRC;
        barrier.stageFlags = PE_STAGE_NONE;
        barrier.accessMask = PE_ACCESS_NONE;

        const PeFilter filter = src->GetWidth() == swapchainImage->GetWidth() && src->GetHeight() == swapchainImage->GetHeight()
                                    ? PE_FILTER_NEAREST
                                    : PE_FILTER_LINEAR;
        cmd->BlitImage(src, swapchainImage, region, filter);
        cmd->ImageBarrier(barrier);
    }

    void RuntimeSceneRenderer::QueueScreenshotReadback(CommandBuffer *cmd, Image *sourceImage)
    {
        if (!m_screenshotRequested || !sourceImage || !m_screenshotRT)
            return;

        m_screenshotRequested = false;
        cmd->CopyImage(sourceImage, m_screenshotRT);

        const uint32_t width = m_screenshotRT->GetWidth();
        const uint32_t height = m_screenshotRT->GetHeight();
        m_screenshotRowPitch = GetScreenshotRowPitch(width);
        const size_t bufferSize = m_screenshotRowPitch * height;

        Buffer::Destroy(m_screenshotBuffer);
        m_screenshotBuffer = Buffer::Create({
            .size = bufferSize,
            .usage = PE_BUFFER_USAGE_TRANSFER_DST,
            .memoryUsage = PE_MEMORY_USAGE_GPU_TO_CPU,
            .name = "RuntimeScreenshotStaging",
        });

        cmd->CopyImageToBuffer(m_screenshotRT, m_screenshotBuffer);
        m_screenshotPending = true;
    }

    void RuntimeSceneRenderer::SaveScreenshot()
    {
        if (!m_screenshotBuffer || !m_screenshotRT)
            return;

        m_screenshotBuffer->Map();
        const uint8_t *pixels = static_cast<const uint8_t *>(m_screenshotBuffer->Data());

        ScreenshotWriteDesc screenshot{};
        screenshot.path = m_screenshotPath;
        screenshot.pixels = pixels;
        screenshot.width = m_screenshotRT->GetWidth();
        screenshot.height = m_screenshotRT->GetHeight();
        screenshot.rowPitch = m_screenshotRowPitch;
        screenshot.format = m_screenshotRT->GetFormat();

        std::string savedPath;
        if (WriteScreenshotPng(screenshot, &savedPath))
            PE_INFO("Screenshot saved: %s", savedPath.c_str());
        else
            PE_ERROR("[Renderer] Failed to save screenshot: %s", m_screenshotPath.c_str());

        m_screenshotBuffer->Unmap();
        Buffer::Destroy(m_screenshotBuffer);
        m_screenshotRowPitch = 0;
    }
} // namespace pe
