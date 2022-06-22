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

#include "TAA.h"
#include "GUI/GUI.h"
#include "Renderer/Surface.h"
#include "Renderer/Swapchain.h"
#include "Shader/Shader.h"
#include "Renderer/RHI.h"
#include "Renderer/Command.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/Image.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Pipeline.h"
#include "Renderer/Buffer.h"
#include "Systems/CameraSystem.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    TAA::TAA()
    {
        DSet = {};
        DSetSharpen = {};
    }

    TAA::~TAA()
    {
    }

    void TAA::Init()
    {
        RendererSystem *rs = CONTEXT->GetSystem<RendererSystem>();

        previous = rs->CreateFSSampledImage();
        frameImage = rs->CreateFSSampledImage();
        taaRT = rs->GetRenderTarget("taa");
        viewportRT = rs->GetRenderTarget("viewport");
        velocityRT = rs->GetRenderTarget("velocity");
        depth = rs->GetDepthTarget("depth");
    }

    void TAA::CreateRenderPass()
    {
        Attachment attachment{};
        attachment.format = taaRT->imageInfo.format;
        renderPass = RenderPass::Create(&attachment, 1, "taa_renderPass");

        attachment.format = viewportRT->imageInfo.format;
        renderPassSharpen = RenderPass::Create(&attachment, 1, "taa_sharpen_renderPass");
    }

    void TAA::CreateFrameBuffers()
    {
        framebuffers.resize(SWAPCHAIN_IMAGES);
        for (size_t i = 0; i < SWAPCHAIN_IMAGES; ++i)
        {
            uint32_t width = taaRT->imageInfo.width;
            uint32_t height = taaRT->imageInfo.height;
            ImageViewHandle view = taaRT->view;
            framebuffers[i] = FrameBuffer::Create(width, height, &view, 1, renderPass, "taa_frameBuffer_" + std::to_string(i));
        }

        framebuffersSharpen.resize(SWAPCHAIN_IMAGES);
        for (size_t i = 0; i < SWAPCHAIN_IMAGES; ++i)
        {
            uint32_t width = viewportRT->imageInfo.width;
            uint32_t height = viewportRT->imageInfo.height;
            ImageViewHandle view = viewportRT->view;
            framebuffersSharpen[i] = FrameBuffer::Create(width, height, &view, 1, renderPassSharpen, "taaSharpen_frameBuffer_" + std::to_string(i));
        }
    }

    void TAA::CreatePipeline()
    {
        CreateTAAPipeline();
        CreatePipelineSharpen();
    }

    void TAA::CreateTAAPipeline()
    {
        PipelineCreateInfo info{};
        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.vert", ShaderStage::VertexBit});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/TAA/TAA.frag", ShaderStage::FragmentBit});
        info.width = taaRT->width_f;
        info.height = taaRT->height_f;
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {taaRT->blendAttachment};
        info.descriptorSetLayouts = {DSet->GetLayout()};
        info.renderPass = renderPass;
        info.name = "taa_pipeline";

        pipeline = Pipeline::Create(info);

        Shader::Destroy(info.pVertShader);
        Shader::Destroy(info.pFragShader);
    }

    void TAA::CreatePipelineSharpen()
    {
        PipelineCreateInfo info{};
        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.vert", ShaderStage::VertexBit});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/TAA/TAASharpen.frag", ShaderStage::FragmentBit});
        info.width = viewportRT->width_f;
        info.height = viewportRT->height_f;
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {viewportRT->blendAttachment};
        info.descriptorSetLayouts = {DSetSharpen->GetLayout()};
        info.renderPass = renderPassSharpen;
        info.name = "taaSharpen_pipeline";

        pipelineSharpen = Pipeline::Create(info);

        Shader::Destroy(info.pVertShader);
        Shader::Destroy(info.pFragShader);
    }

    void TAA::CreateUniforms(CommandBuffer *cmd)
    {
        uniform = Buffer::Create(
            RHII.AlignUniform(sizeof(UBO)) * SWAPCHAIN_IMAGES,
            BufferUsage::UniformBufferBit,
            AllocationCreate::HostAccessSequentialWriteBit,
            "TAA_uniform_buffer");
        uniform->Map();
        uniform->Zero();
        uniform->Flush();
        uniform->Unmap();

        DescriptorBindingInfo bindingInfos[5]{};

        bindingInfos[0].binding = 0;
        bindingInfos[0].type = DescriptorType::CombinedImageSampler;
        bindingInfos[0].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[0].pImage = previous;

        bindingInfos[1].binding = 1;
        bindingInfos[1].type = DescriptorType::CombinedImageSampler;
        bindingInfos[1].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[1].pImage = frameImage;

        bindingInfos[2].binding = 2;
        bindingInfos[2].type = DescriptorType::CombinedImageSampler;
        bindingInfos[2].imageLayout = ImageLayout::DepthStencilReadOnly;
        bindingInfos[2].pImage = depth;

        bindingInfos[3].binding = 3;
        bindingInfos[3].type = DescriptorType::CombinedImageSampler;
        bindingInfos[3].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[3].pImage = velocityRT;

        bindingInfos[4].binding = 4;
        bindingInfos[4].type = DescriptorType::UniformBufferDynamic;
        bindingInfos[4].pBuffer = uniform;

        DescriptorInfo info{};
        info.count = 5;
        info.bindingInfos = bindingInfos;
        info.stage = ShaderStage::FragmentBit;

        DSet = Descriptor::Create(&info, "TAA_descriptor");

        bindingInfos[0].pImage = taaRT;

        bindingInfos[1].type = DescriptorType::UniformBufferDynamic;
        bindingInfos[1].pBuffer = uniform;
        bindingInfos[1].pImage = nullptr;

        info.count = 2;

        DSetSharpen = Descriptor::Create(&info, "TAA_sharpen_descriptor");
    }

    void TAA::UpdateDescriptorSets()
    {
        DescriptorBindingInfo bindingInfos[5]{};

        bindingInfos[0].binding = 0;
        bindingInfos[0].type = DescriptorType::CombinedImageSampler;
        bindingInfos[0].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[0].pImage = previous;

        bindingInfos[1].binding = 1;
        bindingInfos[1].type = DescriptorType::CombinedImageSampler;
        bindingInfos[1].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[1].pImage = frameImage;

        bindingInfos[2].binding = 2;
        bindingInfos[2].type = DescriptorType::CombinedImageSampler;
        bindingInfos[2].imageLayout = ImageLayout::DepthStencilReadOnly;
        bindingInfos[2].pImage = depth;

        bindingInfos[3].binding = 3;
        bindingInfos[3].type = DescriptorType::CombinedImageSampler;
        bindingInfos[3].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[3].pImage = velocityRT;

        bindingInfos[4].binding = 4;
        bindingInfos[4].type = DescriptorType::UniformBufferDynamic;
        bindingInfos[4].pBuffer = uniform;

        DescriptorInfo info{};
        info.count = 5;
        info.bindingInfos = bindingInfos;
        info.stage = ShaderStage::FragmentBit;

        DSet->UpdateDescriptor(&info);

        bindingInfos[0].pImage = taaRT;

        bindingInfos[1].type = DescriptorType::UniformBufferDynamic;
        bindingInfos[1].pBuffer = uniform;
        bindingInfos[1].pImage = nullptr;

        info.count = 2;

        DSetSharpen->UpdateDescriptor(&info);
    }

    void TAA::Update(Camera *camera)
    {
        if (GUI::use_TAA)
        {
            ubo.values = {camera->projOffset.x, camera->projOffset.y, GUI::TAA_feedback_min, GUI::TAA_feedback_max};
            ubo.sharpenValues = {
                GUI::TAA_sharp_strength, GUI::TAA_sharp_clamp, GUI::TAA_sharp_offset_bias,
                sin(static_cast<float>(ImGui::GetTime()) * 0.125f)};
            ubo.invProj = camera->invProjection;

            MemoryRange mr{};
            mr.data = &ubo;
            mr.size = sizeof(ubo);
            mr.offset = RHII.GetFrameDynamicOffset(uniform->Size(), RHII.GetFrameIndex());
            uniform->Copy(1, &mr, false);
        }
    }

    void TAA::Draw(CommandBuffer *cmd, uint32_t imageIndex)
    {
        cmd->BeginDebugRegion("TAA");
        // Copy viewport image
        cmd->CopyImage(viewportRT, frameImage);

        // TEMPORAL ANTI-ALIASING
        // Input
        cmd->ImageBarrier(previous, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(frameImage, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(velocityRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(depth, ImageLayout::DepthStencilReadOnly);
        // Output
        cmd->ImageBarrier(taaRT, ImageLayout::ColorAttachment);

        cmd->BeginPass(renderPass, framebuffers[imageIndex]);
        cmd->BindPipeline(pipeline);
        cmd->BindDescriptors(pipeline, 1, &DSet);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
        cmd->EndDebugRegion();

        cmd->BeginDebugRegion("TAA Sharpen");
        // Copy viewport image to store for the next pass
        cmd->CopyImage(taaRT, previous);

        // TEMPORAL ANTI-ALIASING SHARPEN
        // Input
        cmd->ImageBarrier(taaRT, ImageLayout::ShaderReadOnly);
        // Output
        cmd->ImageBarrier(viewportRT, ImageLayout::ColorAttachment);

        cmd->BeginPass(renderPassSharpen, framebuffersSharpen[imageIndex]);
        cmd->BindPipeline(pipelineSharpen);
        cmd->BindDescriptors(pipelineSharpen, 1, &DSetSharpen);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
        cmd->EndDebugRegion();
    }

    void TAA::Resize(uint32_t width, uint32_t height)
    {
        for (auto &framebuffer : framebuffers)
            FrameBuffer::Destroy(framebuffer);
        for (auto &framebuffer : framebuffersSharpen)
            FrameBuffer::Destroy(framebuffer);

        RenderPass::Destroy(renderPass);
        RenderPass::Destroy(renderPassSharpen);

        Pipeline::Destroy(pipeline);
        Pipeline::Destroy(pipelineSharpen);

        Image::Destroy(previous);
        Image::Destroy(frameImage);

        Init();
        CreateRenderPass();
        CreateFrameBuffers();
        UpdateDescriptorSets();
        CreatePipeline();
    }

    void TAA::Destroy()
    {
        Descriptor::Destroy(DSet);
        Descriptor::Destroy(DSetSharpen);

        for (auto framebuffer : framebuffers)
            FrameBuffer::Destroy(framebuffer);
        for (auto framebuffer : framebuffersSharpen)
            FrameBuffer::Destroy(framebuffer);

        RenderPass::Destroy(renderPass);
        RenderPass::Destroy(renderPassSharpen);

        Pipeline::Destroy(pipeline);
        Pipeline::Destroy(pipelineSharpen);

        Buffer::Destroy(uniform);

        Image::Destroy(previous);
        Image::Destroy(frameImage);
    }
}
