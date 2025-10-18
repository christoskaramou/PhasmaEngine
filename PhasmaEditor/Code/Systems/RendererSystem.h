#pragma once

#include "Skybox/Skybox.h"
#include "GUI/GUI.h"
#include "Script/Script.h"
#include "Scene/Scene.h"

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

    class RenderArea
    {
    public:
        Viewport viewport;
        Rect2Di scissor;

        void Update(float x, float y, float w, float h, float minDepth = 0.f, float maxDepth = 1.f);
    };

    class GpuTimer;

    class RendererSystem : public IDrawSystem
    {
    public:
        void Init(CommandBuffer *cmd) override;
        void Update() override;
        void Destroy() override;
        void Draw() override;

        Scene &GetScene() { return m_scene; }
        const SkyBox &GetSkyBoxDay() const { return m_skyBoxDay; }
        const SkyBox &GetSkyBoxNight() const { return m_skyBoxNight; }
        const GUI &GetGUI() const { return m_gui; }
        void ToggleGUI() { m_gui.ToggleRender(); }

        RenderArea &GetRenderArea() { return m_renderArea; }
        Image *CreateRenderTarget(const std::string &name,
                                  vk::Format format,
                                  vk::ImageUsageFlags additionalFlags = {},
                                  bool useRenderTergetScale = true,
                                  bool useMips = false,
                                  vec4 clearColor = Color::Transparent);
        Image *GetRenderTarget(const std::string &name);
        Image *GetRenderTarget(size_t hash);
        Image *CreateDepthStencilTarget(const std::string &name,
                                        vk::Format format,
                                        vk::ImageUsageFlags additionalFlags = {},
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

    protected:
        void WaitPreviousFrameCommands();
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
        RenderArea m_renderArea;
        Scene m_scene;
        SkyBox m_skyBoxDay;
        SkyBox m_skyBoxNight;
        GUI m_gui;
    };
}
