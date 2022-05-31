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

namespace pe
{
    class Descriptor;
    class FrameBuffer;
    class Image;
    class RenderPass;
    class Pipeline;
    class Buffer;
    class Camera;

    class TAA : public IRenderComponent
    {
    public:
        TAA();

        ~TAA();

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

    private:
        void CreateTAAPipeline();

        void CreatePipelineSharpen();

        void SaveImage(CommandBuffer *cmd, Image *source);

    public:
        std::vector<FrameBuffer *> framebuffers{}, framebuffersSharpen{};
        Pipeline *pipeline;
        Pipeline *pipelineSharpen;
        RenderPass *renderPass;
        RenderPass *renderPassSharpen;
        Descriptor *DSet;
        Descriptor *DSetSharpen;
        Buffer *uniform;
        Image *previous;
        Image *frameImage;
        Image *taaRT;
        Image *viewportRT;
        Image *velocityRT;
        Image *depth;

        struct UBO
        {
            vec4 values;
            vec4 sharpenValues;
            mat4 invProj;
        } ubo;
    };
}
