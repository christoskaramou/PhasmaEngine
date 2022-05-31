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

#include "PostProcess/SSGI.h"
#include "GUI/GUI.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Surface.h"
#include "Shader/Shader.h"
#include "Renderer/RHI.h"
#include "Renderer/Command.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/Image.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Pipeline.h"
#include "Systems/CameraSystem.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    SSGI::SSGI()
    {
        DSet = {};
    }

    SSGI::~SSGI()
    {
    }

    void SSGI::Init()
    {
        RendererSystem *rs = CONTEXT->GetSystem<RendererSystem>();

        viewportRT = rs->GetRenderTarget("viewport");
        frameImage = rs->CreateFSSampledImage();
        depth = rs->GetDepthTarget("depth");
    }

    void SSGI::CreateRenderPass()
    {
        Attachment attachment{};
        attachment.format = viewportRT->imageInfo.format;
        renderPass = RenderPass::Create(attachment);
    }

    void SSGI::CreateFrameBuffers()
    {
        framebuffers.resize(RHII.swapchain->images.size());
        for (size_t i = 0; i < RHII.swapchain->images.size(); ++i)
        {
            uint32_t width = viewportRT->imageInfo.width;
            uint32_t height = viewportRT->imageInfo.height;
            ImageViewHandle view = viewportRT->view;
            framebuffers[i] = FrameBuffer::Create(width, height, view, renderPass);
        }
    }

    void SSGI::CreatePipeline()
    {
        PipelineCreateInfo info{};
        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.vert", ShaderType::Vertex});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/SSGI/SSGI.frag", ShaderType::Fragment});
        info.width = viewportRT->width_f;
        info.height = viewportRT->height_f;
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {viewportRT->blendAttachment};
        info.pushConstantStage = PushConstantStage::Fragment;
        info.pushConstantSize = 5 * sizeof(vec4);
        info.descriptorSetLayouts = {Pipeline::getDescriptorSetLayoutSSGI()};
        info.renderPass = renderPass;

        pipeline = Pipeline::Create(info);

        info.pVertShader->Destroy();
        info.pFragShader->Destroy();
    }

    void SSGI::CreateUniforms()
    {
        DSet = Descriptor::Create(Pipeline::getDescriptorSetLayoutSSGI());
        UpdateDescriptorSets();
    }

    void SSGI::UpdateDescriptorSets()
    {
        std::array<DescriptorUpdateInfo, 2> infos{};
        infos[0].binding = 0;
        infos[0].pImage = frameImage;
        infos[1].binding = 1;
        infos[1].pImage = depth;
        infos[1].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        DSet->UpdateDescriptor(2, infos.data());
    }

    void SSGI::Update(Camera *camera)
    {
    }

    void SSGI::Draw(CommandBuffer *cmd, uint32_t imageIndex)
    {
        Camera &camera = *CONTEXT->GetSystem<CameraSystem>()->GetCamera(0);

        // Copy viewport image
        frameImage->CopyColorAttachment(cmd, viewportRT);

        // SCREEN SPACE GLOBAL ILLUMINATION
        // Input
        frameImage->ChangeLayout(cmd, LayoutState::ShaderReadOnly);
        depth->ChangeLayout(cmd, LayoutState::DepthStencilReadOnly);
        // Output
        viewportRT->ChangeLayout(cmd, LayoutState::ColorAttachment);

        cmd->BeginPass(renderPass, framebuffers[imageIndex]);
        cmd->PushConstants(pipeline, VK_SHADER_STAGE_FRAGMENT_BIT, 0, uint32_t(sizeof(mat4)), &camera.invViewProjection);
        cmd->BindPipeline(pipeline);
        cmd->BindDescriptors(pipeline, 1, &DSet);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
    }

    void SSGI::Resize(uint32_t width, uint32_t height)
    {
        for (auto *frameBuffer : framebuffers)
            frameBuffer->Destroy();
        renderPass->Destroy();
        pipeline->Destroy();
        frameImage->Destroy();

        Init();
        CreateRenderPass();
        CreateFrameBuffers();
        CreatePipeline();
        UpdateDescriptorSets();
    }

    void SSGI::Destroy()
    {
        Pipeline::getDescriptorSetLayoutSSGI()->Destroy();

        for (auto framebuffer : framebuffers)
            framebuffer->Destroy();

        renderPass->Destroy();
        pipeline->Destroy();
        frameImage->Destroy();
    }
}
