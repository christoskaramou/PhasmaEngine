#pragma once

#include "Renderer/Surface.h"
#include "Renderer/Swapchain.h"
#include "GUI/GUI.h"
#include "Skybox/Skybox.h"
#include "Renderer/Shadows.h"
#include "Model/Model.h"
#include "Renderer/Deferred.h"
#include "Renderer/Compute.h"
#include "Script/Script.h"
#include "Script/Script.h"

#define IGNORE_SCRIPTS

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

        RenderArea &GetRenderArea() { return renderArea; }

        Image *CreateRenderTarget(const std::string &name,
                                  Format format,
                                  bool blendEnable,
                                  ImageUsageFlags additionalFlags = {},
                                  bool useRenderTergetScale = true);

        Image *GetRenderTarget(const std::string &name);

        Image *GetRenderTarget(size_t hash);

        Image *CreateDepthTarget(const std::string &name,
                                 Format format,
                                 ImageUsageFlags additionalFlags = {},
                                 bool useRenderTergetScale = true);

        Image *GetDepthTarget(const std::string &name);

        Image *GetDepthTarget(size_t hash);

        Image *CreateFSSampledImage(bool useRenderTergetScale = true);

        void LoadResources(CommandBuffer *cmd);

        void CreateUniforms();

        void Resize(uint32_t width, uint32_t height);

        void BlitToSwapchain(CommandBuffer *cmd, Image *renderedImage, uint32_t imageIndex);

        void PollShaders();

    protected:
        static void CheckModelsQueue();

        void ComputeAnimations();

        void RecordPasses(CommandBuffer *cmd, uint32_t imageIndex);

        void RecordShadowsCmds(uint32_t count, CommandBuffer **cmds, uint32_t imageIndex);

        Image *m_displayRT;
        Image *m_viewportRT;
        Image *m_depth;
        std::unordered_map<size_t, IRenderComponent *> m_renderComponents{};
        std::map<size_t, Image *> m_renderTargets{};
        std::map<size_t, Image *> m_depthTargets{};

    public:
        inline static Queue *s_currentQueue = nullptr;
        RenderArea renderArea;
        SkyBox skyBoxDay;
        SkyBox skyBoxNight;
        GUI gui;

    protected:
        CommandBuffer *m_previousCmds[SWAPCHAIN_IMAGES];
        CommandBuffer *m_previousShadowCmds[SWAPCHAIN_IMAGES][SHADOWMAP_CASCADES];

#ifndef IGNORE_SCRIPTS
        std::vector<Script *> scripts{};
#endif
    };
}