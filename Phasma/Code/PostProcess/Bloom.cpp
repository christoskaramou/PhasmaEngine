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
#include "Renderer/RenderPass.h"
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

    void Bloom::CreateRenderPass()
    {
        Attachment attachment{};
        attachment.format = brightFilterRT->imageInfo.format;
        renderPassBrightFilter = RenderPass::Create(&attachment, 1, "brightFilter_renderpass");

        attachment.format = gaussianBlurHorizontalRT->imageInfo.format;
        renderPassGaussianBlur = RenderPass::Create(&attachment, 1, "gaussianBlur_renderpass");

        attachment.format = viewportRT->imageInfo.format;
        renderPassCombine = RenderPass::Create(&attachment, 1, "bloomCombine_renderpass");
    }

    void Bloom::CreateFrameBuffers()
    {
        framebuffers.resize(SWAPCHAIN_IMAGES * 4);
        for (size_t i = 0; i < SWAPCHAIN_IMAGES; ++i)
        {
            uint32_t width = brightFilterRT->imageInfo.width;
            uint32_t height = brightFilterRT->imageInfo.height;
            ImageViewHandle view = brightFilterRT->view;
            framebuffers[i] = FrameBuffer::Create(width, height, &view, 1, renderPassBrightFilter, "brightFilter_frameBuffer_" + std::to_string(i));
        }

        for (size_t i = SWAPCHAIN_IMAGES; i < SWAPCHAIN_IMAGES * 2; ++i)
        {
            uint32_t width = gaussianBlurHorizontalRT->imageInfo.width;
            uint32_t height = gaussianBlurHorizontalRT->imageInfo.height;
            ImageViewHandle view = gaussianBlurHorizontalRT->view;
            framebuffers[i] = FrameBuffer::Create(width, height, &view, 1, renderPassGaussianBlur, "gaussianBlurHorizontal_frameBuffer_" + std::to_string(i));
        }

        for (size_t i = SWAPCHAIN_IMAGES * 2; i < SWAPCHAIN_IMAGES * 3; ++i)
        {
            uint32_t width = gaussianBlurVerticalRT->imageInfo.width;
            uint32_t height = gaussianBlurVerticalRT->imageInfo.height;
            ImageViewHandle view = gaussianBlurVerticalRT->view;
            framebuffers[i] = FrameBuffer::Create(width, height, &view, 1, renderPassGaussianBlur, "gaussianBlurVertical_frameBuffer_" + std::to_string(i));
        }

        for (size_t i = SWAPCHAIN_IMAGES * 3; i < SWAPCHAIN_IMAGES * 4; ++i)
        {
            uint32_t width = viewportRT->imageInfo.width;
            uint32_t height = viewportRT->imageInfo.height;
            ImageViewHandle view = viewportRT->view;
            framebuffers[i] = FrameBuffer::Create(width, height, &view, 1, renderPassCombine, "bloomCombine_frameBuffer_" + std::to_string(i));
        }
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
        info.width = brightFilterRT->width_f;
        info.height = brightFilterRT->height_f;
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {brightFilterRT->blendAttachment};
        info.pushConstantStage = ShaderStage::FragmentBit;
        info.pushConstantSize = 5 * sizeof(float);
        info.descriptorSetLayouts = {DSBrightFilter->GetLayout()};
        info.renderPass = renderPassBrightFilter;
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
        info.width = gaussianBlurHorizontalRT->width_f;
        info.height = gaussianBlurHorizontalRT->height_f;
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {gaussianBlurHorizontalRT->blendAttachment};
        info.pushConstantStage = ShaderStage::FragmentBit;
        info.pushConstantSize = 5 * sizeof(float);
        info.descriptorSetLayouts = {DSGaussianBlurHorizontal->GetLayout()};
        info.renderPass = renderPassGaussianBlur;
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
        info.width = gaussianBlurVerticalRT->width_f;
        info.height = gaussianBlurVerticalRT->height_f;
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {gaussianBlurVerticalRT->blendAttachment};
        info.pushConstantStage = ShaderStage::FragmentBit;
        info.pushConstantSize = 5 * sizeof(float);
        info.descriptorSetLayouts = {DSGaussianBlurVertical->GetLayout()};
        info.renderPass = renderPassGaussianBlur;
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
        info.width = viewportRT->width_f;
        info.height = viewportRT->height_f;
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {viewportRT->blendAttachment};
        info.pushConstantStage = ShaderStage::FragmentBit;
        info.pushConstantSize = 5 * sizeof(float);
        info.descriptorSetLayouts = {DSCombine->GetLayout()};
        info.renderPass = renderPassCombine;
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
        cmd->ChangeLayout(frameImage, ImageLayout::ShaderReadOnly);
        // Output
        cmd->ChangeLayout(brightFilterRT, ImageLayout::ColorAttachment);

        cmd->BeginPass(renderPassBrightFilter, framebuffers[imageIndex]);
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
        cmd->ChangeLayout(brightFilterRT, ImageLayout::ShaderReadOnly);
        // Output
        cmd->ChangeLayout(gaussianBlurHorizontalRT, ImageLayout::ColorAttachment);

        cmd->BeginPass(renderPassGaussianBlur,
                       framebuffers[static_cast<size_t>(totalImages) + static_cast<size_t>(imageIndex)]);
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
        cmd->ChangeLayout(gaussianBlurHorizontalRT, ImageLayout::ShaderReadOnly);
        // Output
        cmd->ChangeLayout(gaussianBlurVerticalRT, ImageLayout::ColorAttachment);

        cmd->BeginPass(renderPassGaussianBlur,
                       framebuffers[static_cast<size_t>(totalImages) * 2 + static_cast<size_t>(imageIndex)]);
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
        cmd->ChangeLayout(frameImage, ImageLayout::ShaderReadOnly);
        cmd->ChangeLayout(gaussianBlurVerticalRT, ImageLayout::ShaderReadOnly);
        // Output
        cmd->ChangeLayout(viewportRT, ImageLayout::ColorAttachment);

        cmd->BeginPass(renderPassCombine,
                       framebuffers[static_cast<size_t>(totalImages) * 3 + static_cast<size_t>(imageIndex)]);
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
        for (auto *frameBuffer : framebuffers)
            FrameBuffer::Destroy(frameBuffer);

        RenderPass::Destroy(renderPassBrightFilter);
        RenderPass::Destroy(renderPassGaussianBlur);
        RenderPass::Destroy(renderPassCombine);

        Pipeline::Destroy(pipelineBrightFilter);
        Pipeline::Destroy(pipelineGaussianBlurHorizontal);
        Pipeline::Destroy(pipelineGaussianBlurVertical);
        Pipeline::Destroy(pipelineCombine);

        Image::Destroy(frameImage);

        Init();
        CreateRenderPass();
        CreateFrameBuffers();
        UpdateDescriptorSets();
        CreatePipeline();
    }

    void Bloom::Destroy()
    {
        Descriptor::Destroy(DSBrightFilter);
        Descriptor::Destroy(DSGaussianBlurHorizontal);
        Descriptor::Destroy(DSGaussianBlurVertical);
        Descriptor::Destroy(DSCombine);

        for (auto frameBuffer : framebuffers)
            FrameBuffer::Destroy(frameBuffer);

        RenderPass::Destroy(renderPassBrightFilter);
        RenderPass::Destroy(renderPassGaussianBlur);
        RenderPass::Destroy(renderPassCombine);

        Pipeline::Destroy(pipelineBrightFilter);
        Pipeline::Destroy(pipelineGaussianBlurHorizontal);
        Pipeline::Destroy(pipelineGaussianBlurVertical);
        Pipeline::Destroy(pipelineCombine);

        Image::Destroy(frameImage);
    }
}
