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
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {taaRT->blendAttachment};
        info.descriptorSetLayouts = {DSet->GetLayout()};
        info.dynamicColorTargets = 1;
        info.colorFormats = &taaRT->imageInfo.format;
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
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {viewportRT->blendAttachment};
        info.descriptorSetLayouts = {DSetSharpen->GetLayout()};
        info.dynamicColorTargets = 1;
        info.colorFormats = &viewportRT->imageInfo.format;
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

        AttachmentInfo info{};
        info.image = taaRT;

        cmd->BeginPass(1, &info);
        cmd->SetViewport(0.f, 0.f, taaRT->width_f, taaRT->height_f);
        cmd->SetScissor(0, 0, taaRT->imageInfo.width, taaRT->imageInfo.height);
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

        info.image = viewportRT;

        cmd->BeginPass(1, &info);
        cmd->SetViewport(0.f, 0.f, viewportRT->width_f, viewportRT->height_f);
        cmd->SetScissor(0, 0, viewportRT->imageInfo.width, viewportRT->imageInfo.height);
        cmd->BindPipeline(pipelineSharpen);
        cmd->BindDescriptors(pipelineSharpen, 1, &DSetSharpen);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
        cmd->EndDebugRegion();
    }

    void TAA::Resize(uint32_t width, uint32_t height)
    {
        Image::Destroy(previous);
        Image::Destroy(frameImage);
        Init();
        UpdateDescriptorSets();;
    }

    void TAA::Destroy()
    {
        Descriptor::Destroy(DSet);
        Descriptor::Destroy(DSetSharpen);

        Pipeline::Destroy(pipeline);
        Pipeline::Destroy(pipelineSharpen);

        Buffer::Destroy(uniform);

        Image::Destroy(previous);
        Image::Destroy(frameImage);
    }
}
