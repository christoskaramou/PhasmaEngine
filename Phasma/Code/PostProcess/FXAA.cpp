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

#include "FXAA.h"
#include "GUI/GUI.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Surface.h"
#include "Shader/Shader.h"
#include "Renderer/RHI.h"
#include "Renderer/Command.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/Image.h"
#include "Renderer/Pipeline.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    FXAA::FXAA()
    {
        DSet = {};
    }

    FXAA::~FXAA()
    {
    }

    void FXAA::Init()
    {
        RendererSystem *rs = CONTEXT->GetSystem<RendererSystem>();

        viewportRT = rs->GetRenderTarget("viewport");
        frameImage = rs->CreateFSSampledImage();
    }

    void FXAA::CreatePipeline()
    {
        PipelineCreateInfo info{};
        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.vert", ShaderStage::VertexBit});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/FXAA/FXAA.frag", ShaderStage::FragmentBit});
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {viewportRT->blendAttachment};
        info.descriptorSetLayouts = {DSet->GetLayout()};
        info.dynamicColorTargets = 1;
        info.colorFormats = &viewportRT->imageInfo.format;
        info.name = "fxaa_pipeline";

        pipeline = Pipeline::Create(info);

        Shader::Destroy(info.pVertShader);
        Shader::Destroy(info.pFragShader);
    }

    void FXAA::CreateUniforms(CommandBuffer *cmd)
    {
        DescriptorBindingInfo bindingInfo{};
        bindingInfo.binding = 0;
        bindingInfo.type = DescriptorType::CombinedImageSampler;
        bindingInfo.imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfo.pImage = frameImage;

        DescriptorInfo info{};
        info.count = 1;
        info.bindingInfos = &bindingInfo;
        info.stage = ShaderStage::FragmentBit;

        DSet = Descriptor::Create(&info, "FXAA_descriptor");
    }

    void FXAA::UpdateDescriptorSets()
    {
        DescriptorBindingInfo bindingInfo{};
        bindingInfo.binding = 0;
        bindingInfo.type = DescriptorType::CombinedImageSampler;
        bindingInfo.imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfo.pImage = frameImage;

        DescriptorInfo info{};
        info.count = 1;
        info.bindingInfos = &bindingInfo;
        info.stage = ShaderStage::FragmentBit;

        DSet->UpdateDescriptor(&info);
    }

    void FXAA::Update(Camera *camera)
    {
    }

    void FXAA::Draw(CommandBuffer *cmd, uint32_t imageIndex)
    {
        cmd->BeginDebugRegion("FXAA");
        // Copy viewport image
        cmd->CopyImage(viewportRT, frameImage);

        // FAST APPROXIMATE ANTI-ALIASING
        // Input
        cmd->ImageBarrier(frameImage, ImageLayout::ShaderReadOnly);
        // Output
        cmd->ImageBarrier(viewportRT, ImageLayout::ColorAttachment);

        AttachmentInfo info{};
        info.image = viewportRT;

        cmd->BeginPass(1, &info);
        cmd->SetViewport(0.f, 0.f, viewportRT->width_f, viewportRT->height_f);
        cmd->SetScissor(0, 0, viewportRT->imageInfo.width, viewportRT->imageInfo.height);
        cmd->BindPipeline(pipeline);
        cmd->BindDescriptors(pipeline, 1, &DSet);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
        cmd->EndDebugRegion();
    }

    void FXAA::Resize(uint32_t width, uint32_t height)
    {
        Image::Destroy(frameImage);
        Init();
        UpdateDescriptorSets();
    }

    void FXAA::Destroy()
    {
        Descriptor::Destroy(DSet);
        Pipeline::Destroy(pipeline);
        Image::Destroy(frameImage);
    }
}
