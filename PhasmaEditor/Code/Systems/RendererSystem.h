#pragma once

#include "API/RenderGraph.h"
#include "GUI/GUI.h"
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
    class ShadowPass;
    class DepthPass;
    class GbufferOpaquePass;
    class GbufferTransparentPass;
    class SSAOPass;
    class LightOpaquePass;
    class LightTransparentPass;
    class RayTracingPass;
    class ParticleComputePass;
    class ParticlePass;
    class SSRPass;
    class FXAAPass;
    class AabbsPass;
    class TAAPass;
    class SharpenPass;
    class TonemapPass;
    class BloomBrightFilterPass;
    class BloomGaussianBlurHorizontalPass;
    class BloomGaussianBlurVerticalPass;
    class DOFPass;
    class MotionBlurPass;
    class GridPass;

    class RendererSystem : public IDrawSystem
    {
    public:
        enum class RenderGraphPassId : uint32_t
        {
            Shadow = 0,
            Depth,
            GBufferOpaque,
            SSAO,
            LightOpaque,
            GBufferTransparent,
            LightTransparent,
            RayTracing,
            ParticleCompute,
            Particle,
            SSR,
            FXAA,
            Aabbs,
            TAA,
            Sharpen,
            Upsample,
            Tonemap,
            BloomBF,
            BloomH,
            BloomV,
            DOF,
            MotionBlur,
            Grid,
            Count
        };

        void Init(CommandBuffer *cmd) override;
        void Update() override;
        void Destroy() override;
        void Draw() override;
        void DrawPlatformWindows();

        Scene &GetScene() { return m_scene; }
        const SkyBox &GetSkyBoxDay() const { return m_skyBoxDay; }
        const SkyBox &GetSkyBoxNight() const { return m_skyBoxNight; }
        const SkyBox &GetSkyBoxWhite() const { return m_skyBoxWhite; }
        Image *GetIBL_LUT() const { return m_ibl_brdf_lut; }
        const GUI &GetGUI() const { return m_gui; }
        GUI &GetGUI() { return m_gui; }
        void ToggleGUI() { m_gui.ToggleRender(); }

        Image *CreateRenderTarget(const std::string &name,
                                  vk::Format format,
                                  vk::ImageUsageFlags usage = {},
                                  bool useRenderTergetScale = true,
                                  bool useMips = false,
                                  vec4 clearColor = Color::Transparent);
        Image *GetRenderTarget(const std::string &name);
        Image *GetRenderTarget(size_t hash);
        Image *CreateDepthStencilTarget(const std::string &name,
                                        vk::Format format,
                                        vk::ImageUsageFlags usage = {},
                                        bool useRenderTergetScale = true,
                                        float clearDepth = Color::Depth,
                                        uint32_t clearStencil = Color::Stencil);
        Image *GetDepthStencilTarget(const std::string &name);
        Image *GetDepthStencilTarget(size_t hash);
        Image *GetDisplayRT() { return m_displayRT; }
        Image *GetViewportRT() { return m_viewportRT; }
        Image *GetDepthStencilRT() { return m_depthStencil; }
        Image *CreateFSSampledImage(bool useRenderTergetScale = true);
        void Resize(uint32_t width, uint32_t height);
        void BlitToSwapchain(CommandBuffer *cmd, Image *renderedImage, uint32_t imageIndex);
        void PollShaders();
        void WaitPreviousFrameCommands();
        void WaitAllFramesCommands();
        void BuildRenderGraph();
        void UpdateRenderGraphPassStates();
        void CacheGlobalComponents();

    protected:
        void LoadResources(CommandBuffer *cmd);
        CommandBuffer *RecordPasses(uint32_t imageIndex);
        void Upsample(CommandBuffer *cmd, vk::Filter filter);
        void CreateRenderTargets();
        RenderGraph m_renderGraph;

        Image *m_displayRT;
        Image *m_viewportRT;
        Image *m_depthStencil;
        OrderedMap<size_t, IRenderPassComponent *> m_renderPassComponents{};
        std::unordered_map<size_t, Image *> m_renderTargets{};
        std::unordered_map<size_t, Image *> m_depthStencilTargets{};
        std::vector<CommandBuffer *> m_cmds;
        std::mutex m_binarySemaphoresMutex;
        std::vector<Semaphore *> m_acquireSemaphores;
        std::vector<Semaphore *> m_submitSemaphores;
        Scene m_scene;
        std::array<bool, static_cast<size_t>(RenderGraphPassId::Count)> m_renderGraphPassEnabled{};

        // Cached global component pointers (set by CacheGlobalComponents)
        ShadowPass *m_shadowPass = nullptr;
        DepthPass *m_depthPass = nullptr;
        GbufferOpaquePass *m_gbufferOpaquePass = nullptr;
        GbufferTransparentPass *m_gbufferTransparentPass = nullptr;
        SSAOPass *m_ssaoPass = nullptr;
        LightOpaquePass *m_lightOpaquePass = nullptr;
        LightTransparentPass *m_lightTransparentPass = nullptr;
        RayTracingPass *m_rayTracingPass = nullptr;
        ParticleComputePass *m_particleComputePass = nullptr;
        ParticlePass *m_particlePass = nullptr;
        SSRPass *m_ssrPass = nullptr;
        FXAAPass *m_fxaaPass = nullptr;
        AabbsPass *m_aabbsPass = nullptr;
        TAAPass *m_taaPass = nullptr;
        SharpenPass *m_sharpenPass = nullptr;
        TonemapPass *m_tonemapPass = nullptr;
        BloomBrightFilterPass *m_bloomBrightFilterPass = nullptr;
        BloomGaussianBlurHorizontalPass *m_bloomGaussianBlurHorizontalPass = nullptr;
        BloomGaussianBlurVerticalPass *m_bloomGaussianBlurVerticalPass = nullptr;
        DOFPass *m_dofPass = nullptr;
        MotionBlurPass *m_motionBlurPass = nullptr;
        GridPass *m_gridPass = nullptr;

        SkyBox m_skyBoxDay;
        SkyBox m_skyBoxNight;
        SkyBox m_skyBoxWhite;
        Image *m_ibl_brdf_lut;
        GUI m_gui;
    };
} // namespace pe
