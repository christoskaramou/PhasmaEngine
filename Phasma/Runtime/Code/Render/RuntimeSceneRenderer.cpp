#include "Render/RuntimeSceneRenderer.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/Surface.h"
#include "API/Swapchain.h"
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
        auto &settings = Settings::Get<SceneSettings>();

        // These are editor overlays, not standalone player output.
        settings.draw_grid = false;
        settings.draw_aabbs = false;
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

            m_sceneRenderer.CreateRenderTargets();
            m_sceneRenderer.LoadSky(initCmd);

            m_sceneRenderer.CreateRenderPassComponents(SupportsRayTracingPass(), initCmd);

            const uint32_t imageCount = RHII.GetSwapchainImageCount();
            const bool isDx12 = UsesDx12RenderOrchestration();
            const PeBarrierSync semaphoreStageFlags = isDx12 ? PE_STAGE_NONE : PE_STAGE_ALL_COMMANDS;
            m_sceneRenderer.CreateFrameResources(imageCount,
                                                 "RuntimeAcquireSemaphore_",
                                                 "RuntimeSubmitSemaphore_",
                                                 semaphoreStageFlags,
                                                 semaphoreStageFlags);

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
        catch (...)
        {
            Destroy();
            throw;
        }
    }

    void RuntimeSceneRenderer::Update()
    {
        ApplyRuntimeRenderSettings();

        // Scripts added/removed render passes (render_graph.add_pass): rebuild the
        // graph before this frame records. Pure CPU pass-list rebuild, no GPU wait.
        if (m_scriptRenderPassesRevision != GetScriptRenderPassesRevision())
            BuildRenderGraph();
        ApplyPendingRenderScaleResize();

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

        m_sceneRenderer.UpdateRenderPassComponents();
    }

    void RuntimeSceneRenderer::UpdateRenderGraphPassStates()
    {
        const bool hasRTGeom = SupportsRayTracingPass() && m_scene.GetTLAS() != nullptr;
        m_sceneRenderer.UpdateRenderGraphPassStates(hasRTGeom);
    }

    void RuntimeSceneRenderer::WaitPreviousFrameCommands()
    {
        if (!m_initialized)
            return;

        m_sceneRenderer.WaitPreviousFrameCommands();
    }

    void RuntimeSceneRenderer::WaitAllFramesCommands()
    {
        m_sceneRenderer.WaitAllFrameCommands();
    }

    void RuntimeSceneRenderer::RequestScreenshot(std::string path)
    {
        m_sceneRenderer.SetScreenshotPath(std::move(path));
        m_screenshotRequested = true;
    }

    void RuntimeSceneRenderer::PollShaders(std::optional<size_t> hash)
    {
        m_sceneRenderer.PollShaders(hash);
    }

    void RuntimeSceneRenderer::BuildRenderGraph()
    {
        m_scriptRenderPassesRevision = GetScriptRenderPassesRevision();
        RenderGraph &renderGraph = m_sceneRenderer.GetRenderGraph();
        renderGraph.Clear();

        UpdateRenderGraphPassStates();

        m_sceneRenderer.AddScenePassesToRenderGraph();
        renderGraph.Compile();
    }

    CommandBuffer *RuntimeSceneRenderer::RecordPasses(uint32_t imageIndex)
    {
        CommandBuffer *cmd = RHII.GetMainQueue()->AcquireCommandBuffer();

        {
            PE_PROFILE_SCOPE("Runtime Record Set Pass Scenes");
            m_sceneRenderer.SetRenderPassScene(m_scene);
        }

        cmd->Begin();
        m_sceneRenderer.ExecuteRenderGraph(cmd);
        Image *displayRT = m_sceneRenderer.GetDisplayRT();
        if (m_runtimeUi)
            m_runtimeUi->Render(cmd, displayRT);
        m_sceneRenderer.BlitToSwapchain(cmd, displayRT, imageIndex);
        QueueScreenshotReadback(cmd, displayRT);
        Debug::CollectGpuTrace(cmd);
        cmd->End();

        return cmd;
    }

    void RuntimeSceneRenderer::Draw()
    {
        if (!m_initialized)
            return;

        m_sceneRenderer.SubmitAndPresent([this](uint32_t imageIndex)
                                         { return RecordPasses(imageIndex); },
                                         [this]()
                                         { (void)m_sceneRenderer.SaveScreenshot(); },
                                         {"Runtime Acquire Image",
                                          "Runtime Record Passes",
                                          "Runtime Queue Submit",
                                          "Runtime Present"});
    }

    void RuntimeSceneRenderer::Destroy()
    {
        if (!m_initialized)
            return;

        RHII.WaitDeviceIdle();
        m_sceneRenderer.DestroyFrameResources();

        m_sceneRenderer.DestroyRenderPassComponents();

        m_sceneRenderer.DestroySky();
        m_sceneRenderer.DestroyScreenshotBuffer();

        m_sceneRenderer.DestroyRenderTargets();

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
        return m_sceneRenderer.CreateRenderTarget(name, format, usage, useRenderTergetScale, useMips, clearColor);
    }

    Image *RuntimeSceneRenderer::CreateDepthStencilTarget(const std::string &name,
                                                          ::PeFormat format,
                                                          PeImageUsageFlags usage,
                                                          bool useRenderTergetScale,
                                                          float clearDepth,
                                                          uint32_t clearStencil)
    {
        return m_sceneRenderer.CreateDepthStencilTarget(name, format, usage, useRenderTergetScale, clearDepth, clearStencil);
    }

    Image *RuntimeSceneRenderer::GetRenderTarget(const std::string &name)
    {
        return m_sceneRenderer.GetRenderTarget(name);
    }

    Image *RuntimeSceneRenderer::GetRenderTarget(size_t hash)
    {
        return m_sceneRenderer.GetRenderTarget(hash);
    }

    bool RuntimeSceneRenderer::DestroyRenderTarget(const std::string &name)
    {
        return m_sceneRenderer.DestroyRenderTarget(name);
    }

    Image *RuntimeSceneRenderer::GetDepthStencilTarget(const std::string &name)
    {
        return m_sceneRenderer.GetDepthStencilTarget(name);
    }

    Image *RuntimeSceneRenderer::GetDepthStencilTarget(size_t hash)
    {
        return m_sceneRenderer.GetDepthStencilTarget(hash);
    }

    Image *RuntimeSceneRenderer::CreateFSSampledImage(bool useRenderTergetScale)
    {
        return m_sceneRenderer.CreateFSSampledImage("RuntimeFSSampledImage", useRenderTergetScale);
    }

    void RuntimeSceneRenderer::Resize(uint32_t width, uint32_t height)
    {
        if (!m_initialized)
            return;

        RHII.WaitDeviceIdle();
        const bool hasRTGeom = SupportsRayTracingPass() && m_scene.GetTLAS() != nullptr;
        m_sceneRenderer.PrepareRenderTargetResize(hasRTGeom);
        m_sceneRenderer.DestroyFrameResources();
        RHII.GetSurface()->SetActualExtent({0, 0, width, height});

        Swapchain *swapchain = RHII.GetSwapchain();
        Swapchain::Destroy(swapchain);

        Surface *surface = RHII.GetSurface();
        RHII.CreateSwapchain(surface);

        m_sceneRenderer.CreateRenderTargets();
        const bool isDx12 = UsesDx12RenderOrchestration();
        const PeBarrierSync semaphoreStageFlags = isDx12 ? PE_STAGE_NONE : PE_STAGE_ALL_COMMANDS;
        m_sceneRenderer.CreateFrameResources(RHII.GetSwapchainImageCount(),
                                             "RuntimeAcquireSemaphore_",
                                             "RuntimeSubmitSemaphore_",
                                             semaphoreStageFlags,
                                             semaphoreStageFlags);

        m_sceneRenderer.ResizeRenderPassComponents(width, height, hasRTGeom);

        BuildRenderGraph();
    }

    void RuntimeSceneRenderer::ApplyPendingRenderScaleResize()
    {
        if (!m_sceneRenderer.NeedsRenderScaleResize())
            return;

        Resize(RHII.GetWidth(), RHII.GetHeight());
    }

    void RuntimeSceneRenderer::QueueScreenshotReadback(CommandBuffer *cmd, Image *sourceImage)
    {
        if (!m_screenshotRequested || !sourceImage)
            return;

        m_screenshotRequested = false;
        m_sceneRenderer.QueueScreenshotReadback(cmd, sourceImage, "RuntimeScreenshotStaging");
    }

} // namespace pe
