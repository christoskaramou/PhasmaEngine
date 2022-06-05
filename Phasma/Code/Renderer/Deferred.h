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

#include "Skybox/Skybox.h"
#include "Renderer/Shadows.h"

namespace pe
{
    class Descriptor;

    class FrameBuffer;

    class Image;

    class RenderPass;

    class Pipeline;

    class Deferred : public IRenderComponent
    {
    public:
        Deferred();

        ~Deferred();

        void Init() override;

        void CreateRenderPass() override;

        void CreateFrameBuffers() override;

        void CreatePipeline() override;

        void CreateUniforms() override;

        void UpdateDescriptorSets() override;

        void Update(Camera *camera) override;

        void Draw(CommandBuffer *cmd, uint32_t imageIndex) override;

        void Resize(uint32_t width, uint32_t height) override;

        void Destroy() override;

        void BeginPass(CommandBuffer *cmd, uint32_t imageIndex);

        void EndPass(CommandBuffer *cmd);

    private:
        void CreateGBufferFrameBuffers();

        void CreateCompositionFrameBuffers();

        void CreateGBufferPipeline();

        void CreateCompositionPipeline();

    public:
        struct UBO
        {
            vec4 screenSpace[8];
        } ubo;
        Buffer *uniform;
        RenderPass *renderPass;
        RenderPass *compositionRenderPass;
        std::vector<FrameBuffer *> framebuffers{}, compositionFramebuffers{};
        Descriptor *DSComposition;
        Pipeline *pipeline;
        Pipeline *pipelineComposition;
        Image *ibl_brdf_lut;
        Image *normalRT;
        Image *albedoRT;
        Image *srmRT;
        Image *velocityRT;
        Image *emissiveRT;
        Image *viewportRT;
        Image *depth;
        Image *ssaoBlurRT;
        Image *ssrRT;
    };
}