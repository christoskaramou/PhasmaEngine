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
        renderPassBrightFilter = RenderPass::Create(attachment);

        attachment.format = gaussianBlurHorizontalRT->imageInfo.format;
        renderPassGaussianBlur = RenderPass::Create(attachment);

        attachment.format = viewportRT->imageInfo.format;
        renderPassCombine = RenderPass::Create(attachment);
    }

    void Bloom::CreateFrameBuffers()
    {
        framebuffers.resize(RHII.swapchain->images.size() * 4);
        for (size_t i = 0; i < RHII.swapchain->images.size(); ++i)
        {
            uint32_t width = brightFilterRT->imageInfo.width;
            uint32_t height = brightFilterRT->imageInfo.height;
            ImageViewHandle view = brightFilterRT->view;
            framebuffers[i] = FrameBuffer::Create(width, height, view, renderPassBrightFilter);
        }

        for (size_t i = RHII.swapchain->images.size(); i < RHII.swapchain->images.size() * 2; ++i)
        {
            uint32_t width = gaussianBlurHorizontalRT->imageInfo.width;
            uint32_t height = gaussianBlurHorizontalRT->imageInfo.height;
            ImageViewHandle view = gaussianBlurHorizontalRT->view;
            framebuffers[i] = FrameBuffer::Create(width, height, view, renderPassGaussianBlur);
        }

        for (size_t i = RHII.swapchain->images.size() * 2; i < RHII.swapchain->images.size() * 3; ++i)
        {
            uint32_t width = gaussianBlurVerticalRT->imageInfo.width;
            uint32_t height = gaussianBlurVerticalRT->imageInfo.height;
            ImageViewHandle view = gaussianBlurVerticalRT->view;
            framebuffers[i] = FrameBuffer::Create(width, height, view, renderPassGaussianBlur);
        }

        for (size_t i = RHII.swapchain->images.size() * 3; i < RHII.swapchain->images.size() * 4; ++i)
        {
            uint32_t width = viewportRT->imageInfo.width;
            uint32_t height = viewportRT->imageInfo.height;
            ImageViewHandle view = viewportRT->view;
            framebuffers[i] = FrameBuffer::Create(width, height, view, renderPassCombine);
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
        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.vert", ShaderType::Vertex});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/Bloom/brightFilter.frag", ShaderType::Fragment});
        info.width = brightFilterRT->width_f;
        info.height = brightFilterRT->height_f;
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {brightFilterRT->blendAttachment};
        info.pushConstantStage = PushConstantStage::Fragment;
        info.pushConstantSize = 5 * sizeof(float);
        info.descriptorSetLayouts = {Pipeline::getDescriptorSetLayoutBrightFilter()};
        info.renderPass = renderPassBrightFilter;

        pipelineBrightFilter = Pipeline::Create(info);

        info.pVertShader->Destroy();
        info.pFragShader->Destroy();
    }

    void Bloom::CreateGaussianBlurHorizontaPipeline()
    {
        PipelineCreateInfo info{};
        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.vert", ShaderType::Vertex});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/Bloom/gaussianBlurHorizontal.frag", ShaderType::Fragment});
        info.width = gaussianBlurHorizontalRT->width_f;
        info.height = gaussianBlurHorizontalRT->height_f;
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {gaussianBlurHorizontalRT->blendAttachment};
        info.pushConstantStage = PushConstantStage::Fragment;
        info.pushConstantSize = 5 * sizeof(float);
        info.descriptorSetLayouts = {Pipeline::getDescriptorSetLayoutGaussianBlurH()};
        info.renderPass = renderPassGaussianBlur;

        pipelineGaussianBlurHorizontal = Pipeline::Create(info);

        info.pVertShader->Destroy();
        info.pFragShader->Destroy();
    }

    void Bloom::CreateGaussianBlurVerticalPipeline()
    {
        PipelineCreateInfo info{};
        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.vert", ShaderType::Vertex});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/Bloom/gaussianBlurVertical.frag", ShaderType::Fragment});
        info.width = gaussianBlurVerticalRT->width_f;
        info.height = gaussianBlurVerticalRT->height_f;
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {gaussianBlurVerticalRT->blendAttachment};
        info.pushConstantStage = PushConstantStage::Fragment;
        info.pushConstantSize = 5 * sizeof(float);
        info.descriptorSetLayouts = {Pipeline::getDescriptorSetLayoutGaussianBlurV()};
        info.renderPass = renderPassGaussianBlur;

        pipelineGaussianBlurVertical = Pipeline::Create(info);

        info.pVertShader->Destroy();
        info.pFragShader->Destroy();
    }

    void Bloom::CreateCombinePipeline()
    {
        PipelineCreateInfo info{};
        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.vert", ShaderType::Vertex});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/Bloom/combine.frag", ShaderType::Fragment});
        info.width = viewportRT->width_f;
        info.height = viewportRT->height_f;
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {viewportRT->blendAttachment};
        info.pushConstantStage = PushConstantStage::Fragment;
        info.pushConstantSize = 5 * sizeof(float);
        info.descriptorSetLayouts = {Pipeline::getDescriptorSetLayoutCombine()};
        info.renderPass = renderPassCombine;

        pipelineCombine = Pipeline::Create(info);

        info.pVertShader->Destroy();
        info.pFragShader->Destroy();
    }

    void Bloom::CreateUniforms()
    {
        DSBrightFilter = Descriptor::Create(Pipeline::getDescriptorSetLayoutBrightFilter());
        DSGaussianBlurHorizontal = Descriptor::Create(Pipeline::getDescriptorSetLayoutGaussianBlurH());
        DSGaussianBlurVertical = Descriptor::Create(Pipeline::getDescriptorSetLayoutGaussianBlurV());
        DSCombine = Descriptor::Create(Pipeline::getDescriptorSetLayoutCombine());

        UpdateDescriptorSets();
    }

    void Bloom::UpdateDescriptorSets()
    {
        DescriptorUpdateInfo info{};
        std::array<DescriptorUpdateInfo, 2> infos{};

        info.binding = 0;
        info.pImage = frameImage;
        DSBrightFilter->UpdateDescriptor(1, &info);

        info.binding = 0;
        info.pImage = brightFilterRT;
        DSGaussianBlurHorizontal->UpdateDescriptor(1, &info);

        info.binding = 0;
        info.pImage = gaussianBlurHorizontalRT;
        DSGaussianBlurVertical->UpdateDescriptor(1, &info);

        infos[0].binding = 0;
        infos[0].pImage = frameImage;
        infos[1].binding = 1;
        infos[1].pImage = gaussianBlurVerticalRT;
        DSCombine->UpdateDescriptor(2, infos.data());
    }

    void Bloom::Update(Camera *camera)
    {
    }

    void Bloom::Draw(CommandBuffer *cmd, uint32_t imageIndex)
    {
        uint32_t totalImages = static_cast<uint32_t>(RHII.swapchain->images.size());

        std::vector<float> values{
            GUI::Bloom_Inv_brightness,
            GUI::Bloom_intensity,
            GUI::Bloom_range,
            GUI::Bloom_exposure,
            static_cast<float>(GUI::use_tonemap)};

        // Copy viewport image
        frameImage->CopyColorAttachment(cmd, viewportRT);

        // BRIGHT FILTER
        // Input
        frameImage->ChangeLayout(cmd, LayoutState::ShaderReadOnly);
        // Output
        brightFilterRT->ChangeLayout(cmd, LayoutState::ColorAttachment);

        cmd->BeginPass(renderPassBrightFilter, framebuffers[imageIndex]);
        cmd->PushConstants(pipelineBrightFilter, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           uint32_t(sizeof(float) * values.size()), values.data());
        cmd->BindPipeline(pipelineBrightFilter);
        cmd->BindDescriptors(pipelineBrightFilter, 1, &DSBrightFilter);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();

        // GUASSIAN BLUR HORIZONTAL
        // Input
        brightFilterRT->ChangeLayout(cmd, LayoutState::ShaderReadOnly);
        // Output
        gaussianBlurHorizontalRT->ChangeLayout(cmd, LayoutState::ColorAttachment);

        cmd->BeginPass(renderPassGaussianBlur,
                       framebuffers[static_cast<size_t>(totalImages) + static_cast<size_t>(imageIndex)]);
        cmd->PushConstants(pipelineGaussianBlurHorizontal, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           uint32_t(sizeof(float) * values.size()), values.data());
        cmd->BindPipeline(pipelineGaussianBlurHorizontal);
        cmd->BindDescriptors(pipelineGaussianBlurHorizontal, 1, &DSGaussianBlurHorizontal);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();

        // GAUSSIAN BLUR VERTICAL
        // Input
        gaussianBlurHorizontalRT->ChangeLayout(cmd, LayoutState::ShaderReadOnly);
        // Output
        gaussianBlurVerticalRT->ChangeLayout(cmd, LayoutState::ColorAttachment);

        cmd->BeginPass(renderPassGaussianBlur,
                       framebuffers[static_cast<size_t>(totalImages) * 2 + static_cast<size_t>(imageIndex)]);
        cmd->PushConstants(pipelineGaussianBlurVertical, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           uint32_t(sizeof(float) * values.size()), values.data());
        cmd->BindPipeline(pipelineGaussianBlurVertical);
        cmd->BindDescriptors(pipelineGaussianBlurVertical, 1, &DSGaussianBlurVertical);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
        
        // COMBINE
        // Input
        frameImage->ChangeLayout(cmd, LayoutState::ShaderReadOnly);
        gaussianBlurVerticalRT->ChangeLayout(cmd, LayoutState::ShaderReadOnly);
        // Output
        viewportRT->ChangeLayout(cmd, LayoutState::ColorAttachment);

        cmd->BeginPass(renderPassCombine,
                       framebuffers[static_cast<size_t>(totalImages) * 3 + static_cast<size_t>(imageIndex)]);
        cmd->PushConstants(pipelineCombine, VK_SHADER_STAGE_FRAGMENT_BIT, 0, uint32_t(sizeof(float) * values.size()),
                           values.data());
        cmd->BindPipeline(pipelineCombine);
        cmd->BindDescriptors(pipelineCombine, 1, &DSCombine);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
    }

    void Bloom::Resize(uint32_t width, uint32_t height)
    {
        for (auto *frameBuffer : framebuffers)
            frameBuffer->Destroy();

        renderPassBrightFilter->Destroy();
        renderPassGaussianBlur->Destroy();
        renderPassCombine->Destroy();

        pipelineBrightFilter->Destroy();
        pipelineGaussianBlurHorizontal->Destroy();
        pipelineGaussianBlurVertical->Destroy();
        pipelineCombine->Destroy();

        frameImage->Destroy();

        Init();
        CreateRenderPass();
        CreateFrameBuffers();
        CreatePipeline();
        UpdateDescriptorSets();
    }

    void Bloom::Destroy()
    {
        Pipeline::getDescriptorSetLayoutBrightFilter()->Destroy();
        Pipeline::getDescriptorSetLayoutGaussianBlurH()->Destroy();
        Pipeline::getDescriptorSetLayoutGaussianBlurV()->Destroy();
        Pipeline::getDescriptorSetLayoutCombine()->Destroy();

        for (auto frameBuffer : framebuffers)
            frameBuffer->Destroy();

        renderPassBrightFilter->Destroy();
        renderPassGaussianBlur->Destroy();
        renderPassCombine->Destroy();

        pipelineBrightFilter->Destroy();
        pipelineGaussianBlurHorizontal->Destroy();
        pipelineGaussianBlurVertical->Destroy();
        pipelineCombine->Destroy();
        frameImage->Destroy();
    }
}
