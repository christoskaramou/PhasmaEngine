#pragma once

#include "API/RenderGraph.h"
#include "GUI/GUI.h"
#include "Render/SceneRenderGraph.h"
#include "Render/SceneRendererHost.h"
#include "Scene/Scene.h"
#include "Skybox/Skybox.h"

namespace pe
{
    class CommandBuffer;
    class Image;

    class Viewport
    {
    public:
        float x;
        float y;
        float width;
        float height;
        float minDepth;
        float maxDepth;
    };

    class GpuTimer;
    class Semaphore;

    class RendererSystem : public ISystem, public SceneRendererHost
    {
    public:
        enum class RenderGraphPassId : uint32_t
        {
            Culling = static_cast<uint32_t>(SceneRenderGraphPassId::Culling),
            Shadow = static_cast<uint32_t>(SceneRenderGraphPassId::Shadow),
            Depth = static_cast<uint32_t>(SceneRenderGraphPassId::Depth),
            GBufferOpaque = static_cast<uint32_t>(SceneRenderGraphPassId::GBufferOpaque),
            SSAO = static_cast<uint32_t>(SceneRenderGraphPassId::SSAO),
            LightOpaque = static_cast<uint32_t>(SceneRenderGraphPassId::LightOpaque),
            GBufferTransparent = static_cast<uint32_t>(SceneRenderGraphPassId::GBufferTransparent),
            LightTransparent = static_cast<uint32_t>(SceneRenderGraphPassId::LightTransparent),
            RayTracing = static_cast<uint32_t>(SceneRenderGraphPassId::RayTracing),
            ParticleCompute = static_cast<uint32_t>(SceneRenderGraphPassId::ParticleCompute),
            Particle = static_cast<uint32_t>(SceneRenderGraphPassId::Particle),
            SSR = static_cast<uint32_t>(SceneRenderGraphPassId::SSR),
            FXAA = static_cast<uint32_t>(SceneRenderGraphPassId::FXAA),
            Aabbs = static_cast<uint32_t>(SceneRenderGraphPassId::Aabbs),
            TAA = static_cast<uint32_t>(SceneRenderGraphPassId::TAA),
            Sharpen = static_cast<uint32_t>(SceneRenderGraphPassId::Sharpen),
            Upsample = static_cast<uint32_t>(SceneRenderGraphPassId::Upsample),
            Tonemap = static_cast<uint32_t>(SceneRenderGraphPassId::Tonemap),
            BloomBF = static_cast<uint32_t>(SceneRenderGraphPassId::BloomBF),
            BloomH = static_cast<uint32_t>(SceneRenderGraphPassId::BloomH),
            BloomV = static_cast<uint32_t>(SceneRenderGraphPassId::BloomV),
            DOF = static_cast<uint32_t>(SceneRenderGraphPassId::DOF),
            MotionBlur = static_cast<uint32_t>(SceneRenderGraphPassId::MotionBlur),
            Grid = static_cast<uint32_t>(SceneRenderGraphPassId::Grid),
            GUI = static_cast<uint32_t>(SceneRenderGraphPassId::Count),
            Count
        };

        void Init(CommandBuffer *cmd) override;
        void Update() override;
        void Destroy() override;
        void Draw();
        void DrawPlatformWindows();

        Scene &GetScene() override { return m_scene; }
        const SkyBox &GetSkyBoxDay() const override { return m_skyBoxDay; }
        const SkyBox &GetSkyBoxNight() const override { return m_skyBoxNight; }
        const SkyBox &GetSkyBoxWhite() const { return m_skyBoxWhite; }
        Image *GetIBL_LUT() const override { return m_ibl_brdf_lut; }
        const GUI &GetGUI() const { return m_gui; }
        GUI &GetGUI() { return m_gui; }
        void ToggleGUI() { m_gui.ToggleRender(); }

        Image *CreateRenderTarget(const std::string &name,
                                  ::PeFormat format,
                                  PeImageUsageFlags usage = PE_IMAGE_USAGE_NONE,
                                  bool useRenderTergetScale = true,
                                  bool useMips = false,
                                  vec4 clearColor = Color::Transparent);
        Image *GetRenderTarget(const std::string &name) override;
        Image *GetRenderTarget(size_t hash) override;
        Image *CreateDepthStencilTarget(const std::string &name,
                                        ::PeFormat format,
                                        PeImageUsageFlags usage = PE_IMAGE_USAGE_NONE,
                                        bool useRenderTergetScale = true,
                                        float clearDepth = Color::Depth,
                                        uint32_t clearStencil = Color::Stencil);
        Image *GetDepthStencilTarget(const std::string &name) override;
        Image *GetDepthStencilTarget(size_t hash) override;
        Image *GetDisplayRT() override { return m_displayRT; }
        Image *GetViewportRT() override { return m_viewportRT; }
        Image *GetDepthStencilRT() override { return m_depthStencil; }
        Image *CreateFSSampledImage(bool useRenderTergetScale = true) override;
        void Resize(uint32_t width, uint32_t height);
        void BlitToSwapchain(CommandBuffer *cmd, Image *renderedImage, uint32_t imageIndex);
        void PollShaders(std::optional<size_t> hash = std::nullopt);
        void WaitPreviousFrameCommands();
        void WaitAllFramesCommands();
        void BuildRenderGraph();
        void UpdateRenderGraphPassStates();
        void CacheGlobalComponents();

        void ResetTAAHistory();
        void SaveScreenshot();
        std::string GetScreenshotSavedPath()
        {
            std::string path = std::move(m_screenshotSavedPath);
            m_screenshotSavedPath.clear();
            return path;
        }

    protected:
        SceneRenderGraphPassComponents GetSceneRenderGraphPassComponents() const;
        void LoadResources(CommandBuffer *cmd);
        CommandBuffer *RecordPasses(uint32_t imageIndex);
        void CreateRenderTargets();
        Image *GetFrameOutputImage() const;
        void QueueScreenshotReadback(CommandBuffer *cmd, Image *sourceImage);
        RenderGraph m_renderGraph;

        Image *m_displayRT = nullptr;
        Image *m_viewportRT = nullptr;
        Image *m_depthStencil = nullptr;
        Image *m_screenshotRT = nullptr;
        bool m_screenshotPending = false;
        size_t m_screenshotRowPitch = 0;
        std::string m_screenshotPath;
        std::string m_screenshotSavedPath;
        Buffer *m_screenshotBuffer = nullptr;
        OrderedMap<size_t, IRenderPassComponent *> m_renderPassComponents{};
        std::unordered_map<size_t, Image *> m_renderTargets{};
        std::unordered_map<size_t, Image *> m_depthStencilTargets{};
        std::vector<CommandBuffer *> m_cmds;
        std::mutex m_binarySemaphoresMutex;
        std::vector<Semaphore *> m_acquireSemaphores;
        std::vector<Semaphore *> m_submitSemaphores;
        Scene m_scene;
        std::array<bool, static_cast<size_t>(RenderGraphPassId::Count)> m_renderGraphPassEnabled{};
        SceneRenderGraphPassComponents m_scenePasses{};

        SkyBox m_skyBoxDay;
        SkyBox m_skyBoxNight;
        SkyBox m_skyBoxWhite;
        Image *m_ibl_brdf_lut = nullptr;
        GUI m_gui;
    };
} // namespace pe
