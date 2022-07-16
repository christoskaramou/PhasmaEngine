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
    class CommandBuffer;
    class Descriptor;
    class Image;
    class Pipeline;
    class Buffer;
    class Camera;
    class PipelineCreateInfo;

    class SSAO : public IRenderComponent
    {
    public:
        SSAO();

        ~SSAO();

        void Init() override;

        void CreatePipeline() override;

        void CreateUniforms(CommandBuffer *cmd) override;

        void UpdateDescriptorSets() override;

        void Update(Camera *camera) override;

        void Draw(CommandBuffer *cmd, uint32_t imageIndex) override;

        void Resize(uint32_t width, uint32_t height) override;

        void Destroy() override;

    private:
        friend class Renderer;
        
        void CreateSSAOFrameBuffers();

        void CreateSSAOBlurFrameBuffers();

        void CreateSSAOPipeline();

        void CreateBlurPipeline();

    public:
        struct UBO
        {
            mat4 projection;
            mat4 view;
            mat4 invProjection;
        }ubo;
        Buffer *UB_Kernel;
        Buffer *UB_PVM;
        Image *noiseTex;
        Pipeline *pipeline;
        Pipeline *pipelineBlur;
        std::shared_ptr<PipelineCreateInfo> pipelineInfo;
        std::shared_ptr<PipelineCreateInfo> pipelineInfoBlur;
        Descriptor *DSet;
        Descriptor *DSBlur;
        Image *ssaoRT;
        Image *ssaoBlurRT;
        Image *normalRT;
        Image *depth;
    };
}