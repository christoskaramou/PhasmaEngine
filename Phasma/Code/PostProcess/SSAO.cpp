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

#include "SSAO.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Surface.h"
#include "GUI/GUI.h"
#include "Shader/Shader.h"
#include "Renderer/RHI.h"
#include "Renderer/Command.h"
#include "Renderer/Queue.h"
#include "Renderer/Descriptor.h"
#include "Renderer/FrameBuffer.h"
#include "Renderer/Image.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Pipeline.h"
#include "Renderer/Buffer.h"
#include "Systems/CameraSystem.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    void SSAO::Init()
    {
        RendererSystem *rs = CONTEXT->GetSystem<RendererSystem>();

        ssaoRT = rs->GetRenderTarget("ssao");
        ssaoBlurRT = rs->GetRenderTarget("ssaoBlur");
        normalRT = rs->GetRenderTarget("normal");
        depth = rs->GetDepthTarget("depth");
    }

    void SSAO::CreateRenderPass()
    {
        Attachment attachment{};
        attachment.format = ssaoRT->imageInfo.format;
        renderPass = RenderPass::Create(&attachment, 1, "ssao_renderpass");

        attachment.format = ssaoBlurRT->imageInfo.format;
        blurRenderPass = RenderPass::Create(&attachment, 1, "ssaoBlur_renderpass");
    }

    void SSAO::CreateFrameBuffers()
    {
        CreateSSAOFrameBuffers();
        CreateSSAOBlurFrameBuffers();
    }

    void SSAO::CreateSSAOFrameBuffers()
    {
        framebuffers.resize(SWAPCHAIN_IMAGES);
        for (size_t i = 0; i < SWAPCHAIN_IMAGES; ++i)
        {
            uint32_t width = ssaoRT->imageInfo.width;
            uint32_t height = ssaoRT->imageInfo.height;
            ImageViewHandle view = ssaoRT->view;
            framebuffers[i] = FrameBuffer::Create(width, height, &view, 1, renderPass, "ssao_frameBuffer_" + std::to_string(i));
        }
    }

    void SSAO::CreateSSAOBlurFrameBuffers()
    {
        blurFramebuffers.resize(SWAPCHAIN_IMAGES);
        for (size_t i = 0; i < SWAPCHAIN_IMAGES; ++i)
        {
            uint32_t width = ssaoBlurRT->imageInfo.width;
            uint32_t height = ssaoBlurRT->imageInfo.height;
            ImageViewHandle view = ssaoBlurRT->view;
            blurFramebuffers[i] = FrameBuffer::Create(width, height, &view, 1, blurRenderPass, "ssaoBlur_frameBuffer_" + std::to_string(i));
        }
    }

    void SSAO::CreatePipeline()
    {
        CreateSSAOPipeline();
        CreateBlurPipeline();
    }

    void SSAO::CreateSSAOPipeline()
    {
        PipelineCreateInfo info{};
        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.vert", ShaderStage::VertexBit});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/SSAO/ssao.frag", ShaderStage::FragmentBit});
        info.width = ssaoRT->width_f;
        info.height = ssaoRT->height_f;
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {ssaoRT->blendAttachment};
        info.descriptorSetLayouts = {DSet->GetLayout()};
        info.renderPass = renderPass;
        info.name = "ssao_pipeline";

        pipeline = Pipeline::Create(info);

        Shader::Destroy(info.pVertShader);
        Shader::Destroy(info.pFragShader);
    }

    void SSAO::CreateBlurPipeline()
    {
        PipelineCreateInfo info{};
        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.vert", ShaderStage::VertexBit});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/SSAO/ssaoBlur.frag", ShaderStage::FragmentBit});
        info.width = ssaoBlurRT->width_f;
        info.height = ssaoBlurRT->height_f;
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {ssaoBlurRT->blendAttachment};
        info.descriptorSetLayouts = {DSBlur->GetLayout()};
        info.renderPass = blurRenderPass;
        info.name = "ssaoBlur_pipeline";

        pipelineBlur = Pipeline::Create(info);

        Shader::Destroy(info.pVertShader);
        Shader::Destroy(info.pFragShader);
    }

    void SSAO::CreateUniforms(CommandBuffer *cmd)
    {
        // kernel buffer
        std::vector<vec4> kernel{};
        for (unsigned i = 0; i < 16; i++)
        {
            vec3 sample(rand(-1.f, 1.f), rand(-1.f, 1.f), rand(0.f, 1.f));
            sample = normalize(sample);
            sample *= rand(0.f, 1.f);
            float scale = (float)i / 16.f;
            scale = lerp(.1f, 1.f, scale * scale);
            kernel.emplace_back(sample * scale, 0.f);
        }

        size_t size = kernel.size() * sizeof(vec4);
        UB_Kernel = Buffer::Create(
            sizeof(vec4) * 16,
            BufferUsage::UniformBufferBit,
            AllocationCreate::HostAccessRandomBit,
            "ssao_UB_Kernel_buffer");

        MemoryRange mr{};
        mr.data = kernel.data();
        mr.size = size;
        UB_Kernel->Copy(1, &mr, false);

        // noise image
        std::vector<vec4> noise{};
        for (unsigned int i = 0; i < 16; i++)
            noise.emplace_back(rand(-1.f, 1.f), rand(-1.f, 1.f), 0.f, 1.f);

        ImageCreateInfo imageInfo{};
        imageInfo.format = Format::RGBA16SFloat;
        imageInfo.width = 4;
        imageInfo.height = 4;
        imageInfo.usage = ImageUsage::TransferSrcBit | ImageUsage::TransferDstBit | ImageUsage::SampledBit;
        imageInfo.properties = MemoryProperty::DeviceLocalBit;
        imageInfo.initialLayout = ImageLayout::Preinitialized;
        imageInfo.name = "ssao_noise_image";
        noiseTex = Image::Create(imageInfo);

        ImageViewCreateInfo viewInfo{};
        viewInfo.image = noiseTex;
        noiseTex->CreateImageView(viewInfo);

        SamplerCreateInfo samplerInfo{};
        samplerInfo.minFilter = Filter::Nearest;
        samplerInfo.magFilter = Filter::Nearest;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;
        samplerInfo.maxAnisotropy = 1.0f;
        noiseTex->CreateSampler(samplerInfo);

        cmd->CopyDataToImageStaged(noiseTex, noise.data(), size);

        // pvm uniform
        UB_PVM = Buffer::Create(
            3 * sizeof(mat4),
            BufferUsage::UniformBufferBit,
            AllocationCreate::HostAccessRandomBit,
            "ssao_UB_PVM_buffer");
        UB_PVM->Map();
        UB_PVM->Zero();
        UB_PVM->Flush();
        UB_PVM->Unmap();

        // DESCRIPTOR SET FOR SSAO
        DescriptorBindingInfo bindingInfos[5]{};

        bindingInfos[0].binding = 0;
        bindingInfos[0].type = DescriptorType::CombinedImageSampler;
        bindingInfos[0].imageLayout = ImageLayout::DepthStencilReadOnly;
        bindingInfos[0].pImage = depth;

        bindingInfos[1].binding = 1;
        bindingInfos[1].type = DescriptorType::CombinedImageSampler;
        bindingInfos[1].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[1].pImage = normalRT;

        bindingInfos[2].binding = 2;
        bindingInfos[2].type = DescriptorType::CombinedImageSampler;
        bindingInfos[2].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[2].pImage = noiseTex;

        bindingInfos[3].binding = 3;
        bindingInfos[3].type = DescriptorType::UniformBuffer;
        bindingInfos[3].pBuffer = UB_Kernel;

        bindingInfos[4].binding = 4;
        bindingInfos[4].type = DescriptorType::UniformBuffer;
        bindingInfos[4].pBuffer = UB_PVM;

        DescriptorInfo info{};
        info.count = 5;
        info.bindingInfos = bindingInfos;
        info.stage = ShaderStage::FragmentBit;

        DSet = Descriptor::Create(&info, "ssao_descriptor");

        // DESCRIPTOR SET FOR SSAO BLUR
        bindingInfos[0].binding = 0;
        bindingInfos[0].type = DescriptorType::CombinedImageSampler;
        bindingInfos[0].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[0].pImage = ssaoRT;

        info.count = 1;

        DSBlur = Descriptor::Create(&info, "ssao_blur_descriptor");
    }

    void SSAO::UpdateDescriptorSets()
    {
        DescriptorBindingInfo bindingInfos[5]{};

        bindingInfos[0].binding = 0;
        bindingInfos[0].type = DescriptorType::CombinedImageSampler;
        bindingInfos[0].imageLayout = ImageLayout::DepthStencilReadOnly;
        bindingInfos[0].pImage = depth;

        bindingInfos[1].binding = 1;
        bindingInfos[1].type = DescriptorType::CombinedImageSampler;
        bindingInfos[1].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[1].pImage = normalRT;

        bindingInfos[2].binding = 2;
        bindingInfos[2].type = DescriptorType::CombinedImageSampler;
        bindingInfos[2].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[2].pImage = noiseTex;

        bindingInfos[3].binding = 3;
        bindingInfos[3].type = DescriptorType::UniformBuffer;
        bindingInfos[3].pBuffer = UB_Kernel;

        bindingInfos[4].binding = 4;
        bindingInfos[4].type = DescriptorType::UniformBuffer;
        bindingInfos[4].pBuffer = UB_PVM;

        DescriptorInfo info{};
        info.count = 5;
        info.bindingInfos = bindingInfos;
        info.stage = ShaderStage::FragmentBit;

        DSet->UpdateDescriptor(&info);

        // DESCRIPTOR SET FOR SSAO BLUR
        bindingInfos[0].binding = 0;
        bindingInfos[0].type = DescriptorType::CombinedImageSampler;
        bindingInfos[0].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[0].pImage = ssaoRT;

        info.count = 1;

        DSBlur->UpdateDescriptor(&info);
    }

    void SSAO::Update(Camera *camera)
    {
        if (GUI::show_ssao)
        {
            pvm[0] = camera->projection;
            pvm[1] = camera->view;
            pvm[2] = camera->invProjection;

            MemoryRange mr{};
            mr.data = &pvm;
            mr.size = sizeof(pvm);
            mr.offset = 0;
            UB_PVM->Copy(1, &mr, false);
        }
    }

    void SSAO::Draw(CommandBuffer *cmd, uint32_t imageIndex)
    {
        cmd->BeginDebugRegion("SSAO");
        // SSAO
        // Input
        cmd->ImageBarrier(normalRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(noiseTex, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(depth, ImageLayout::DepthStencilReadOnly);
        // Output
        cmd->ImageBarrier(ssaoRT, ImageLayout::ColorAttachment);

        cmd->BeginPass(renderPass, framebuffers[imageIndex]);
        cmd->BindPipeline(pipeline);
        cmd->BindDescriptors(pipeline, 1, &DSet);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
        cmd->EndDebugRegion();

        cmd->BeginDebugRegion("SSAO Blur");
        // SSAO BLUR
        // Input
        cmd->ImageBarrier(ssaoRT, ImageLayout::ShaderReadOnly);
        // Output
        cmd->ImageBarrier(ssaoBlurRT, ImageLayout::ColorAttachment);

        cmd->BeginPass(blurRenderPass, blurFramebuffers[imageIndex]);
        cmd->BindPipeline(pipelineBlur);
        cmd->BindDescriptors(pipelineBlur, 1, &DSBlur);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
        cmd->EndDebugRegion();
    }

    void SSAO::Resize(uint32_t width, uint32_t height)
    {
        RenderPass::Destroy(renderPass);
        RenderPass::Destroy(blurRenderPass);

        for (auto &framebuffer : framebuffers)
            FrameBuffer::Destroy(framebuffer);
        for (auto &framebuffer : blurFramebuffers)
            FrameBuffer::Destroy(framebuffer);

        Pipeline::Destroy(pipeline);
        Pipeline::Destroy(pipelineBlur);

        Init();
        CreateRenderPass();
        CreateFrameBuffers();
        UpdateDescriptorSets();
        CreatePipeline();
    }

    void SSAO::Destroy()
    {
        Descriptor::Destroy(DSet);
        Descriptor::Destroy(DSBlur);

        for (auto frameBuffer : framebuffers)
            FrameBuffer::Destroy(frameBuffer);
        for (auto frameBuffer : blurFramebuffers)
            FrameBuffer::Destroy(frameBuffer);

        RenderPass::Destroy(renderPass);
        RenderPass::Destroy(blurRenderPass);

        Pipeline::Destroy(pipeline);
        Pipeline::Destroy(pipelineBlur);

        Buffer::Destroy(UB_Kernel);
        Buffer::Destroy(UB_PVM);
        Image::Destroy(noiseTex);
    }
}
