#pragma once

#include "API/RenderGraph.h"
#include "Render/SceneRenderGraph.h"
#include "Render/SceneRendererHost.h"
#include "Scene/Scene.h"
#include "Skybox/Skybox.h"

#include <optional>

namespace pe
{
    class CommandBuffer;
    class Buffer;
    class Image;
    class RuntimeUiSystem;
    class Semaphore;

    class RuntimeSceneRenderer : public SceneRendererHost, public NoCopy, public NoMove
    {
    public:
        explicit RuntimeSceneRenderer(Scene &scene);
        ~RuntimeSceneRenderer();

        void Init(CommandBuffer *cmd);
        void Update();
        void Draw();
        void Resize(uint32_t width, uint32_t height);
        void Destroy();
        void RequestScreenshot(std::string path = {});
        void PollShaders(std::optional<size_t> hash = std::nullopt);
        void WaitPreviousFrameCommands();
        void WaitAllFramesCommands();
        void SetRuntimeUi(RuntimeUiSystem *runtimeUi) { m_runtimeUi = runtimeUi; }

        Scene &GetScene() override { return m_scene; }
        const SkyBox &GetSkyBoxDay() const override { return m_skyBoxDay; }
        const SkyBox &GetSkyBoxNight() const override { return m_skyBoxNight; }
        Image *GetIBL_LUT() const override { return m_ibl_brdf_lut; }

        Image *GetRenderTarget(const std::string &name) override;
        Image *GetRenderTarget(size_t hash) override;
        Image *GetDepthStencilTarget(const std::string &name) override;
        Image *GetDepthStencilTarget(size_t hash) override;
        Image *GetDisplayRT() override { return m_displayRT; }
        Image *GetViewportRT() override { return m_viewportRT; }
        Image *GetDepthStencilRT() override { return m_depthStencil; }
        Image *CreateFSSampledImage(bool useRenderTergetScale = true) override;

    private:
        using RenderGraphPassId = SceneRenderGraphPassId;

        Image *CreateRenderTarget(const std::string &name,
                                  ::PeFormat format,
                                  PeImageUsageFlags usage = PE_IMAGE_USAGE_NONE,
                                  bool useRenderTergetScale = true,
                                  bool useMips = false,
                                  vec4 clearColor = Color::Transparent);
        Image *CreateDepthStencilTarget(const std::string &name,
                                        ::PeFormat format,
                                        PeImageUsageFlags usage = PE_IMAGE_USAGE_NONE,
                                        bool useRenderTergetScale = true,
                                        float clearDepth = Color::Depth,
                                        uint32_t clearStencil = Color::Stencil);
        void LoadResources(CommandBuffer *cmd);
        void CreateRenderTargets();
        void CreateFrameResources(uint32_t imageCount);
        void DestroyFrameResources();
        void TransitionSwapchainImagesToPresent(CommandBuffer *cmd);
        void CacheGlobalComponents();
        SceneRenderGraphPassComponents GetSceneRenderGraphPassComponents() const;
        void UpdateRenderGraphPassStates();
        void BuildRenderGraph();
        CommandBuffer *RecordPasses(uint32_t imageIndex);
        void BlitToSwapchain(CommandBuffer *cmd, Image *src, uint32_t imageIndex);
        void QueueScreenshotReadback(CommandBuffer *cmd, Image *sourceImage);
        void SaveScreenshot();
        void ApplyRuntimeRenderSettings();

        Scene &m_scene;
        RenderGraph m_renderGraph;
        Image *m_displayRT = nullptr;
        Image *m_viewportRT = nullptr;
        Image *m_depthStencil = nullptr;
        Image *m_screenshotRT = nullptr;
        RuntimeUiSystem *m_runtimeUi = nullptr;
        bool m_screenshotRequested = false;
        bool m_screenshotPending = false;
        size_t m_screenshotRowPitch = 0;
        std::string m_screenshotPath;
        Buffer *m_screenshotBuffer = nullptr;
        OrderedMap<size_t, IRenderPassComponent *> m_renderPassComponents{};
        std::unordered_map<size_t, Image *> m_renderTargets{};
        std::unordered_map<size_t, Image *> m_depthStencilTargets{};
        std::vector<CommandBuffer *> m_cmds;
        std::mutex m_binarySemaphoresMutex;
        std::vector<Semaphore *> m_acquireSemaphores;
        std::vector<Semaphore *> m_submitSemaphores;
        std::array<bool, static_cast<size_t>(RenderGraphPassId::Count)> m_renderGraphPassEnabled{};
        SceneRenderGraphPassComponents m_scenePasses{};

        SkyBox m_skyBoxDay;
        SkyBox m_skyBoxNight;
        Image *m_ibl_brdf_lut = nullptr;
        bool m_initialized = false;
    };
} // namespace pe
