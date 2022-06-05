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
        framebuffers.resize(RHII.swapchain->images.size());
        for (size_t i = 0; i < RHII.swapchain->images.size(); ++i)
        {
            uint32_t width = taaRT->imageInfo.width;
            uint32_t height = taaRT->imageInfo.height;
            ImageViewHandle view = taaRT->view;
            framebuffers[i] = FrameBuffer::Create(width, height, &view, 1, renderPass, "taa_frameBuffer_" + std::to_string(i));
        }

        framebuffersSharpen.resize(RHII.swapchain->images.size());
        for (size_t i = 0; i < RHII.swapchain->images.size(); ++i)
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
        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.vert", ShaderType::Vertex});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/TAA/TAA.frag", ShaderType::Fragment});
        info.width = taaRT->width_f;
        info.height = taaRT->height_f;
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {taaRT->blendAttachment};
        info.descriptorSetLayouts = {Pipeline::getDescriptorSetLayoutTAA()};
        info.renderPass = renderPass;
        info.name = "taa_pipeline";

        pipeline = Pipeline::Create(info);

        Shader::Destroy(info.pVertShader);
        Shader::Destroy(info.pFragShader);
    }

    void TAA::CreatePipelineSharpen()
    {
        PipelineCreateInfo info{};
        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.vert", ShaderType::Vertex});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/TAA/TAASharpen.frag", ShaderType::Fragment});
        info.width = viewportRT->width_f;
        info.height = viewportRT->height_f;
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {viewportRT->blendAttachment};
        info.descriptorSetLayouts = {Pipeline::getDescriptorSetLayoutTAASharpen()};
        info.renderPass = renderPassSharpen;
        info.name = "taaSharpen_pipeline";

        pipelineSharpen = Pipeline::Create(info);

        Shader::Destroy(info.pVertShader);
        Shader::Destroy(info.pFragShader);
    }

    void TAA::SaveImage(CommandBuffer *cmd, Image *source)
    {
        previous->ChangeLayout(cmd, LayoutState::TransferDst);
        source->ChangeLayout(cmd, LayoutState::TransferSrc);

        // Copy the image

        VkImageCopy region{};
        region.srcSubresource.aspectMask = source->viewInfo.aspectMask;
        region.srcSubresource.layerCount = 1;
        region.dstSubresource.aspectMask = previous->viewInfo.aspectMask;
        region.dstSubresource.layerCount = 1;
        region.extent.width = source->imageInfo.width;
        region.extent.height = source->imageInfo.height;
        region.extent.depth = 1;

        vkCmdCopyImage(
            cmd->Handle(),
            source->Handle(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            previous->Handle(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &region);

        previous->ChangeLayout(cmd, LayoutState::ShaderReadOnly);
        source->ChangeLayout(cmd, LayoutState::ShaderReadOnly);
    }

    void TAA::CreateUniforms()
    {
        uniform = Buffer::Create(
            sizeof(UBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            "TAA_uniform_buffer");
        uniform->Map();
        uniform->Zero();
        uniform->Flush();
        uniform->Unmap();

        DSet = Descriptor::Create(Pipeline::getDescriptorSetLayoutTAA(), 1, "TAA_descriptor");
        DSetSharpen = Descriptor::Create(Pipeline::getDescriptorSetLayoutTAASharpen(), 1, "TAA_sharpen_descriptor");

        UpdateDescriptorSets();
    }

    void TAA::UpdateDescriptorSets()
    {
        std::array<DescriptorUpdateInfo, 5> infos{};

        infos[0].binding = 0;
        infos[0].pImage = previous;

        infos[1].binding = 1;
        infos[1].pImage = frameImage;

        infos[2].binding = 2;
        infos[2].pImage = depth;
        infos[2].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        infos[3].binding = 3;
        infos[3].pImage = velocityRT;

        infos[4].binding = 4;
        infos[4].pBuffer = uniform;

        DSet->UpdateDescriptor(5, infos.data());

        std::array<DescriptorUpdateInfo, 2> infos2{};

        infos2[0].binding = 0;
        infos2[0].pImage = taaRT;

        infos2[1].binding = 1;
        infos2[1].pBuffer = uniform;

        DSetSharpen->UpdateDescriptor(2, infos2.data());
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

            MemoryRange range{};
            range.data = &ubo;
            range.size = sizeof(ubo);
            range.offset = 0;
            uniform->Copy(&range, 1, false);
        }
    }

    void TAA::Draw(CommandBuffer *cmd, uint32_t imageIndex)
    {
        Debug::BeginCmdRegion(cmd->Handle(), "TAA");
        // Copy viewport image
        frameImage->CopyColorAttachment(cmd, viewportRT);

        // TEMPORAL ANTI-ALIASING
        // Input
        previous->ChangeLayout(cmd, LayoutState::ShaderReadOnly);
        frameImage->ChangeLayout(cmd, LayoutState::ShaderReadOnly);
        velocityRT->ChangeLayout(cmd, LayoutState::ShaderReadOnly);
        depth->ChangeLayout(cmd, LayoutState::DepthStencilReadOnly);
        // Output
        taaRT->ChangeLayout(cmd, LayoutState::ColorAttachment);

        cmd->BeginPass(renderPass, framebuffers[imageIndex]);
        cmd->BindPipeline(pipeline);
        cmd->BindDescriptors(pipeline, 1, &DSet);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
        Debug::EndCmdRegion(cmd->Handle());

        Debug::BeginCmdRegion(cmd->Handle(), "TAA Sharpen");
        // Copy viewport image to store for the next pass
        previous->CopyColorAttachment(cmd, taaRT);

        // TEMPORAL ANTI-ALIASING SHARPEN
        // Input
        taaRT->ChangeLayout(cmd, LayoutState::ShaderReadOnly);
        // Output
        viewportRT->ChangeLayout(cmd, LayoutState::ColorAttachment);

        cmd->BeginPass(renderPassSharpen, framebuffersSharpen[imageIndex]);
        cmd->BindPipeline(pipelineSharpen);
        cmd->BindDescriptors(pipelineSharpen, 1, &DSetSharpen);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
        Debug::EndCmdRegion(cmd->Handle());
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
        CreatePipeline();
        UpdateDescriptorSets();
    }

    void TAA::Destroy()
    {
        DescriptorLayout::Destroy(Pipeline::getDescriptorSetLayoutTAA());
        DescriptorLayout::Destroy(Pipeline::getDescriptorSetLayoutTAASharpen());

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
