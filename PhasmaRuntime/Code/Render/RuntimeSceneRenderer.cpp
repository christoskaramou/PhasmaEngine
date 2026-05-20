#include "Render/RuntimeSceneRenderer.h"
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

            CreateSceneRenderGraphPassComponents(m_renderPassComponents, SupportsRayTracingPass());

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
        m_scenePasses = GetGlobalSceneRenderGraphPassComponents();
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

        const SceneRenderGraphPassComponents scenePasses = GetSceneRenderGraphPassComponents();
        const auto isPassEnabled = [this](SceneRenderGraphPassId passId) -> bool
        {
            return m_renderGraphPassEnabled[static_cast<size_t>(passId)];
        };

        UpdateSceneRenderGraphPassComponents(m_renderPassComponents, scenePasses, isPassEnabled);
    }

    SceneRenderGraphPassComponents RuntimeSceneRenderer::GetSceneRenderGraphPassComponents() const
    {
        return m_scenePasses;
    }

    void RuntimeSceneRenderer::UpdateRenderGraphPassStates()
    {
        const bool hasRTGeom = SupportsRayTracingPass() && m_scene.GetTLAS() != nullptr;
        UpdateSceneRenderGraphPassStates(m_renderGraphPassEnabled, hasRTGeom);
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
            WaitSceneFrameCommand(frameCmd);
        }

        RHII.FlushDeletionQueue(frame);
        RHII.GetStagingManager()->RemoveUnused();
    }

    void RuntimeSceneRenderer::WaitAllFramesCommands()
    {
        WaitSceneFrameCommands(m_cmds);
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

        const SceneRenderGraphPassComponents scenePasses = GetSceneRenderGraphPassComponents();

        AddSceneRenderGraphPasses(m_renderGraph,
                                  scenePasses,
                                  [this](SceneRenderGraphPassId passId)
                                  { return m_renderGraphPassEnabled[static_cast<size_t>(passId)]; });
        m_renderGraph.Compile();
    }

    CommandBuffer *RuntimeSceneRenderer::RecordPasses(uint32_t imageIndex)
    {
        CommandBuffer *cmd = RHII.GetMainQueue()->AcquireCommandBuffer();

        {
            PE_PROFILE_SCOPE("Runtime Record Set Pass Scenes");
            SetSceneRenderGraphPassScene(GetSceneRenderGraphPassComponents(), m_scene);
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
        return CreateSceneRenderTarget(m_renderTargets, name, format, usage, useRenderTergetScale, useMips, clearColor);
    }

    Image *RuntimeSceneRenderer::CreateDepthStencilTarget(const std::string &name,
                                                          ::PeFormat format,
                                                          PeImageUsageFlags usage,
                                                          bool useRenderTergetScale,
                                                          float clearDepth,
                                                          uint32_t clearStencil)
    {
        return CreateSceneDepthStencilTarget(m_depthStencilTargets, name, format, usage, useRenderTergetScale, clearDepth, clearStencil);
    }

    Image *RuntimeSceneRenderer::GetRenderTarget(const std::string &name)
    {
        return GetSceneRenderTarget(m_renderTargets, name);
    }

    Image *RuntimeSceneRenderer::GetRenderTarget(size_t hash)
    {
        return GetSceneRenderTarget(m_renderTargets, hash);
    }

    Image *RuntimeSceneRenderer::GetDepthStencilTarget(const std::string &name)
    {
        return GetSceneRenderTarget(m_depthStencilTargets, name);
    }

    Image *RuntimeSceneRenderer::GetDepthStencilTarget(size_t hash)
    {
        return GetSceneRenderTarget(m_depthStencilTargets, hash);
    }

    Image *RuntimeSceneRenderer::CreateFSSampledImage(bool useRenderTergetScale)
    {
        return CreateSceneFSSampledImage("RuntimeFSSampledImage", useRenderTergetScale);
    }

    void RuntimeSceneRenderer::CreateRenderTargets()
    {
        const SceneRenderTargets targets = CreateDefaultSceneRenderTargets(m_renderTargets, m_depthStencilTargets);
        m_depthStencil = targets.depthStencil;
        m_viewportRT = targets.viewport;
        m_displayRT = targets.display;
        m_screenshotRT = targets.screenshot;
    }

    void RuntimeSceneRenderer::CreateFrameResources(uint32_t imageCount)
    {
        const bool isDx12 = UsesDx12RenderOrchestration();
        const PeBarrierSync semaphoreStageFlags = isDx12 ? PE_STAGE_NONE : PE_STAGE_ALL_COMMANDS;

        m_cmds.assign(imageCount, nullptr);
        CreateSceneFrameSemaphores(m_acquireSemaphores,
                                   m_submitSemaphores,
                                   imageCount,
                                   "RuntimeAcquireSemaphore_",
                                   "RuntimeSubmitSemaphore_",
                                   semaphoreStageFlags,
                                   semaphoreStageFlags);
    }

    void RuntimeSceneRenderer::DestroyFrameResources()
    {
        WaitSceneFrameCommands(m_cmds);
        m_cmds.clear();

        DestroySceneFrameSemaphores(m_acquireSemaphores);
        DestroySceneFrameSemaphores(m_submitSemaphores);
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
        BlitSceneImageToSwapchain(cmd, src, imageIndex);
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
