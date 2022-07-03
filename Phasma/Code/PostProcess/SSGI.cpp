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

    void SSGI::CreatePipeline()
    {
        PipelineCreateInfo info{};
        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.vert", ShaderStage::VertexBit});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/SSGI/SSGI.frag", ShaderStage::FragmentBit});
        info.width = viewportRT->width_f;
        info.height = viewportRT->height_f;
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {viewportRT->blendAttachment};
        info.pushConstantStage = ShaderStage::FragmentBit;
        info.pushConstantSize = 5 * sizeof(vec4);
        info.descriptorSetLayouts = {DSet->GetLayout()};
        info.dynamicColorTargets = 1;
        info.colorFormats = &viewportRT->imageInfo.format;
        info.name = "ssgi_pipeline";

        pipeline = Pipeline::Create(info);

        Shader::Destroy(info.pVertShader);
        Shader::Destroy(info.pFragShader);
    }

    void SSGI::CreateUniforms(CommandBuffer *cmd)
    {
        DescriptorBindingInfo bindingInfos[2]{};

        bindingInfos[0].binding = 0;
        bindingInfos[0].type = DescriptorType::CombinedImageSampler;
        bindingInfos[0].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[0].pImage = frameImage;

        bindingInfos[1].binding = 1;
        bindingInfos[1].type = DescriptorType::CombinedImageSampler;
        bindingInfos[1].imageLayout = ImageLayout::DepthStencilReadOnly;
        bindingInfos[1].pImage = depth;

        DescriptorInfo info{};
        info.count = 2;
        info.bindingInfos = bindingInfos;
        info.stage = ShaderStage::FragmentBit;

        DSet = Descriptor::Create(&info, "SSGI_descriptor");
    }

    void SSGI::UpdateDescriptorSets()
    {
        DescriptorBindingInfo bindingInfos[2]{};

        bindingInfos[0].binding = 0;
        bindingInfos[0].type = DescriptorType::CombinedImageSampler;
        bindingInfos[0].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[0].pImage = frameImage;

        bindingInfos[1].binding = 1;
        bindingInfos[1].type = DescriptorType::CombinedImageSampler;
        bindingInfos[1].imageLayout = ImageLayout::DepthStencilReadOnly;
        bindingInfos[1].pImage = depth;

        DescriptorInfo info{};
        info.count = 2;
        info.bindingInfos = bindingInfos;
        info.stage = ShaderStage::FragmentBit;

        DSet->UpdateDescriptor(&info);
    }

    void SSGI::Update(Camera *camera)
    {
    }

    void SSGI::Draw(CommandBuffer *cmd, uint32_t imageIndex)
    {
        cmd->BeginDebugRegion("SSGI");

        Camera &camera = *CONTEXT->GetSystem<CameraSystem>()->GetCamera(0);

        // Copy viewport image
        cmd->CopyImage(viewportRT, frameImage);

        // SCREEN SPACE GLOBAL ILLUMINATION
        // Input
        cmd->ImageBarrier(frameImage, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(depth, ImageLayout::DepthStencilReadOnly);
        // Output
        cmd->ImageBarrier(viewportRT, ImageLayout::ColorAttachment);

        AttachmentInfo info{};
        info.image = viewportRT;

        cmd->BeginPass(1, &info);
        cmd->PushConstants(pipeline, ShaderStage::FragmentBit, 0, uint32_t(sizeof(mat4)), &camera.invViewProjection);
        cmd->BindPipeline(pipeline);
        cmd->BindDescriptors(pipeline, 1, &DSet);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();

        cmd->EndDebugRegion();
    }

    void SSGI::Resize(uint32_t width, uint32_t height)
    {
        Pipeline::Destroy(pipeline);
        Image::Destroy(frameImage);

        Init();
        UpdateDescriptorSets();
        CreatePipeline();
    }

    void SSGI::Destroy()
    {
        Descriptor::Destroy(DSet);
        Pipeline::Destroy(pipeline);
        Image::Destroy(frameImage);
    }
}
