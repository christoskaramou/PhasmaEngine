#include "RendererSystem.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/Surface.h"
#include "API/Swapchain.h"
#include "RenderPasses/TAAPass.h"
#include "Render/ScriptRenderPasses.h"
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

        bool HasRuntimeUiPass()
        {
            RuntimeUiSystem *runtimeUi = GetActiveRuntimeUi();
            return runtimeUi && runtimeUi->IsInitialized();
        }

        void DispatchWindowTitle()
        {
            const Swapchain *swapchain = RHII.GetSwapchain();
            const Surface *surface = RHII.GetSurface();
            const PePresentMode presentMode = swapchain ? swapchain->GetPresentMode()
                                              : surface ? surface->GetPresentMode()
                                                        : PE_PRESENT_MODE_FIFO;

            std::string title = "PhasmaEngine";
            title += " - Device: " + RHII.GetGpuName();
            title += " - API: " + std::string(PeGraphicsApiName(RHII.GetApi()));
            title += " - Present Mode: " + std::string(RHII.PresentModeToString(presentMode));
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
        }
    } // namespace

    void RendererSystem::Init(CommandBuffer *cmd)
    {
        SetActiveSceneRendererHost(this);

        const bool isDx12 = UsesDx12RenderOrchestration();

        DispatchWindowTitle();

        Queue *queue = RHII.GetMainQueue();
        CommandBuffer *initCmd = cmd;
        const bool ownsInitCmd = isDx12 && initCmd == nullptr;
        if (ownsInitCmd)
        {
            initCmd = queue->AcquireCommandBuffer();
            initCmd->Begin();
        }

        m_sceneRenderer.CreateRenderTargets();

        // Skybox / IBL are consumed by the Light pass. DX12 reaches that slice in 14c.
        m_sceneRenderer.LoadSky(initCmd);

        m_sceneRenderer.CreateRenderPassComponents(SupportsRayTracingPass(), initCmd);

        // Init GUI
        m_gui.Init();

        uint32_t imageCount = RHII.GetSwapchainImageCount();
        const PeBarrierSync acquireStageFlags = isDx12 ? PE_STAGE_NONE
                                                       : PE_STAGE_COLOR_ATTACHMENT_OUTPUT | PE_STAGE_COMPUTE_SHADER |
                                                             PE_STAGE_RAY_TRACING_SHADER_KHR | PE_STAGE_TRANSFER;
        const PeBarrierSync submitStageFlags = isDx12 ? PE_STAGE_NONE : PE_STAGE_ALL_COMMANDS;
        m_sceneRenderer.CreateFrameResources(imageCount,
                                             "AcquireSemaphore_",
                                             "SubmitSemaphore_",
                                             acquireStageFlags,
                                             submitStageFlags);

        m_scene.UploadBuffers(initCmd);
        m_sceneRenderer.CacheGlobalComponents();
        m_sceneRenderer.UpdateRenderGraphPassStates(SupportsRayTracingPass() && m_scene.GetTLAS() != nullptr, initCmd);
        BuildRenderGraph();

        if (ownsInitCmd)
        {
            initCmd->End();
            queue->Submit(1, &initCmd, nullptr, nullptr);
            initCmd->Wait();
            initCmd->Return();
        }
    }

    void RendererSystem::Update()
    {
        // Scripts added/removed render passes (render_graph.add_pass): rebuild the
        // graph before this frame records. Pure CPU pass-list rebuild, no GPU wait.
        if (m_scriptRenderPassesRevision != GetScriptRenderPassesRevision())
        {
            m_scriptRenderPassesRevision = GetScriptRenderPassesRevision();
            BuildRenderGraph();
        }

        {
            PE_PROFILE_SCOPE("GUI");
            m_gui.Update();
        }
        ApplyPendingRenderScaleResize();

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
        ApplyPendingOptionalRTSync();

        {
            PE_PROFILE_SCOPE("Render Graph Pass States");
            UpdateRenderGraphPassStates();
        }

        // Render Components
        {
            PE_PROFILE_SCOPE("Render Pass Updates");
            m_sceneRenderer.UpdateRenderPassComponents();
        }
    }

    void RendererSystem::LateCatchUpForScriptMutations()
    {
        const bool hasPendingRenderUpdate = m_scene.HasPendingRenderUpdate();
        const bool hasDirtyCameras = m_scene.HasDirtyCameras();
        if (!hasPendingRenderUpdate && !hasDirtyCameras)
            return;

        if (hasPendingRenderUpdate)
        {
            if (m_scene.IsGeometryDirty())
                WaitAllFramesCommands();
            m_scene.FlushPendingGpuWork();

            {
                PE_PROFILE_SCOPE("Scene Late");
                m_scene.Update();
            }
        }
        else
        {
            m_scene.UpdateCameraRenderState();
        }
        ApplyPendingOptionalRTSync();

        {
            PE_PROFILE_SCOPE("Render Graph Pass States Late");
            UpdateRenderGraphPassStates();
        }

        {
            PE_PROFILE_SCOPE("Render Pass Updates Late");
            m_sceneRenderer.UpdateRenderPassComponents();
        }
    }

    void RendererSystem::UpdateRenderGraphPassStates()
    {
        const bool hasRTGeom = SupportsRayTracingPass() && m_scene.GetTLAS() != nullptr;
        m_sceneRenderer.UpdateRenderGraphPassStates(hasRTGeom);
        m_guiPassEnabled = m_gui.Render() || HasRuntimeUiPass();
    }

    void RendererSystem::WaitPreviousFrameCommands()
    {
        m_sceneRenderer.WaitPreviousFrameCommands();
    }

    void RendererSystem::ResetTAAHistory()
    {
        if (auto *taaPass = static_cast<TAAPass *>(m_sceneRenderer.GetSceneRenderGraphPassComponents().taa))
            taaPass->RequestHistoryReset();
    }

    void RendererSystem::WaitAllFramesCommands()
    {
        m_sceneRenderer.WaitAllFrameCommands();
    }

    void RendererSystem::BuildRenderGraph()
    {
        m_scriptRenderPassesRevision = GetScriptRenderPassesRevision();
        RenderGraph &renderGraph = m_sceneRenderer.GetRenderGraph();
        renderGraph.Clear();

        UpdateRenderGraphPassStates();

        auto isGuiPassEnabled = [this]()
        {
            return m_guiPassEnabled;
        };
        m_sceneRenderer.AddScenePassesToRenderGraph();
        renderGraph.AddPass(static_cast<RenderGraph::PassID>(RenderGraphPassId::GUI),
                            10000,
                            "GUI",
                            isGuiPassEnabled,
                            [this](CommandBuffer *cmd)
                            { m_gui.ExecutePass(cmd); });
        renderGraph.Compile();
    }

    CommandBuffer *RendererSystem::RecordPasses(uint32_t imageIndex)
    {
        CommandBuffer *cmd = RHII.GetMainQueue()->AcquireCommandBuffer();
        const bool isDx12 = UsesDx12RenderOrchestration();
        Image *dx12SwapchainImage = isDx12 ? RHII.GetSwapchain()->GetImage(imageIndex) : nullptr;

        // Set scene on all scene-dependent passes before execution.
        {
            PE_PROFILE_SCOPE("Record Set Pass Scenes");
            m_sceneRenderer.SetRenderPassScene(m_scene);
        }

        {
            PE_PROFILE_SCOPE("Record Cmd Begin");
            cmd->Begin();
        }
        m_scene.RecordPendingUvUploads(cmd);
        {
            PE_PROFILE_SCOPE("Render Graph Execute");
            m_sceneRenderer.ExecuteRenderGraph(cmd);
        }

        if (isDx12)
        {
            Image *frameOutputImage = GetFrameOutputImage();

            if (frameOutputImage)
            {
                {
                    PE_PROFILE_SCOPE("DX12 FrameOutput Blit");
                    m_sceneRenderer.BlitToSwapchain(cmd, frameOutputImage, imageIndex);
                }
                {
                    PE_PROFILE_SCOPE("DX12 Screenshot Readback");
                    QueueScreenshotReadback(cmd, frameOutputImage);
                }
            }
            else
            {
                Attachment attachment{};
                attachment.image = dx12SwapchainImage;
                attachment.loadOp = PE_LOAD_OP_CLEAR;
                attachment.storeOp = PE_STORE_OP_STORE;
                cmd->BeginPass(1, &attachment, "DX12FinalClear");
                cmd->EndPass();

                {
                    PE_PROFILE_SCOPE("DX12 Screenshot Readback");
                    QueueScreenshotReadback(cmd, dx12SwapchainImage);
                }

                ImageBarrierInfo presentBarrier{};
                presentBarrier.image = dx12SwapchainImage;
                presentBarrier.layout = PE_IMAGE_LAYOUT_PRESENT_SRC;
                presentBarrier.stageFlags = PE_STAGE_ALL_COMMANDS;
                presentBarrier.accessMask = PE_ACCESS_NONE;
                {
                    PE_PROFILE_SCOPE("DX12 Present Barrier");
                    cmd->ImageBarrier(presentBarrier);
                }
            }
        }
        else
        {
            {
                PE_PROFILE_SCOPE("Blit To Swapchain");
                m_sceneRenderer.BlitToSwapchain(cmd, m_sceneRenderer.GetDisplayRT(), imageIndex);
            }

            {
                PE_PROFILE_SCOPE("Vulkan Screenshot Readback");
                QueueScreenshotReadback(cmd, m_sceneRenderer.GetDisplayRT());
            }
        }

        {
            PE_PROFILE_SCOPE("Collect GPU Trace");
            Debug::CollectGpuTrace(cmd);
        }

        {
            PE_PROFILE_SCOPE("Record Cmd End");
            cmd->End();
        }

        return cmd;
    }

    Image *RendererSystem::GetFrameOutputImage() const
    {
        const bool displayProduced =
            m_sceneRenderer.IsPassEnabled(SceneRenderGraphPassId::Upsample) ||
            m_sceneRenderer.IsPassEnabled(SceneRenderGraphPassId::TAA) ||
            m_sceneRenderer.IsPassEnabled(SceneRenderGraphPassId::Sharpen) ||
            m_sceneRenderer.IsPassEnabled(SceneRenderGraphPassId::Tonemap) ||
            m_sceneRenderer.IsPassEnabled(SceneRenderGraphPassId::BloomV) ||
            m_sceneRenderer.IsPassEnabled(SceneRenderGraphPassId::DOF) ||
            m_sceneRenderer.IsPassEnabled(SceneRenderGraphPassId::MotionBlur) ||
            m_sceneRenderer.IsPassEnabled(SceneRenderGraphPassId::Grid) || m_guiPassEnabled;
        if (displayProduced)
            return m_sceneRenderer.GetDisplayRT();

        const bool viewportProduced =
            m_sceneRenderer.IsPassEnabled(SceneRenderGraphPassId::LightOpaque) ||
            m_sceneRenderer.IsPassEnabled(SceneRenderGraphPassId::LightTransparent) ||
            m_sceneRenderer.IsPassEnabled(SceneRenderGraphPassId::RayTracing);
        if (viewportProduced)
            return m_sceneRenderer.GetViewportRT();

        return nullptr;
    }

    void RendererSystem::QueueScreenshotReadback(CommandBuffer *cmd, Image *sourceImage)
    {
        EventSystem::QueuedEvent screenshotEvt;
        if (!sourceImage || !EventSystem::PeekAndPop(EventType::Screenshot, screenshotEvt))
            return;

        m_sceneRenderer.SetScreenshotPath(screenshotEvt.payload.has_value()
                                              ? std::any_cast<std::string>(screenshotEvt.payload)
                                              : std::string());
        m_sceneRenderer.QueueScreenshotReadback(cmd, sourceImage, "ScreenshotStaging");
    }

    void RendererSystem::Draw()
    {
        m_sceneRenderer.SubmitAndPresent([this](uint32_t imageIndex)
                                         { return RecordPasses(imageIndex); },
                                         [this]()
                                         {
                                             std::string savedPath;
                                             if (m_sceneRenderer.SaveScreenshot(&savedPath))
                                                 m_screenshotSavedPath = savedPath;
                                         });
    }

    void RendererSystem::DrawPlatformWindows()
    {
        m_gui.DrawPlatformWindows();
    }

    void RendererSystem::Destroy()
    {
        RHII.WaitDeviceIdle();

        m_sceneRenderer.DestroyFrameResources();

        m_sceneRenderer.DestroyRenderPassComponents();

        m_sceneRenderer.DestroySky();
        m_skyBoxWhite.Destroy();

        m_sceneRenderer.DestroyRenderTargets();

        m_sceneRenderer.DestroyScreenshotBuffer();

        if (GetActiveSceneRendererHost() == this)
            SetActiveSceneRendererHost(nullptr);
    }

    Image *RendererSystem::CreateRenderTarget(const std::string &name,
                                              ::PeFormat format,
                                              PeImageUsageFlags usage,
                                              bool useRenderTergetScale,
                                              bool useMips,
                                              vec4 clearColor)
    {
        return m_sceneRenderer.CreateRenderTarget(name, format, usage, useRenderTergetScale, useMips, clearColor);
    }

    Image *RendererSystem::CreateDepthStencilTarget(const std::string &name,
                                                    ::PeFormat format,
                                                    PeImageUsageFlags usage,
                                                    bool useRenderTergetScale,
                                                    float clearDepth,
                                                    uint32_t clearStencil)
    {
        return m_sceneRenderer.CreateDepthStencilTarget(name, format, usage, useRenderTergetScale, clearDepth, clearStencil);
    }

    Image *RendererSystem::GetRenderTarget(const std::string &name)
    {
        return m_sceneRenderer.GetRenderTarget(name);
    }

    Image *RendererSystem::GetRenderTarget(size_t hash)
    {
        return m_sceneRenderer.GetRenderTarget(hash);
    }

    bool RendererSystem::DestroyRenderTarget(const std::string &name)
    {
        return m_sceneRenderer.DestroyRenderTarget(name);
    }

    Image *RendererSystem::GetDepthStencilTarget(const std::string &name)
    {
        return m_sceneRenderer.GetDepthStencilTarget(name);
    }

    Image *RendererSystem::GetDepthStencilTarget(size_t hash)
    {
        return m_sceneRenderer.GetDepthStencilTarget(hash);
    }

    Image *RendererSystem::CreateFSSampledImage(bool useRenderTergetScale)
    {
        return m_sceneRenderer.CreateFSSampledImage("FSSampledImage", useRenderTergetScale);
    }

    void RendererSystem::Resize(uint32_t width, uint32_t height)
    {
        // Wait idle, we dont want to destroy objects in use
        RHII.WaitDeviceIdle();
        const bool hasRTGeom = SupportsRayTracingPass() && m_scene.GetTLAS() != nullptr;
        RHII.GetSurface()->SetActualExtent({0, 0, width, height});

        Swapchain *swapchain = RHII.GetSwapchain();
        Swapchain::Destroy(swapchain);

        Surface *surface = RHII.GetSurface();
        RHII.CreateSwapchain(surface);
        DispatchWindowTitle();

        m_sceneRenderer.CreateRenderTargets(hasRTGeom);

        m_sceneRenderer.ResizeRenderPassComponents(width, height, hasRTGeom);
    }

    void RendererSystem::ApplyPendingRenderScaleResize()
    {
        if (!m_sceneRenderer.NeedsRenderScaleResize())
            return;

        Resize(RHII.GetWidth(), RHII.GetHeight());

        // Scene loads queue this resize after the event pump has already run; avoid repeating it next frame.
        EventSystem::QueuedEvent resizeEvent;
        while (EventSystem::PeekAndPop(EventType::Resize, resizeEvent))
        {
        }
    }

    void RendererSystem::ApplyPendingOptionalRTSync()
    {
        const bool hasRTGeom = SupportsRayTracingPass() && m_scene.GetTLAS() != nullptr;
        if (!m_sceneRenderer.SyncOptionalRenderTargets(hasRTGeom))
            return;

        m_sceneRenderer.ResizeRenderPassComponents(RHII.GetWidth(), RHII.GetHeight(), hasRTGeom);
        BuildRenderGraph();
    }

    void RendererSystem::PollShaders(std::optional<size_t> hash)
    {
        m_sceneRenderer.PollShaders(hash);
    }
} // namespace pe
