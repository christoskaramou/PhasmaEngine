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

#include "MotionBlur.h"
#include "Renderer/Surface.h"
#include "Renderer/Swapchain.h"
#include "GUI/GUI.h"
#include "Shader/Shader.h"
#include "Renderer/RHI.h"
#include "Renderer/Command.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/Image.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Pipeline.h"
#include "Renderer/Buffer.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    MotionBlur::MotionBlur()
    {
        DSet = {};
    }

    MotionBlur::~MotionBlur()
    {
    }

    void MotionBlur::Init()
    {
        RendererSystem *rs = CONTEXT->GetSystem<RendererSystem>();

        viewportRT = rs->GetRenderTarget("viewport");
        velocityRT = rs->GetRenderTarget("velocity");
        frameImage = rs->CreateFSSampledImage();
        depth = rs->GetDepthTarget("depth");
    }

    void MotionBlur::CreateRenderPass()
    {
        Attachment attachment{};
        attachment.format = viewportRT->imageInfo.format;
        renderPass = RenderPass::Create(&attachment, 1, "motionBlur_renderPass");
    }

    void MotionBlur::CreateFrameBuffers()
    {
        framebuffers.resize(RHII.swapchain->images.size());
        for (size_t i = 0; i < RHII.swapchain->images.size(); ++i)
        {
            uint32_t width = viewportRT->imageInfo.width;
            uint32_t height = viewportRT->imageInfo.height;
            ImageViewHandle view = viewportRT->view;
            framebuffers[i] = FrameBuffer::Create(width, height, &view, 1, renderPass, "motionBlur_frameBuffer_" + std::to_string(i));
        }
    }

    void MotionBlur::CreatePipeline()
    {
        PipelineCreateInfo info{};
        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.vert", ShaderType::Vertex});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/MotionBlur/motionBlur.frag", ShaderType::Fragment});
        info.width = viewportRT->width_f;
        info.height = viewportRT->height_f;
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {viewportRT->blendAttachment};
        info.pushConstantStage = PushConstantStage::Fragment;
        info.pushConstantSize = sizeof(vec4);
        info.descriptorSetLayouts = {Pipeline::getDescriptorSetLayoutMotionBlur()};
        info.renderPass = renderPass;
        info.name = "motionBlur_pipeline";

        pipeline = Pipeline::Create(info);

        Shader::Destroy(info.pVertShader);
        Shader::Destroy(info.pFragShader);
    }

    void MotionBlur::CreateUniforms()
    {
        DSet = Descriptor::Create(Pipeline::getDescriptorSetLayoutMotionBlur(), 1, "MotionBlur_descriptor");
        UpdateDescriptorSets();
    }

    void MotionBlur::UpdateDescriptorSets()
    {
        std::array<DescriptorUpdateInfo, 3> infos{};

        infos[0].binding = 0;
        infos[0].pImage = frameImage;

        infos[1].binding = 1;
        infos[1].pImage = depth;
        infos[1].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        infos[2].binding = 2;
        infos[2].pImage = velocityRT;

        DSet->UpdateDescriptor(static_cast<uint32_t>(infos.size()), infos.data());
    }

    void MotionBlur::Update(Camera *camera)
    {
        if (GUI::show_motionBlur)
        {
        }
    }

    void MotionBlur::Draw(CommandBuffer *cmd, uint32_t imageIndex)
    {
        const vec4 values{
            1.f / static_cast<float>(FrameTimer::Instance().GetDelta()),
            0.f, // sin(static_cast<float>(FrameTimer::Instance().Count()) * 0.125f),
            GUI::motionBlur_strength,
            0.f};

        Debug::BeginCmdRegion(cmd->Handle(), "MotionBlur");
        // Copy viewport image
        frameImage->CopyColorAttachment(cmd, viewportRT);

        // MOTION BLUR
        // Input
        frameImage->ChangeLayout(cmd, LayoutState::ShaderReadOnly);
        velocityRT->ChangeLayout(cmd, LayoutState::ShaderReadOnly);
        depth->ChangeLayout(cmd, LayoutState::DepthStencilReadOnly);
        // Output
        viewportRT->ChangeLayout(cmd, LayoutState::ColorAttachment);

        cmd->BeginPass(renderPass, framebuffers[imageIndex]);
        cmd->PushConstants(pipeline, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(vec4), &values);
        cmd->BindPipeline(pipeline);
        cmd->BindDescriptors(pipeline, 1, &DSet);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
        Debug::EndCmdRegion(cmd->Handle());
    }

    void MotionBlur::Resize(uint32_t width, uint32_t height)
    {
        for (auto *frameBuffer : framebuffers)
            FrameBuffer::Destroy(frameBuffer);
        RenderPass::Destroy(renderPass);
        Pipeline::Destroy(pipeline);
        Image::Destroy(frameImage);

        Init();
        CreateRenderPass();
        CreateFrameBuffers();
        CreatePipeline();
        UpdateDescriptorSets();
    }

    void MotionBlur::Destroy()
    {
        DescriptorLayout::Destroy(Pipeline::getDescriptorSetLayoutMotionBlur());

        for (auto frameBuffer : framebuffers)
            FrameBuffer::Destroy(frameBuffer);

        RenderPass::Destroy(renderPass);
        Pipeline::Destroy(pipeline);
        Image::Destroy(frameImage);
    }
}
