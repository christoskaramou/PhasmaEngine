#pragma once

#include "Renderer/Surface.h"
#include "Renderer/Swapchain.h"
#include "GUI/GUI.h"
#include "Renderer/Skybox.h"
#include "Renderer/RenderPasses/ShadowPass.h"
#include "Scene/Model.h"
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
        Rect2D scissor;

        void Update(float x, float y, float w, float h, float minDepth = 0.f, float maxDepth = 1.f);
    };

    class GpuTimer;

    class Renderer
    {
    public:
        Renderer();

        virtual ~Renderer();

        RenderArea &GetRenderArea() { return m_renderArea; }

        Image *CreateRenderTarget(const std::string &name,
                                  Format format,
                                  bool blendEnable,
                                  ImageUsageFlags additionalFlags = {},
                                  bool useRenderTergetScale = true,
                                  bool useMips = false,
                                  vec4 clearColor = Color::Transparent);
        Image *GetRenderTarget(const std::string &name);
        Image *GetRenderTarget(size_t hash);

        Image *CreateDepthStencilTarget(const std::string &name,
                                        Format format,
                                        ImageUsageFlags additionalFlags = {},
                                        bool useRenderTergetScale = true,
                                        float clearDepth = Color::Depth,
                                        uint32_t clearStencil = Color::Stencil);
        Image *GetDepthStencilTarget(const std::string &name);
        Image *GetDepthStencilTarget(size_t hash);

        Image *GetDisplayRT() { return m_displayRT; }
        Image *GetViewportRT() { return m_viewportRT; }
        Image *GetDepthStencilRT() { return m_depthStencil; }

        Image *CreateFSSampledImage(bool useRenderTergetScale = true);

        void LoadResources(CommandBuffer *cmd);

        void CreateUniforms();

        void Resize(uint32_t width, uint32_t height);

        CommandBuffer *BlitToSwapchain(CommandBuffer *cmd, Image *renderedImage, uint32_t imageIndex);

        void PollShaders();

        Scene &GetScene() { return m_scene; }

        const SkyBox &GetSkyBoxDay() const { return m_skyBoxDay; }

        const SkyBox &GetSkyBoxNight() const { return m_skyBoxNight; }

        const GUI &GetGUI() const { return m_gui; }

        void ToggleGUI() { m_gui.render = !m_gui.render; }

    protected:
        static void CheckModelsQueue();

        void ComputeAnimations();

        CommandBuffer *Upsample(Filter filter);

        void CreateRenderTargets();

        std::vector<CommandBuffer *> RecordPasses(uint32_t imageIndex);

        Image *m_displayRT;
        Image *m_viewportRT;
        Image *m_depthStencil;
        OrderedMap<size_t, IRenderPassComponent *> m_renderPassComponents{};
        std::unordered_map<size_t, Image *> m_renderTargets{};
        std::unordered_map<size_t, Image *> m_depthStencilTargets{};

        Scene m_scene;
        std::vector<CommandBuffer *> m_cmds[SWAPCHAIN_IMAGES];

        RenderArea m_renderArea;
        SkyBox m_skyBoxDay;
        SkyBox m_skyBoxNight;
        GUI m_gui;

#if PE_SCRIPTS
        std::vector<Script *> scripts{};
#endif
    };
}