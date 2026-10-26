#pragma once

#include "API/RenderGraph.h"
#include "Render/SceneFrameResources.h"
#include "Render/SceneRenderGraph.h"
#include "Render/SceneRenderTargets.h"
#include "Render/SceneRendererHost.h"
#include "Skybox/Skybox.h"

namespace pe
{
    class Buffer;
    class CommandBuffer;
    class Image;
    class ModulePipelines;
    class PassInfo;
    class RayTracingPass;
    class Scene;
    class Semaphore;

    class SceneRendererCore : public NoCopy, public NoMove
    {
    public:
        RenderGraph &GetRenderGraph() { return m_renderGraph; }
        const RenderGraph &GetRenderGraph() const { return m_renderGraph; }

        Image *GetDisplayRT() const { return m_frameDisplayOverride ? m_frameDisplayOverride : m_displayRT; }
        // ponytail: Player can alias display→swapchain for one frame to skip the 1:1 present blit.
        void SetFrameDisplayOverride(Image *image) { m_frameDisplayOverride = image; }
        Image *GetViewportRT() const { return m_viewportRT; }
        Image *GetDepthStencilRT() const { return m_depthStencil; }
        Image *GetScreenshotRT() const { return m_screenshotRT; }
        const SkyBox &GetSkyBox() const { return m_skyBox; }
        Image *GetIBL_LUT() const { return m_ibl_brdf_lut; }

        Image *CreateRenderTarget(const std::string &name,
                                  ::PeFormat format,
                                  PeImageUsageFlags usage = PE_IMAGE_USAGE_NONE,
                                  bool useRenderTargetScale = true,
                                  bool useMips = false,
                                  vec4 clearColor = Color::Transparent);
        Image *CreateDepthStencilTarget(const std::string &name,
                                        ::PeFormat format,
                                        PeImageUsageFlags usage = PE_IMAGE_USAGE_NONE,
                                        bool useRenderTargetScale = true,
                                        float clearDepth = Color::Depth,
                                        uint32_t clearStencil = Color::Stencil);
        Image *GetRenderTarget(const std::string &name) const;
        Image *GetRenderTarget(size_t hash) const;
        bool DestroyRenderTarget(const std::string &name);
        Image *GetDepthStencilTarget(const std::string &name) const;
        Image *GetDepthStencilTarget(size_t hash) const;
        Image *CreateFSSampledImage(const std::string &name, Image *source);
        void CreateRenderTargets(bool hasRayTracingGeometry = false, bool scaleOutput = false);
        void DestroyRenderTargets();
        bool NeedsRenderScaleResize() const;
        // Synchronize optional velocity membership and configurable scene-target formats.
        // Returns true if membership/format changed — callers should refresh pass attachments.
        bool SyncOptionalRenderTargets(bool hasRayTracingGeometry);
        void BlitToSwapchain(CommandBuffer *cmd, Image *src, uint32_t imageIndex);

        void LoadSky(CommandBuffer *cmd);
        void ReloadSkyFromSettings();
        void SyncOpenPBREnvironment();
        void DestroySky();

        void CreateRenderPassComponents(bool includeRayTracingPass, CommandBuffer *cmd);
        void DestroyRenderPassComponents();
        void ResizeRenderPassComponents(uint32_t width, uint32_t height, bool hasRayTracingGeometry);
        void CacheGlobalComponents();
        const SceneRenderGraphPassComponents &GetSceneRenderGraphPassComponents() const { return m_scenePasses; }
        void UpdateRenderGraphPassStates(bool hasRayTracingGeometry, CommandBuffer *cmd = nullptr);
        bool IsPassEnabled(SceneRenderGraphPassId passId) const;
        // Keeps an enabled pass out of the graph so the host can run it itself later in the
        // frame (the player grades after compositing its UI). Init/Update still see it enabled.
        void SetPassDeferred(SceneRenderGraphPassId passId, bool deferred);
        bool IsPassDeferred(SceneRenderGraphPassId passId) const;
        void AddScenePassesToRenderGraph();
        bool NeedsRenderGraphOrderRebuild() const { return m_renderPassOrdersRevision != Settings::Get<SceneSettings>().render_pass_orders_revision; }
        void UpdateRenderPassComponents();
        void SetRenderPassScene(Scene &scene);
        void ExecuteRenderGraph(CommandBuffer *cmd);
        uint64_t GetRenderedFrames() const { return m_renderedFrames; }
        OpenPBRBuildProgress GetOpenPBRBuildProgress() const;
        void ExecuteDeferredRenderGraph(CommandBuffer *cmd);
        void PollShaders(std::optional<size_t> hash = std::nullopt);

        void CreateFrameResources(uint32_t imageCount,
                                  std::string_view acquireNamePrefix,
                                  std::string_view submitNamePrefix,
                                  PeBarrierSync acquireStageFlags,
                                  PeBarrierSync submitStageFlags);
        void DestroyFrameResources();
        void WaitPreviousFrameCommands();
        void WaitAllFrameCommands();
        void SubmitAndPresent(const SceneFrameRecordCallback &recordFrame,
                              const SceneFrameScreenshotCallback &saveScreenshot,
                              const SceneFrameSubmitLabels &labels = {});

        void SetScreenshotPath(std::string path);
        void QueueScreenshotReadback(CommandBuffer *cmd, Image *sourceImage, const std::string &stagingBufferName);
        bool SaveScreenshot(std::string *savedPath = nullptr);
        void DestroyScreenshotBuffer();

    private:
        // RT pipelines with hit groups for a new module list, built on a worker thread (SyncRTHitModules).
        struct RTHitBuild
        {
            std::shared_future<std::vector<std::shared_ptr<PassInfo>>> done;
            std::vector<uint32_t> modules;
            std::vector<RayTracingPass *> passes;
            std::vector<uint64_t> generations;
            std::shared_ptr<std::atomic<bool>> cancel;
        };

        void SyncRTHitModules();
        void StartRTHitBuild(const std::vector<RayTracingPass *> &passes, const std::vector<uint32_t> &modules);
        void FinishRTHitBuild();

        // The raster pipelines one MaterialX module lacks in some passes, built on a worker thread (SyncModulePipelines).
        struct ModuleBuild
        {
            struct Result
            {
                std::vector<std::shared_ptr<PassInfo>> infos; // the owners' in order, counts[i] each
                std::string error;
            };

            uint32_t module = 0;
            std::vector<ModulePipelines *> owners;
            std::vector<uint64_t> generations;
            std::vector<size_t> counts;
            std::shared_future<Result> done;
            std::shared_ptr<std::atomic<bool>> cancel;
        };

        // Each pass with module pipelines and the modules the scene wants from it.
        using ModuleOwners = std::vector<std::pair<ModulePipelines *, const std::vector<uint32_t> *>>;

        ModuleOwners GetModuleOwners() const;
        void SyncModulePipelines();
        void StartModuleBuild(const ModuleOwners &owners);
        void FinishModuleBuild(const ModuleOwners &owners);

        void InitEnabledRenderPassComponents(CommandBuffer *cmd);

        RenderGraph m_renderGraph;
        uint64_t m_renderedFrames = 0;
        uint64_t m_renderPassOrdersRevision = 0;
        RenderGraph m_deferredRenderGraph;
        Image *m_displayRT = nullptr;
        Image *m_frameDisplayOverride = nullptr;
        Image *m_viewportRT = nullptr;
        Image *m_depthStencil = nullptr;
        Image *m_screenshotRT = nullptr;
        bool m_screenshotPending = false;
        size_t m_screenshotRowPitch = 0;
        std::string m_screenshotPath;
        Buffer *m_screenshotBuffer = nullptr;
        Scene *m_scene = nullptr;
        OrderedMap<size_t, IRenderPassComponent *> m_renderPassComponents{};
        SceneRenderTargetMap m_renderTargets{};
        SceneRenderTargetMap m_depthStencilTargets{};
        std::vector<CommandBuffer *> m_cmds;
        std::vector<Semaphore *> m_acquireSemaphores;
        std::vector<Semaphore *> m_submitSemaphores;
        std::array<bool, kSceneRenderGraphPassCount> m_renderGraphPassEnabled{};
        std::array<bool, kSceneRenderGraphPassCount> m_renderGraphPassDeferred{};
        std::array<bool, kSceneRenderGraphPassCount> m_renderGraphPassInitialized{};
        std::array<uint32_t, kSceneRenderGraphPassCount> m_renderGraphPassLastEnabledFrame{}; // RHI frame counter
        SceneRenderGraphPassComponents m_scenePasses{};
        RTHitBuild m_rtHitBuild;
        ModuleBuild m_moduleBuild;
        std::map<uint32_t, uint64_t> m_failedModules; // module -> GenerationSum(owners) when its build failed
        float m_renderTargetScale = 0.0f;
        bool m_hdr = false;
        bool m_colorFormatChanged = false;
        bool m_openpbr = false; // SceneUsesOpenPBR() the passes were last built for

        SkyBox m_skyBox;
        Image *m_ibl_brdf_lut = nullptr;
    };
} // namespace pe
