/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

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

        Image *CreateRenderTarget(const std::string &name, Format format, bool blendEnable, ImageUsageFlags additionalFlags = {});

        Image *GetRenderTarget(const std::string &name);

        Image *GetRenderTarget(size_t hash);

        Image *CreateDepthTarget(const std::string &name, Format format, ImageUsageFlags additionalFlags = {});

        Image *GetDepthTarget(const std::string &name);

        Image *GetDepthTarget(size_t hash);

        Image *CreateFSSampledImage();

        void LoadResources(CommandBuffer *cmd);

        void CreateUniforms();

        void Resize(uint32_t width, uint32_t height);

        void BlitToSwapchain(CommandBuffer *cmd, Image *renderedImage, uint32_t imageIndex);

        void RecreatePipelines();

    protected:
        static void CheckModelsQueue();

        void ComputeAnimations();

        void RecordPasses(CommandBuffer *cmd, uint32_t imageIndex);

        void RecordShadowsCmds(uint32_t count, CommandBuffer **cmds, uint32_t imageIndex);

        Image *m_viewportRT;
        Image *m_depth;
        std::unordered_map<size_t, IRenderComponent *> m_renderComponents{};
        std::map<size_t, Image *> m_renderTargets{};
        std::map<size_t, Image *> m_depthTargets{};

    public:
        inline static Queue *s_currentQueue = nullptr;
        std::vector<GpuTimer *> gpuTimers{};
        RenderArea renderArea;
        SkyBox skyBoxDay;
        SkyBox skyBoxNight;
        GUI gui;

#ifndef IGNORE_SCRIPTS
        std::vector<Script *> scripts{};
#endif
    };
}