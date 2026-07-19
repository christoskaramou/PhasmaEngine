#pragma once

#include "Render/SceneRendererCore.h"
#include "Render/SceneRendererHost.h"
#include "Scene/Scene.h"

namespace pe
{
    class CommandBuffer;
    class Image;
    class RuntimeUiSystem;

    class RuntimeSceneRenderer : public SceneRendererHost, public NoCopy, public NoMove
    {
    public:
        explicit RuntimeSceneRenderer(Scene &scene);
        ~RuntimeSceneRenderer();

        void Init(CommandBuffer *cmd);
        void Update();
        void Draw();
        void Resize(uint32_t width, uint32_t height, bool recreateSurface = false);
        void Destroy();
        void RequestScreenshot(std::string path = {});
        void PollShaders(std::optional<size_t> hash = std::nullopt);
        void WaitPreviousFrameCommands();
        void WaitAllFramesCommands();
        void SetRuntimeUi(RuntimeUiSystem *runtimeUi) { m_runtimeUi = runtimeUi; }

        Scene &GetScene() override { return m_scene; }
        const SkyBox &GetSkyBox() const override { return m_sceneRenderer.GetSkyBox(); }
        Image *GetIBL_LUT() const override { return m_sceneRenderer.GetIBL_LUT(); }
        void ReloadSkyFromSettings() override { m_sceneRenderer.ReloadSkyFromSettings(); }

        Image *GetRenderTarget(const std::string &name) override;
        Image *GetRenderTarget(size_t hash) override;
        bool DestroyRenderTarget(const std::string &name) override;
        Image *GetDepthStencilTarget(const std::string &name) override;
        Image *GetDepthStencilTarget(size_t hash) override;
        Image *GetDisplayRT() override { return m_sceneRenderer.GetDisplayRT(); }
        Image *GetViewportRT() override { return m_sceneRenderer.GetViewportRT(); }
        Image *GetDepthStencilRT() override { return m_sceneRenderer.GetDepthStencilRT(); }
        Image *CreateFSSampledImage(bool useRenderTergetScale = true) override;

    private:
        Image *CreateRenderTarget(const std::string &name,
                                  ::PeFormat format,
                                  PeImageUsageFlags usage = PE_IMAGE_USAGE_NONE,
                                  bool useRenderTergetScale = true,
                                  bool useMips = false,
                                  vec4 clearColor = Color::Transparent) override;
        Image *CreateDepthStencilTarget(const std::string &name,
                                        ::PeFormat format,
                                        PeImageUsageFlags usage = PE_IMAGE_USAGE_NONE,
                                        bool useRenderTergetScale = true,
                                        float clearDepth = Color::Depth,
                                        uint32_t clearStencil = Color::Stencil) override;
        void UpdateRenderGraphPassStates();
        void BuildRenderGraph();
        CommandBuffer *RecordPasses(uint32_t imageIndex);
        void QueueScreenshotReadback(CommandBuffer *cmd, Image *sourceImage);
        void ApplyRuntimeRenderSettings();
        void ApplyPendingRenderScaleResize();

        Scene &m_scene;
        SceneRendererCore m_sceneRenderer;
        RuntimeUiSystem *m_runtimeUi = nullptr;
        bool m_screenshotRequested = false;
        bool m_initialized = false;
        uint64_t m_scriptRenderPassesRevision = 0;
    };
} // namespace pe
