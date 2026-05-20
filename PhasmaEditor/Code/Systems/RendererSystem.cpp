#include "RendererSystem.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Debug.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/StagingManager.h"
#include "API/Surface.h"
#include "API/Swapchain.h"
#include "Render/RenderPassShaderReload.h"
#include "Render/SceneFrameResources.h"
#include "Render/SceneRenderGraph.h"
#include "Render/SceneRenderTargets.h"
#include "Render/SceneScreenshot.h"
#include "RenderPasses/TAAPass.h"
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
        SetActiveSceneRendererHost(this);

        const bool isDx12 = UsesDx12RenderOrchestration();

        // Set Window Title
        std::string title = "PhasmaEngine";
        title += " - Device: " + RHII.GetGpuName();
        title += " - API: " + std::string(PeGraphicsApiName(RHII.GetApi()));
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

        CreateSceneRenderGraphPassComponents(m_renderPassComponents, SupportsRayTracingPass());

        for (auto &renderPassComponent : m_renderPassComponents)
        {
            renderPassComponent->Init();
            renderPassComponent->UpdatePassInfo();
            renderPassComponent->CreateUniforms(initCmd);
        }

        // Init GUI
        m_gui.Init();

        uint32_t imageCount = RHII.GetSwapchainImageCount();
        m_cmds.resize(imageCount, nullptr);
        TransitionSceneSwapchainImagesToPresent(initCmd);

        m_scene.UploadBuffers(initCmd);
        CacheGlobalComponents();
        BuildRenderGraph();

        const PeBarrierSync acquireStageFlags = isDx12 ? PE_STAGE_NONE
                                                       : PE_STAGE_COLOR_ATTACHMENT_OUTPUT | PE_STAGE_COMPUTE_SHADER |
                                                             PE_STAGE_RAY_TRACING_SHADER_KHR | PE_STAGE_TRANSFER;
        const PeBarrierSync submitStageFlags = isDx12 ? PE_STAGE_NONE : PE_STAGE_ALL_COMMANDS;
        CreateSceneFrameSemaphores(m_acquireSemaphores,
                                   m_submitSemaphores,
                                   imageCount,
                                   "AcquireSemaphore_",
                                   "SubmitSemaphore_",
                                   acquireStageFlags,
                                   submitStageFlags);

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
        m_scenePasses = GetGlobalSceneRenderGraphPassComponents();
    }

    void RendererSystem::Update()
    {
        {
            PE_PROFILE_SCOPE("GUI");
            m_gui.Update();
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

        const SceneRenderGraphPassComponents scenePasses = GetSceneRenderGraphPassComponents();
        const auto isPassEnabled = [this](SceneRenderGraphPassId passId) -> bool
        {
            return m_renderGraphPassEnabled[static_cast<size_t>(passId)];
        };

        // Render Components
        {
            PE_PROFILE_SCOPE("Render Pass Updates");
            UpdateSceneRenderGraphPassComponents(m_renderPassComponents, scenePasses, isPassEnabled);
        }
    }

    SceneRenderGraphPassComponents RendererSystem::GetSceneRenderGraphPassComponents() const
    {
        return m_scenePasses;
    }

    void RendererSystem::UpdateRenderGraphPassStates()
    {
        const bool hasRTGeom = SupportsRayTracingPass() && m_scene.GetTLAS() != nullptr;
        UpdateSceneRenderGraphPassStates(m_renderGraphPassEnabled, hasRTGeom);
        m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::GUI)] = m_gui.Render() || HasRuntimeUiPass();
    }

    void RendererSystem::WaitPreviousFrameCommands()
    {
        uint32_t frame = RHII.GetFrameIndex();

        auto &frameCmd = m_cmds[frame];
        if (frameCmd)
        {
            PE_PROFILE_SCOPE("Cmd Wait");
            WaitSceneFrameCommand(frameCmd);
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
        if (auto *taaPass = static_cast<TAAPass *>(m_scenePasses.taa))
            taaPass->RequestHistoryReset();
    }

    void RendererSystem::WaitAllFramesCommands()
    {
        WaitSceneFrameCommands(m_cmds);
        RHII.GetStagingManager()->RemoveUnused();
    }

    void RendererSystem::BuildRenderGraph()
    {
        m_renderGraph.Clear();

        UpdateRenderGraphPassStates();

        const SceneRenderGraphPassComponents scenePasses = GetSceneRenderGraphPassComponents();

        AddSceneRenderGraphPasses(m_renderGraph,
                                  scenePasses,
                                  [this](SceneRenderGraphPassId passId)
                                  { return m_renderGraphPassEnabled[static_cast<size_t>(passId)]; });
        auto isGuiPassEnabled = [this]()
        {
            return m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::GUI)];
        };
        m_renderGraph.AddPass(static_cast<RenderGraph::PassID>(RenderGraphPassId::GUI),
                              10000,
                              "GUI",
                              isGuiPassEnabled,
                              [this](CommandBuffer *cmd)
                              { m_gui.ExecutePass(cmd); });
        m_renderGraph.Compile();
    }

    CommandBuffer *RendererSystem::RecordPasses(uint32_t imageIndex)
    {
        CommandBuffer *cmd = RHII.GetMainQueue()->AcquireCommandBuffer();
        const bool isDx12 = UsesDx12RenderOrchestration();
        Image *dx12SwapchainImage = isDx12 ? RHII.GetSwapchain()->GetImage(imageIndex) : nullptr;

        // Set scene on all scene-dependent passes before execution.
        {
            PE_PROFILE_SCOPE("Record Set Pass Scenes");
            SetSceneRenderGraphPassScene(GetSceneRenderGraphPassComponents(), m_scene);
        }

        {
            PE_PROFILE_SCOPE("Record Cmd Begin");
            cmd->Begin();
        }
        {
            PE_PROFILE_SCOPE("Render Graph Execute");
            m_renderGraph.Execute(cmd);
        }

        if (isDx12)
        {
            Image *frameOutputImage = GetFrameOutputImage();

            if (frameOutputImage)
            {
                {
                    PE_PROFILE_SCOPE("DX12 FrameOutput Blit");
                    BlitToSwapchain(cmd, frameOutputImage, imageIndex);
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
                BlitToSwapchain(cmd, m_displayRT, imageIndex);
            }

            {
                PE_PROFILE_SCOPE("Vulkan Screenshot Readback");
                QueueScreenshotReadback(cmd, m_displayRT);
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
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::Upsample)] ||
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::TAA)] ||
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::Sharpen)] ||
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::Tonemap)] ||
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::BloomV)] ||
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::DOF)] ||
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::MotionBlur)] ||
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::Grid)] ||
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::GUI)];
        if (displayProduced)
            return m_displayRT;

        const bool viewportProduced =
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::LightOpaque)] ||
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::LightTransparent)] ||
            m_renderGraphPassEnabled[static_cast<size_t>(RenderGraphPassId::RayTracing)];
        if (viewportProduced)
            return m_viewportRT;

        return nullptr;
    }

    void RendererSystem::QueueScreenshotReadback(CommandBuffer *cmd, Image *sourceImage)
    {
        EventSystem::QueuedEvent screenshotEvt;
        if (!sourceImage || !EventSystem::PeekAndPop(EventType::Screenshot, screenshotEvt))
            return;

        m_screenshotPath = screenshotEvt.payload.has_value()
                               ? std::any_cast<std::string>(screenshotEvt.payload)
                               : std::string();

        m_screenshotPending =
            QueueSceneScreenshotReadback(cmd, sourceImage, m_screenshotRT, m_screenshotBuffer, m_screenshotRowPitch, "ScreenshotStaging");
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
        catch (const SwapchainOutOfDateError &)
        {
            // Just ignore and try again
        }
    }

    void RendererSystem::SaveScreenshot()
    {
        std::string savedPath;
        if (SaveSceneScreenshot(m_screenshotBuffer, m_screenshotRT, m_screenshotPath, m_screenshotRowPitch, &savedPath))
            m_screenshotSavedPath = savedPath;
    }

    void RendererSystem::DrawPlatformWindows()
    {
        m_gui.DrawPlatformWindows();
    }

    void RendererSystem::Destroy()
    {
        RHII.WaitDeviceIdle();

        WaitSceneFrameCommands(m_cmds);

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

        DestroySceneFrameSemaphores(m_acquireSemaphores);
        DestroySceneFrameSemaphores(m_submitSemaphores);

        Buffer::Destroy(m_screenshotBuffer);

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
        return CreateSceneRenderTarget(m_renderTargets, name, format, usage, useRenderTergetScale, useMips, clearColor);
    }

    Image *RendererSystem::CreateDepthStencilTarget(const std::string &name,
                                                    ::PeFormat format,
                                                    PeImageUsageFlags usage,
                                                    bool useRenderTergetScale,
                                                    float clearDepth,
                                                    uint32_t clearStencil)
    {
        return CreateSceneDepthStencilTarget(m_depthStencilTargets, name, format, usage, useRenderTergetScale, clearDepth, clearStencil);
    }

    Image *RendererSystem::GetRenderTarget(const std::string &name)
    {
        return GetSceneRenderTarget(m_renderTargets, name);
    }

    Image *RendererSystem::GetRenderTarget(size_t hash)
    {
        return GetSceneRenderTarget(m_renderTargets, hash);
    }

    Image *RendererSystem::GetDepthStencilTarget(const std::string &name)
    {
        return GetSceneRenderTarget(m_depthStencilTargets, name);
    }

    Image *RendererSystem::GetDepthStencilTarget(size_t hash)
    {
        return GetSceneRenderTarget(m_depthStencilTargets, hash);
    }

    Image *RendererSystem::CreateFSSampledImage(bool useRenderTergetScale)
    {
        return CreateSceneFSSampledImage("FSSampledImage", useRenderTergetScale);
    }

    void RendererSystem::CreateRenderTargets()
    {
        const SceneRenderTargets targets = CreateDefaultSceneRenderTargets(m_renderTargets, m_depthStencilTargets);
        m_depthStencil = targets.depthStencil;
        m_viewportRT = targets.viewport;
        m_displayRT = targets.display;
        m_screenshotRT = targets.screenshot;
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
        BlitSceneImageToSwapchain(cmd, src, imageIndex);
    }

    void RendererSystem::PollShaders(std::optional<size_t> hash)
    {
        RHII.WaitDeviceIdle();

        for (auto &rc : m_renderPassComponents)
            ReloadRenderPassShaders(*rc, hash);
    }
} // namespace pe
