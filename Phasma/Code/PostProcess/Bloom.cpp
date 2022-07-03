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

#include "Bloom.h"
#include "GUI/GUI.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Surface.h"
#include "Shader/Shader.h"
#include "Renderer/RHI.h"
#include "Renderer/Command.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Image.h"
#include "Renderer/Pipeline.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    Bloom::Bloom()
    {
    }

    Bloom::~Bloom()
    {
    }

    void Bloom::Init()
    {
        RendererSystem *rs = CONTEXT->GetSystem<RendererSystem>();

        brightFilterRT = rs->GetRenderTarget("brightFilter");
        gaussianBlurHorizontalRT = rs->GetRenderTarget("gaussianBlurHorizontal");
        gaussianBlurVerticalRT = rs->GetRenderTarget("gaussianBlurVertical");
        viewportRT = rs->GetRenderTarget("viewport");
        frameImage = rs->CreateFSSampledImage();
    }

    void Bloom::CreatePipeline()
    {
        CreateBrightFilterPipeline();
        CreateGaussianBlurHorizontaPipeline();
        CreateGaussianBlurVerticalPipeline();
        CreateCombinePipeline();
    }

    void Bloom::CreateBrightFilterPipeline()
    {
        PipelineCreateInfo info{};
        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.vert", ShaderStage::VertexBit});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/Bloom/brightFilter.frag", ShaderStage::FragmentBit});
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {brightFilterRT->blendAttachment};
        info.pushConstantStage = ShaderStage::FragmentBit;
        info.pushConstantSize = 5 * sizeof(float);
        info.descriptorSetLayouts = {DSBrightFilter->GetLayout()};
        info.dynamicColorTargets = 1;
        info.colorFormats = &brightFilterRT->imageInfo.format;
        info.name = "BrightFilter_pipeline";

        pipelineBrightFilter = Pipeline::Create(info);

        Shader::Destroy(info.pVertShader);
        Shader::Destroy(info.pFragShader);
    }

    void Bloom::CreateGaussianBlurHorizontaPipeline()
    {
        PipelineCreateInfo info{};
        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.vert", ShaderStage::VertexBit});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/Bloom/gaussianBlurHorizontal.frag", ShaderStage::FragmentBit});
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {gaussianBlurHorizontalRT->blendAttachment};
        info.pushConstantStage = ShaderStage::FragmentBit;
        info.pushConstantSize = 5 * sizeof(float);
        info.descriptorSetLayouts = {DSGaussianBlurHorizontal->GetLayout()};
        info.dynamicColorTargets = 1;
        info.colorFormats = &gaussianBlurHorizontalRT->imageInfo.format;
        info.name = "GaussianBlurHorizontal_pipeline";

        pipelineGaussianBlurHorizontal = Pipeline::Create(info);

        Shader::Destroy(info.pVertShader);
        Shader::Destroy(info.pFragShader);
    }

    void Bloom::CreateGaussianBlurVerticalPipeline()
    {
        PipelineCreateInfo info{};
        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.vert", ShaderStage::VertexBit});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/Bloom/gaussianBlurVertical.frag", ShaderStage::FragmentBit});
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {gaussianBlurVerticalRT->blendAttachment};
        info.pushConstantStage = ShaderStage::FragmentBit;
        info.pushConstantSize = 5 * sizeof(float);
        info.descriptorSetLayouts = {DSGaussianBlurVertical->GetLayout()};
        info.dynamicColorTargets = 1;
        info.colorFormats = &gaussianBlurVerticalRT->imageInfo.format;
        info.name = "GaussianBlurVertical_pipeline";

        pipelineGaussianBlurVertical = Pipeline::Create(info);

        Shader::Destroy(info.pVertShader);
        Shader::Destroy(info.pFragShader);
    }

    void Bloom::CreateCombinePipeline()
    {
        PipelineCreateInfo info{};
        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.vert", ShaderStage::VertexBit});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/Bloom/combine.frag", ShaderStage::FragmentBit});
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {viewportRT->blendAttachment};
        info.pushConstantStage = ShaderStage::FragmentBit;
        info.pushConstantSize = 5 * sizeof(float);
        info.descriptorSetLayouts = {DSCombine->GetLayout()};
        info.dynamicColorTargets = 1;
        info.colorFormats = &viewportRT->imageInfo.format;
        info.name = "BloomCombine_pipeline";

        pipelineCombine = Pipeline::Create(info);

        Shader::Destroy(info.pVertShader);
        Shader::Destroy(info.pFragShader);
    }

    void Bloom::CreateUniforms(CommandBuffer *cmd)
    {
        DescriptorBindingInfo bindingInfos[2]{};
        bindingInfos[0].binding = 0;
        bindingInfos[0].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[0].type = DescriptorType::CombinedImageSampler;
        bindingInfos[1].binding = 1;
        bindingInfos[1].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[1].type = DescriptorType::CombinedImageSampler;

        DescriptorInfo info{};
        info.count = 1;
        info.bindingInfos = bindingInfos;
        info.stage = ShaderStage::FragmentBit;

        bindingInfos[0].pImage = frameImage;
        DSBrightFilter = Descriptor::Create(&info, "Bloom_BrightFilter_descriptor");

        bindingInfos[0].pImage = brightFilterRT;
        DSGaussianBlurHorizontal = Descriptor::Create(&info, "Bloom_GaussianBlurHorizontal_descriptor");

        bindingInfos[0].pImage = gaussianBlurHorizontalRT;
        DSGaussianBlurVertical = Descriptor::Create(&info, "Bloom_GaussianBlurVertical_descriptor");

        bindingInfos[0].pImage = frameImage;
        bindingInfos[1].pImage = gaussianBlurVerticalRT;
        info.count = 2;
        DSCombine = Descriptor::Create(&info, "Bloom_Combine_descriptor");
    }

    void Bloom::UpdateDescriptorSets()
    {
        DescriptorBindingInfo bindingInfos[2]{};
        bindingInfos[0].binding = 0;
        bindingInfos[0].type = DescriptorType::CombinedImageSampler;
        bindingInfos[0].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[1].binding = 1;
        bindingInfos[1].type = DescriptorType::CombinedImageSampler;
        bindingInfos[1].imageLayout = ImageLayout::ShaderReadOnly;

        DescriptorInfo info{};
        info.count = 1;
        info.bindingInfos = bindingInfos;
        info.stage = ShaderStage::FragmentBit;

        bindingInfos[0].pImage = frameImage;
        DSBrightFilter->UpdateDescriptor(&info);

        bindingInfos[0].pImage = brightFilterRT;
        DSGaussianBlurHorizontal->UpdateDescriptor(&info);

        bindingInfos[0].pImage = gaussianBlurHorizontalRT;
        DSGaussianBlurVertical->UpdateDescriptor(&info);

        bindingInfos[0].pImage = frameImage;
        bindingInfos[1].pImage = gaussianBlurVerticalRT;
        info.count = 2;
        DSCombine->UpdateDescriptor(&info);
    }

    void Bloom::Update(Camera *camera)
    {
    }

    void Bloom::Draw(CommandBuffer *cmd, uint32_t imageIndex)
    {
        uint32_t totalImages = static_cast<uint32_t>(SWAPCHAIN_IMAGES);

        std::vector<float> values{
            GUI::Bloom_Inv_brightness,
            GUI::Bloom_intensity,
            GUI::Bloom_range,
            GUI::Bloom_exposure,
            static_cast<float>(GUI::use_tonemap)};

        cmd->BeginDebugRegion("Bloom");

        cmd->BeginDebugRegion("BrightFilter");
        // Copy viewport image
        cmd->CopyImage(viewportRT, frameImage);

        // BRIGHT FILTER
        // Input
        cmd->ImageBarrier(frameImage, ImageLayout::ShaderReadOnly);
        // Output
        cmd->ImageBarrier(brightFilterRT, ImageLayout::ColorAttachment);

        AttachmentInfo info{};
        info.image = brightFilterRT;

        cmd->BeginPass(1, &info);
        cmd->SetViewport(0.f, 0.f, brightFilterRT->width_f, brightFilterRT->height_f);
        cmd->SetScissor(0, 0, brightFilterRT->imageInfo.width, brightFilterRT->imageInfo.height);
        cmd->PushConstants(pipelineBrightFilter, ShaderStage::FragmentBit, 0,
                           uint32_t(sizeof(float) * values.size()), values.data());
        cmd->BindPipeline(pipelineBrightFilter);
        cmd->BindDescriptors(pipelineBrightFilter, 1, &DSBrightFilter);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
        cmd->EndDebugRegion();

        cmd->BeginDebugRegion("GaussianBlurHorizontal");
        // GUASSIAN BLUR HORIZONTAL
        // Input
        cmd->ImageBarrier(brightFilterRT, ImageLayout::ShaderReadOnly);
        // Output
        cmd->ImageBarrier(gaussianBlurHorizontalRT, ImageLayout::ColorAttachment);

        info.image = gaussianBlurHorizontalRT;
        cmd->BeginPass(1, &info);
        cmd->SetViewport(0.f, 0.f, gaussianBlurHorizontalRT->width_f, gaussianBlurHorizontalRT->height_f);
        cmd->SetScissor(0, 0, gaussianBlurHorizontalRT->imageInfo.width, gaussianBlurHorizontalRT->imageInfo.height);
        cmd->PushConstants(pipelineGaussianBlurHorizontal, ShaderStage::FragmentBit, 0,
                           uint32_t(sizeof(float) * values.size()), values.data());
        cmd->BindPipeline(pipelineGaussianBlurHorizontal);
        cmd->BindDescriptors(pipelineGaussianBlurHorizontal, 1, &DSGaussianBlurHorizontal);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
        cmd->EndDebugRegion();

        cmd->BeginDebugRegion("GaussianBlurVertical");
        // GAUSSIAN BLUR VERTICAL
        // Input
        cmd->ImageBarrier(gaussianBlurHorizontalRT, ImageLayout::ShaderReadOnly);
        // Output
        cmd->ImageBarrier(gaussianBlurVerticalRT, ImageLayout::ColorAttachment);

        info.image = gaussianBlurVerticalRT;
        cmd->BeginPass(1, &info);
        cmd->SetViewport(0.f, 0.f, gaussianBlurVerticalRT->width_f, gaussianBlurVerticalRT->height_f);
        cmd->SetScissor(0, 0, gaussianBlurVerticalRT->imageInfo.width, gaussianBlurVerticalRT->imageInfo.height);
        cmd->PushConstants(pipelineGaussianBlurVertical, ShaderStage::FragmentBit, 0,
                           uint32_t(sizeof(float) * values.size()), values.data());
        cmd->BindPipeline(pipelineGaussianBlurVertical);
        cmd->BindDescriptors(pipelineGaussianBlurVertical, 1, &DSGaussianBlurVertical);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
        cmd->EndDebugRegion();

        cmd->BeginDebugRegion("Bloom Combine");
        // COMBINE
        // Input
        cmd->ImageBarrier(frameImage, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(gaussianBlurVerticalRT, ImageLayout::ShaderReadOnly);
        // Output
        cmd->ImageBarrier(viewportRT, ImageLayout::ColorAttachment);

        info.image = viewportRT;
        cmd->BeginPass(1, &info);
        cmd->SetViewport(0.f, 0.f, viewportRT->width_f, viewportRT->height_f);
        cmd->SetScissor(0, 0, viewportRT->imageInfo.width, viewportRT->imageInfo.height);
        cmd->PushConstants(pipelineCombine, ShaderStage::FragmentBit, 0, uint32_t(sizeof(float) * values.size()),
                           values.data());
        cmd->BindPipeline(pipelineCombine);
        cmd->BindDescriptors(pipelineCombine, 1, &DSCombine);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
        cmd->EndDebugRegion();

        cmd->EndDebugRegion();
    }

    void Bloom::Resize(uint32_t width, uint32_t height)
    {
        Image::Destroy(frameImage);
        Init();
        UpdateDescriptorSets();
    }

    void Bloom::Destroy()
    {
        Descriptor::Destroy(DSBrightFilter);
        Descriptor::Destroy(DSGaussianBlurHorizontal);
        Descriptor::Destroy(DSGaussianBlurVertical);
        Descriptor::Destroy(DSCombine);

        Pipeline::Destroy(pipelineBrightFilter);
        Pipeline::Destroy(pipelineGaussianBlurHorizontal);
        Pipeline::Destroy(pipelineGaussianBlurVertical);
        Pipeline::Destroy(pipelineCombine);

        Image::Destroy(frameImage);
    }
}
