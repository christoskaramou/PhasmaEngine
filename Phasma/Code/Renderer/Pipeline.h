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

#include "Core/Base.h"
#include "Shader/Shader.h"
#include "Renderer/Vertex.h"

namespace pe
{
    class RenderPass;

    class DescriptorLayout;

    enum class CullMode
    {
        None = 0,
        Front = 1,
        Back = 2
    };

    enum class PushConstantStage
    {
        Vertex = 1,
        Fragment = 16,
        Compute = 32
    };

    class PipelineColorBlendAttachmentState
    {
    public:
        PipelineColorBlendAttachmentState();

        uint32_t blendEnable;
        BlendFactor srcColorBlendFactor;
        BlendFactor dstColorBlendFactor;
        BlendOp colorBlendOp;
        BlendFactor srcAlphaBlendFactor;
        BlendFactor dstAlphaBlendFactor;
        BlendOp alphaBlendOp;
        ColorComponentFlags colorWriteMask;
    };

    class PipelineCreateInfo
    {
    public:
        PipelineCreateInfo();

        ~PipelineCreateInfo();

        bool blendEnable;
        Shader *pVertShader;
        Shader *pFragShader;
        Shader *pCompShader;
        std::vector <VertexInputBindingDescription> vertexInputBindingDescriptions;
        std::vector <VertexInputAttributeDescription> vertexInputAttributeDescriptions;
        float width;
        float height;
        CullMode cullMode;
        std::vector <PipelineColorBlendAttachmentState> colorBlendAttachments;
        std::vector <DynamicState> dynamicStates;
        PushConstantStage pushConstantStage;
        uint32_t pushConstantSize;
        std::vector<DescriptorLayout *> descriptorSetLayouts;
        RenderPass *renderPass;
        PipelineCacheHandle pipelineCache;
    };

    class Pipeline : public IHandle<Pipeline, PipelineHandle>
    {
    public:
        Pipeline(const PipelineCreateInfo &info);

        ~Pipeline();

        PipelineLayoutHandle layout;

        void createComputePipeline();

        static DescriptorLayout *getDescriptorSetLayoutComposition();

        static DescriptorLayout *getDescriptorSetLayoutBrightFilter();

        static DescriptorLayout *getDescriptorSetLayoutGaussianBlurH();

        static DescriptorLayout *getDescriptorSetLayoutGaussianBlurV();

        static DescriptorLayout *getDescriptorSetLayoutCombine();

        static DescriptorLayout *getDescriptorSetLayoutDOF();

        static DescriptorLayout *getDescriptorSetLayoutFXAA();

        static DescriptorLayout *getDescriptorSetLayoutMotionBlur();

        static DescriptorLayout *getDescriptorSetLayoutSSAO();

        static DescriptorLayout *getDescriptorSetLayoutSSAOBlur();

        static DescriptorLayout *getDescriptorSetLayoutSSR();

        static DescriptorLayout *getDescriptorSetLayoutTAA();

        static DescriptorLayout *getDescriptorSetLayoutTAASharpen();

        static DescriptorLayout *getDescriptorSetLayoutShadows();

        static DescriptorLayout *getDescriptorSetLayoutShadowsDeferred();

        static DescriptorLayout *getDescriptorSetLayoutMesh();

        static DescriptorLayout *getDescriptorSetLayoutPrimitive();

        static DescriptorLayout *getDescriptorSetLayoutModel();

        static DescriptorLayout *getDescriptorSetLayoutSkybox();

        static DescriptorLayout *getDescriptorSetLayoutCompute();

    private:
        PipelineCreateInfo info;
    };
}