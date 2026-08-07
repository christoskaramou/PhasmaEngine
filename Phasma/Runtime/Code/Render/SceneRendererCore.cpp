#include "Render/SceneRendererCore.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "Render/RenderPassShaderReload.h"
#include "Render/SceneScreenshot.h"
#include "Render/SceneSky.h"
#include "Render/ScriptRenderPasses.h"
#include "Scene/Scene.h"

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
        if (m_frameDisplayOverride && name == "display")
            return m_frameDisplayOverride;
        return GetSceneRenderTarget(m_renderTargets, name);
    }

    Image *SceneRendererCore::GetRenderTarget(size_t hash) const
    {
        if (m_frameDisplayOverride && hash == static_cast<size_t>(StringHash("display")))
            return m_frameDisplayOverride;
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

    void SceneRendererCore::CreateRenderTargets(bool hasRayTracingGeometry, bool scaleOutput)
    {
        const SceneRenderTargets targets =
            CreateDefaultSceneRenderTargets(m_renderTargets, m_depthStencilTargets, hasRayTracingGeometry, scaleOutput);
        m_depthStencil = targets.depthStencil;
        m_viewportRT = targets.viewport;
        m_displayRT = targets.display;
        m_screenshotRT = targets.screenshot;
        m_renderTargetScale = Settings::Get<SceneSettings>().render_scale;
    }

    void SceneRendererCore::DestroyRenderTargets()
    {
        DestroySceneRenderTargets(m_renderTargets, m_depthStencilTargets);
        m_depthStencil = nullptr;
        m_viewportRT = nullptr;
        m_displayRT = nullptr;
        m_screenshotRT = nullptr;
        m_renderTargetScale = 0.0f;
    }

    bool SceneRendererCore::NeedsRenderScaleResize() const
    {
        return m_displayRT && m_renderTargetScale != Settings::Get<SceneSettings>().render_scale;
    }

    bool SceneRendererCore::SyncOptionalRenderTargets(bool hasRayTracingGeometry)
    {
        return SyncOptionalSceneRenderTargets(m_renderTargets, hasRayTracingGeometry);
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

    void SceneRendererCore::ResizeRenderPassComponents(uint32_t width, uint32_t height, bool hasRayTracingGeometry)
    {
        pe::UpdateSceneRenderGraphPassStates(m_renderGraphPassEnabled, hasRayTracingGeometry);
        ResizeInitializedSceneRenderGraphPassComponents(m_scenePasses, m_renderGraphPassInitialized, width, height);
        InitEnabledRenderPassComponents(nullptr);
    }

    void SceneRendererCore::CacheGlobalComponents()
    {
        m_scenePasses = GetGlobalSceneRenderGraphPassComponents();
    }

    void SceneRendererCore::UpdateRenderGraphPassStates(bool hasRayTracingGeometry, CommandBuffer *cmd)
    {
        pe::UpdateSceneRenderGraphPassStates(m_renderGraphPassEnabled, hasRayTracingGeometry);

        // Disabled passes stay initialized and are skipped when recording (destroy/re-init on mode
        // toggles caused device loss). Setting toggles pass no command buffer; lazily initable
        // passes must tolerate nullptr here.
        InitEnabledRenderPassComponents(cmd);
    }

    bool SceneRendererCore::IsPassEnabled(SceneRenderGraphPassId passId) const
    {
        return m_renderGraphPassEnabled[static_cast<size_t>(passId)];
    }

    void SceneRendererCore::SetPassDeferred(SceneRenderGraphPassId passId, bool deferred)
    {
        m_renderGraphPassDeferred[static_cast<size_t>(passId)] = deferred;
    }

    bool SceneRendererCore::IsPassDeferred(SceneRenderGraphPassId passId) const
    {
        return m_renderGraphPassDeferred[static_cast<size_t>(passId)];
    }

    void SceneRendererCore::AddScenePassesToRenderGraph()
    {
        AddSceneRenderGraphPasses(m_renderGraph,
                                  m_scenePasses,
                                  [this](SceneRenderGraphPassId passId)
                                  { return IsPassEnabled(passId) && !IsPassDeferred(passId); });

        // Deferred passes run in their normal graph order, just later in the frame: the player
        // executes this graph after compositing its UI so display-space post covers the whole
        // image. Own graph, so RenderGraph still issues each pass's declared input barriers.
        m_deferredRenderGraph.Clear();
        AddSceneRenderGraphPasses(m_deferredRenderGraph,
                                  m_scenePasses,
                                  [this](SceneRenderGraphPassId passId)
                                  { return IsPassEnabled(passId) && IsPassDeferred(passId); });
        m_deferredRenderGraph.Compile();

        // Script-registered passes (render_graph.add_pass): inserted by order among
        // the built-in passes; RenderGraph::Compile sorts them into place. The graph
        // holds only the pass name - the callback (and its Lua refs) stays in the
        // registry, so script reloads can release Lua state safely; a stale graph
        // entry resolves to nothing and no-ops until the next rebuild.
        const std::vector<ScriptRenderPass> &scriptPasses = GetScriptRenderPasses();
        for (size_t i = 0; i < scriptPasses.size(); i++)
        {
            std::string passName = scriptPasses[i].name;
            m_renderGraph.AddPass(static_cast<RenderGraph::PassID>(kScriptRenderPassIdBase + i), scriptPasses[i].order, passName, []()
                                  { return true; }, [passName](CommandBuffer *cmd)
                                  {
                                      const ScriptRenderPass *pass = FindScriptRenderPass(passName);
                                      if (pass && pass->execute)
                                          pass->execute(cmd); });
        }
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

    void SceneRendererCore::SetRenderPassScene(Scene &scene)
    {
        m_scene = &scene;
        SetSceneRenderGraphPassScene(m_scenePasses, scene);
    }

    void SceneRendererCore::ExecuteRenderGraph(CommandBuffer *cmd)
    {
        if (m_scene)
            m_scene->UploadDynamicUniforms(cmd);

        m_renderGraph.Execute(cmd);
    }

    void SceneRendererCore::ExecuteDeferredRenderGraph(CommandBuffer *cmd)
    {
        m_deferredRenderGraph.Execute(cmd);
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
