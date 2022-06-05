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
        framebuffers.resize(RHII.swapchain->images.size());
        for (size_t i = 0; i < RHII.swapchain->images.size(); ++i)
        {
            uint32_t width = ssaoRT->imageInfo.width;
            uint32_t height = ssaoRT->imageInfo.height;
            ImageViewHandle view = ssaoRT->view;
            framebuffers[i] = FrameBuffer::Create(width, height, &view, 1, renderPass, "ssao_frameBuffer_" + std::to_string(i));
        }
    }

    void SSAO::CreateSSAOBlurFrameBuffers()
    {
        blurFramebuffers.resize(RHII.swapchain->images.size());
        for (size_t i = 0; i < RHII.swapchain->images.size(); ++i)
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
        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.vert", ShaderType::Vertex});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/SSAO/ssao.frag", ShaderType::Fragment});
        info.width = ssaoRT->width_f;
        info.height = ssaoRT->height_f;
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {ssaoRT->blendAttachment};
        info.descriptorSetLayouts = {Pipeline::getDescriptorSetLayoutSSAO()};
        info.renderPass = renderPass;
        info.name = "ssao_pipeline";

        pipeline = Pipeline::Create(info);

        Shader::Destroy(info.pVertShader);
        Shader::Destroy(info.pFragShader);
    }

    void SSAO::CreateBlurPipeline()
    {
        PipelineCreateInfo info{};
        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.vert", ShaderType::Vertex});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/SSAO/ssaoBlur.frag", ShaderType::Fragment});
        info.width = ssaoBlurRT->width_f;
        info.height = ssaoBlurRT->height_f;
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {ssaoBlurRT->blendAttachment};
        info.descriptorSetLayouts = {Pipeline::getDescriptorSetLayoutSSAOBlur()};
        info.renderPass = blurRenderPass;
        info.name = "ssaoBlur_pipeline";

        pipelineBlur = Pipeline::Create(info);

        Shader::Destroy(info.pVertShader);
        Shader::Destroy(info.pFragShader);
    }

    void SSAO::CreateUniforms()
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
        UB_Kernel = Buffer::Create(
            sizeof(vec4) * 16,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
            "ssao_UB_Kernel_buffer");
        UB_Kernel->Map();
        UB_Kernel->CopyData(kernel.data());
        UB_Kernel->Flush();
        UB_Kernel->Unmap();

        // noise image
        std::vector<vec4> noise{};
        for (unsigned int i = 0; i < 16; i++)
            noise.emplace_back(rand(-1.f, 1.f), rand(-1.f, 1.f), 0.f, 1.f);

        const uint64_t bufSize = sizeof(vec4) * 16;
        Buffer *staging = Buffer::Create(
            bufSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            "ssao_UB_Noise_staging_buffer");
        staging->Map();
        staging->CopyData(noise.data());
        staging->Flush();
        staging->Unmap();

        ImageCreateInfo info{};
        info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        info.width = 4;
        info.height = 4;
        info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        info.properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        info.initialState = LayoutState::Preinitialized;
        info.name = "ssao_noise_image";
        noiseTex = Image::Create(info);

        ImageViewCreateInfo viewInfo{};
        viewInfo.image = noiseTex;
        noiseTex->CreateImageView(viewInfo);

        SamplerCreateInfo samplerInfo{};
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;
        samplerInfo.maxAnisotropy = 1.0f;
        noiseTex->CreateSampler(samplerInfo);

        RHII.generalCmd->Begin();
        noiseTex->CopyBufferToImage(RHII.generalCmd, staging);
        noiseTex->ChangeLayout(RHII.generalCmd, LayoutState::ShaderReadOnly);
        RHII.generalCmd->End();
        RHII.generalQueue->SubmitAndWaitFence(1, &RHII.generalCmd, nullptr, 0, nullptr, 0, nullptr);
        
        Buffer::Destroy(staging);
        // pvm uniform
        UB_PVM = Buffer::Create(
            3 * sizeof(mat4),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
            "ssao_UB_PVM_buffer");
        UB_PVM->Map();
        UB_PVM->Zero();
        UB_PVM->Flush();
        UB_PVM->Unmap();

        // DESCRIPTOR SET FOR SSAO
        DSet = Descriptor::Create(Pipeline::getDescriptorSetLayoutSSAO(), 1, "ssao_descriptor");

        // DESCRIPTOR SET FOR SSAO BLUR
        DSBlur = Descriptor::Create(Pipeline::getDescriptorSetLayoutSSAOBlur(), 1, "ssao_blur_descriptor");

        UpdateDescriptorSets();
    }

    void SSAO::UpdateDescriptorSets()
    {
        std::array<DescriptorUpdateInfo, 5> infos{};

        infos[0].binding = 0;
        infos[0].pImage = depth;
        infos[0].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        infos[1].binding = 1;
        infos[1].pImage = normalRT;

        infos[2].binding = 2;
        infos[2].pImage = noiseTex;

        infos[3].binding = 3;
        infos[3].pBuffer = UB_Kernel;

        infos[4].binding = 4;
        infos[4].pBuffer = UB_PVM;

        DSet->UpdateDescriptor(5, infos.data());

        DescriptorUpdateInfo info{};
        info.binding = 0;
        info.pImage = ssaoRT;

        DSBlur->UpdateDescriptor(1, &info);
    }

    void SSAO::Update(Camera *camera)
    {
        if (GUI::show_ssao)
        {
            pvm[0] = camera->projection;
            pvm[1] = camera->view;
            pvm[2] = camera->invProjection;

            MemoryRange range{};
            range.data = &pvm;
            range.size = sizeof(pvm);
            range.offset = 0;
            UB_PVM->Copy(&range, 1, false);
        }
    }

    void SSAO::Draw(CommandBuffer *cmd, uint32_t imageIndex)
    {
        Debug::BeginCmdRegion(cmd->Handle(), "SSAO");
        // SSAO
        // Input
        normalRT->ChangeLayout(cmd, LayoutState::ShaderReadOnly);
        noiseTex->ChangeLayout(cmd, LayoutState::ShaderReadOnly);
        depth->ChangeLayout(cmd, LayoutState::DepthStencilReadOnly);
        // Output
        ssaoRT->ChangeLayout(cmd, LayoutState::ColorAttachment);

        cmd->BeginPass(renderPass, framebuffers[imageIndex]);
        cmd->BindPipeline(pipeline);
        cmd->BindDescriptors(pipeline, 1, &DSet);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
        Debug::EndCmdRegion(cmd->Handle());

        Debug::BeginCmdRegion(cmd->Handle(), "SSAO Blur");
        // SSAO BLUR
        // Input
        ssaoRT->ChangeLayout(cmd, LayoutState::ShaderReadOnly);
        // Output
        ssaoBlurRT->ChangeLayout(cmd, LayoutState::ColorAttachment);

        cmd->BeginPass(blurRenderPass, blurFramebuffers[imageIndex]);
        cmd->BindPipeline(pipelineBlur);
        cmd->BindDescriptors(pipelineBlur, 1, &DSBlur);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
        Debug::EndCmdRegion(cmd->Handle());
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
        CreatePipeline();
        UpdateDescriptorSets();
    }

    void SSAO::Destroy()
    {
        DescriptorLayout::Destroy(Pipeline::getDescriptorSetLayoutSSAO());
        DescriptorLayout::Destroy(Pipeline::getDescriptorSetLayoutSSAOBlur());

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
