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

    class SSR : public IEffectComponent
    {
    public:
        SSR();

        ~SSR();

        void Init() override;

        void CreateRenderPass(std::map<std::string, Image *> &renderTargets) override;

        void CreateFrameBuffers(std::map<std::string, Image *> &renderTargets) override;

        void CreatePipeline(std::map<std::string, Image *> &renderTargets) override;

        void CreateUniforms(std::map<std::string, Image *> &renderTargets) override;

        void UpdateDescriptorSets(std::map<std::string, Image *> &renderTargets) override;

        void Update(Camera *camera) override;

        void Draw(CommandBuffer *cmd, uint32_t imageIndex, std::map<std::string, Image *> &renderTargets) override;

        void Destroy() override;

        mat4 reflectionInput[4];
        Buffer *UBReflection;
        std::vector<FrameBuffer *> framebuffers{};
        Pipeline *pipeline;
        RenderPass *renderPass;
        Descriptor *DSet;
    };
}