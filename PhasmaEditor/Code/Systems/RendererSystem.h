#pragma once

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

    class RendererSystem : public IDrawSystem
    {
    public:
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

    protected:
        void LoadResources(CommandBuffer *cmd);
        CommandBuffer *RecordPasses(uint32_t imageIndex);
        void Upsample(CommandBuffer *cmd, vk::Filter filter);
        void CreateRenderTargets();

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
        SkyBox m_skyBoxDay;
        SkyBox m_skyBoxNight;
        SkyBox m_skyBoxWhite;
        Image *m_ibl_brdf_lut;
        GUI m_gui;
    };
} // namespace pe
