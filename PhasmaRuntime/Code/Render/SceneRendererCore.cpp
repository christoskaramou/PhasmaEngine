#include "Render/SceneRendererCore.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "Render/RenderPassShaderReload.h"
#include "Render/SceneScreenshot.h"
#include "Render/SceneSky.h"

namespace pe
{
    Image *SceneRendererCore::CreateRenderTarget(const std::string &name,
                                                 ::PeFormat format,
                                                 PeImageUsageFlags usage,
                                                 bool useRenderTargetScale,
                                                 bool useMips,
                                                 vec4 clearColor)
    {
        return CreateSceneRenderTarget(m_renderTargets, name, format, usage, useRenderTargetScale, useMips, clearColor);
    }

    Image *SceneRendererCore::CreateDepthStencilTarget(const std::string &name,
                                                       ::PeFormat format,
                                                       PeImageUsageFlags usage,
                                                       bool useRenderTargetScale,
                                                       float clearDepth,
                                                       uint32_t clearStencil)
    {
        return CreateSceneDepthStencilTarget(m_depthStencilTargets,
                                             name,
                                             format,
                                             usage,
                                             useRenderTargetScale,
                                             clearDepth,
                                             clearStencil);
    }

    Image *SceneRendererCore::GetRenderTarget(const std::string &name) const
    {
        return GetSceneRenderTarget(m_renderTargets, name);
    }

    Image *SceneRendererCore::GetRenderTarget(size_t hash) const
    {
        return GetSceneRenderTarget(m_renderTargets, hash);
    }

    bool SceneRendererCore::DestroyRenderTarget(const std::string &name)
    {
        return DestroySceneRenderTarget(m_renderTargets, name);
    }

    Image *SceneRendererCore::GetDepthStencilTarget(const std::string &name) const
    {
        return GetSceneRenderTarget(m_depthStencilTargets, name);
    }

    Image *SceneRendererCore::GetDepthStencilTarget(size_t hash) const
    {
        return GetSceneRenderTarget(m_depthStencilTargets, hash);
    }

    Image *SceneRendererCore::CreateFSSampledImage(const std::string &name, bool useRenderTargetScale)
    {
        return CreateSceneFSSampledImage(name, useRenderTargetScale);
    }

    void SceneRendererCore::CreateRenderTargets()
    {
        const SceneRenderTargets targets = CreateDefaultSceneRenderTargets(m_renderTargets, m_depthStencilTargets);
        m_depthStencil = targets.depthStencil;
        m_viewportRT = targets.viewport;
        m_displayRT = targets.display;
        m_screenshotRT = targets.screenshot;
    }

    void SceneRendererCore::DestroyRenderTargets()
    {
        DestroySceneRenderTargets(m_renderTargets, m_depthStencilTargets);
        m_depthStencil = nullptr;
        m_viewportRT = nullptr;
        m_displayRT = nullptr;
        m_screenshotRT = nullptr;
    }

    void SceneRendererCore::BlitToSwapchain(CommandBuffer *cmd, Image *src, uint32_t imageIndex)
    {
        BlitSceneImageToSwapchain(cmd, src, imageIndex);
    }

    void SceneRendererCore::LoadSky(CommandBuffer *cmd)
    {
        LoadDefaultSceneSky(cmd, m_skyBox, m_ibl_brdf_lut);
    }

    void SceneRendererCore::ReloadSkyFromSettings()
    {
        RHII.WaitDeviceIdle();

        Queue *queue = RHII.GetMainQueue();
        CommandBuffer *cmd = queue->AcquireCommandBuffer();
        cmd->Begin();
        LoadConfiguredSceneSkybox(cmd, m_skyBox);
        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        cmd->Return();
    }

    void SceneRendererCore::DestroySky()
    {
        DestroyDefaultSceneSky(m_skyBox, m_ibl_brdf_lut);
    }

    void SceneRendererCore::CreateRenderPassComponents(bool includeRayTracingPass, CommandBuffer *cmd)
    {
        (void)cmd;
        CreateSceneRenderGraphPassComponents(m_renderPassComponents, includeRayTracingPass);
    }

    void SceneRendererCore::DestroyRenderPassComponents()
    {
        DestroyInitializedSceneRenderGraphPassComponents(m_scenePasses, m_renderGraphPassInitialized);
    }

    void SceneRendererCore::PrepareRenderTargetResize(bool hasRayTracingGeometry)
    {
        pe::UpdateSceneRenderGraphPassStates(m_renderGraphPassEnabled, hasRayTracingGeometry);
        DestroyDisabledRenderPassComponents();
    }

    void SceneRendererCore::ResizeRenderPassComponents(uint32_t width, uint32_t height, bool hasRayTracingGeometry)
    {
        pe::UpdateSceneRenderGraphPassStates(m_renderGraphPassEnabled, hasRayTracingGeometry);
        ResizeInitializedSceneRenderGraphPassComponents(m_scenePasses, [this](SceneRenderGraphPassId passId)
                                                        { return IsPassEnabled(passId); }, m_renderGraphPassInitialized, width, height);
        InitEnabledRenderPassComponents(nullptr);
    }

    void SceneRendererCore::CacheGlobalComponents()
    {
        m_scenePasses = GetGlobalSceneRenderGraphPassComponents();
    }

    void SceneRendererCore::UpdateRenderGraphPassStates(bool hasRayTracingGeometry, CommandBuffer *cmd)
    {
        pe::UpdateSceneRenderGraphPassStates(m_renderGraphPassEnabled, hasRayTracingGeometry);
        if (HasDisabledRenderPassComponents())
        {
            RHII.WaitDeviceIdle();
            DestroyDisabledRenderPassComponents();
        }

        // Per-frame setting toggles call this with no command buffer; lazily initable passes must tolerate nullptr here.
        InitEnabledRenderPassComponents(cmd);
    }

    bool SceneRendererCore::IsPassEnabled(SceneRenderGraphPassId passId) const
    {
        return m_renderGraphPassEnabled[static_cast<size_t>(passId)];
    }

    void SceneRendererCore::AddScenePassesToRenderGraph()
    {
        AddSceneRenderGraphPasses(m_renderGraph,
                                  m_scenePasses,
                                  [this](SceneRenderGraphPassId passId)
                                  { return IsPassEnabled(passId); });
    }

    void SceneRendererCore::UpdateRenderPassComponents()
    {
        UpdateSceneRenderGraphPassComponents(m_renderPassComponents,
                                             m_scenePasses,
                                             [this](SceneRenderGraphPassId passId)
                                             { return IsPassEnabled(passId); });
    }

    void SceneRendererCore::InitEnabledRenderPassComponents(CommandBuffer *cmd)
    {
        InitEnabledSceneRenderGraphPassComponents(m_scenePasses, [this](SceneRenderGraphPassId passId)
                                                  { return IsPassEnabled(passId); }, m_renderGraphPassInitialized, cmd);
    }

    bool SceneRendererCore::HasDisabledRenderPassComponents() const
    {
        return HasDisabledInitializedSceneRenderGraphPassComponents(m_scenePasses, [this](SceneRenderGraphPassId passId)
                                                                    { return IsPassEnabled(passId); }, m_renderGraphPassInitialized);
    }

    void SceneRendererCore::DestroyDisabledRenderPassComponents()
    {
        DestroyDisabledSceneRenderGraphPassComponents(m_scenePasses, [this](SceneRenderGraphPassId passId)
                                                      { return IsPassEnabled(passId); }, m_renderGraphPassInitialized);
    }

    void SceneRendererCore::SetRenderPassScene(Scene &scene)
    {
        SetSceneRenderGraphPassScene(m_scenePasses, scene);
    }

    void SceneRendererCore::ExecuteRenderGraph(CommandBuffer *cmd)
    {
        m_renderGraph.Execute(cmd);
    }

    void SceneRendererCore::PollShaders(std::optional<size_t> hash)
    {
        ReloadRenderPassShaders(m_renderPassComponents, hash);
    }

    void SceneRendererCore::CreateFrameResources(uint32_t imageCount,
                                                 std::string_view acquireNamePrefix,
                                                 std::string_view submitNamePrefix,
                                                 PeBarrierSync acquireStageFlags,
                                                 PeBarrierSync submitStageFlags)
    {
        m_cmds.assign(imageCount, nullptr);
        CreateSceneFrameSemaphores(m_acquireSemaphores,
                                   m_submitSemaphores,
                                   imageCount,
                                   acquireNamePrefix,
                                   submitNamePrefix,
                                   acquireStageFlags,
                                   submitStageFlags);
    }

    void SceneRendererCore::DestroyFrameResources()
    {
        WaitSceneFrameCommands(m_cmds);
        m_cmds.clear();

        DestroySceneFrameSemaphores(m_acquireSemaphores);
        DestroySceneFrameSemaphores(m_submitSemaphores);
    }

    void SceneRendererCore::WaitPreviousFrameCommands()
    {
        WaitPreviousSceneFrameCommand(m_cmds);
    }

    void SceneRendererCore::WaitAllFrameCommands()
    {
        WaitSceneFrameCommandsAndCleanup(m_cmds);
    }

    void SceneRendererCore::TransitionSwapchainImagesToPresent(CommandBuffer *cmd)
    {
        TransitionSceneSwapchainImagesToPresent(cmd);
    }

    void SceneRendererCore::SubmitAndPresent(const SceneFrameRecordCallback &recordFrame,
                                             const SceneFrameScreenshotCallback &saveScreenshot,
                                             const SceneFrameSubmitLabels &labels)
    {
        SubmitAndPresentSceneFrame(m_cmds, m_acquireSemaphores, m_submitSemaphores, recordFrame, m_screenshotPending, saveScreenshot, labels);
    }

    void SceneRendererCore::SetScreenshotPath(std::string path)
    {
        m_screenshotPath = std::move(path);
    }

    void SceneRendererCore::QueueScreenshotReadback(CommandBuffer *cmd, Image *sourceImage, const std::string &stagingBufferName)
    {
        m_screenshotPending =
            QueueSceneScreenshotReadback(cmd, sourceImage, m_screenshotRT, m_screenshotBuffer, m_screenshotRowPitch, stagingBufferName);
    }

    bool SceneRendererCore::SaveScreenshot(std::string *savedPath)
    {
        return SaveSceneScreenshot(m_screenshotBuffer, m_screenshotRT, m_screenshotPath, m_screenshotRowPitch, savedPath);
    }

    void SceneRendererCore::DestroyScreenshotBuffer()
    {
        Buffer::Destroy(m_screenshotBuffer);
        m_screenshotRowPitch = 0;
        m_screenshotPending = false;
    }
} // namespace pe
