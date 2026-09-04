#pragma once

#include "GUI/GUI.h"
#include "Render/SceneRendererCore.h"
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
            ForwardPlusLightCulling = static_cast<uint32_t>(SceneRenderGraphPassId::ForwardPlusLightCulling),
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
            SelectionOutline = static_cast<uint32_t>(SceneRenderGraphPassId::SelectionOutline),
            GUI = static_cast<uint32_t>(SceneRenderGraphPassId::Count),
            Count
        };

        void Init(CommandBuffer *cmd) override;
        void Update() override;
        void Destroy() override;
        static void RefreshWindowTitle();
        void Draw();
        void DrawPlatformWindows();
        void LateCatchUpForScriptMutations();

        Scene &GetScene() override { return m_scene; }
        const SkyBox &GetSkyBox() const override { return m_sceneRenderer.GetSkyBox(); }
        const SkyBox &GetSkyBoxWhite() const { return m_skyBoxWhite; }
        Image *GetIBL_LUT() const override { return m_sceneRenderer.GetIBL_LUT(); }
        void ReloadSkyFromSettings() override { m_sceneRenderer.ReloadSkyFromSettings(); }
        const GUI &GetGUI() const { return m_gui; }
        GUI &GetGUI() { return m_gui; }
        void ToggleGUI() { m_gui.ToggleRender(); }

        Image *CreateRenderTarget(const std::string &name,
                                  ::PeFormat format,
                                  PeImageUsageFlags usage = PE_IMAGE_USAGE_NONE,
                                  bool useRenderTergetScale = true,
                                  bool useMips = false,
                                  vec4 clearColor = Color::Transparent) override;
        Image *GetRenderTarget(const std::string &name) override;
        Image *GetRenderTarget(size_t hash) override;
        bool DestroyRenderTarget(const std::string &name) override;
        Image *CreateDepthStencilTarget(const std::string &name,
                                        ::PeFormat format,
                                        PeImageUsageFlags usage = PE_IMAGE_USAGE_NONE,
                                        bool useRenderTergetScale = true,
                                        float clearDepth = Color::Depth,
                                        uint32_t clearStencil = Color::Stencil) override;
        Image *GetDepthStencilTarget(const std::string &name) override;
        Image *GetDepthStencilTarget(size_t hash) override;
        Image *GetDisplayRT() override { return m_sceneRenderer.GetDisplayRT(); }
        Image *GetViewportRT() override { return m_sceneRenderer.GetViewportRT(); }
        Image *GetDepthStencilRT() override { return m_sceneRenderer.GetDepthStencilRT(); }
        Image *CreateFSSampledImage(bool useRenderTergetScale = true) override;
        void Resize(uint32_t width, uint32_t height);
        void PollShaders(std::optional<size_t> hash = std::nullopt);
        void WaitPreviousFrameCommands();
        void WaitAllFramesCommands();
        void BuildRenderGraph();
        void UpdateRenderGraphPassStates();

        void ResetTAAHistory();
        std::string GetScreenshotSavedPath()
        {
            std::string path = std::move(m_screenshotSavedPath);
            m_screenshotSavedPath.clear();
            return path;
        }

    protected:
        CommandBuffer *RecordPasses(uint32_t imageIndex);
        Image *GetFrameOutputImage() const;
        void QueueScreenshotReadback(CommandBuffer *cmd, Image *sourceImage);
        void ApplyPendingRenderScaleResize();
        void ApplyPendingOptionalRTSync();
        std::string m_screenshotSavedPath;
        Scene m_scene;
        SceneRendererCore m_sceneRenderer;
        bool m_guiPassEnabled = false;
        uint64_t m_scriptRenderPassesRevision = 0;

        SkyBox m_skyBoxWhite;
        GUI m_gui;
    };
} // namespace pe
