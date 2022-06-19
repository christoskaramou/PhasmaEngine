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

#include "SSR.h"
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
#include "Renderer/Buffer.h"
#include "Systems/CameraSystem.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    SSR::SSR()
    {
        DSet = {};
    }

    SSR::~SSR()
    {
    }

    void SSR::Init()
    {
        RendererSystem *rs = CONTEXT->GetSystem<RendererSystem>();
        Deferred *deferred = WORLD_ENTITY->GetComponent<Deferred>();

        ssrRT = rs->GetRenderTarget("ssr");
        albedoRT = rs->GetRenderTarget("albedo");
        normalRT = rs->GetRenderTarget("normal");
        srmRT = rs->GetRenderTarget("srm");
        depth = rs->GetDepthTarget("depth");
    }

    void SSR::CreateRenderPass()
    {
        Attachment attachment{};
        attachment.format = ssrRT->imageInfo.format;
        renderPass = RenderPass::Create(&attachment, 1, "ssr_renderpass");
    }

    void SSR::CreateFrameBuffers()
    {
        framebuffers.resize(SWAPCHAIN_IMAGES);
        for (size_t i = 0; i < SWAPCHAIN_IMAGES; ++i)
        {
            uint32_t width = ssrRT->imageInfo.width;
            uint32_t height = ssrRT->imageInfo.height;
            ImageViewHandle view = ssrRT->view;
            framebuffers[i] = FrameBuffer::Create(width, height, &view, 1, renderPass, "ssr_frameBuffer_" + std::to_string(i));
        }
    }

    void SSR::CreatePipeline()
    {
        PipelineCreateInfo info{};
        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.vert", ShaderStage::VertexBit});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/SSR/ssr.frag", ShaderStage::FragmentBit});
        info.width = ssrRT->width_f;
        info.height = ssrRT->height_f;
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {ssrRT->blendAttachment};
        info.descriptorSetLayouts = {DSet->GetLayout()};
        info.renderPass = renderPass;
        info.name = "ssr_pipeline";

        pipeline = Pipeline::Create(info);

        Shader::Destroy(info.pVertShader);
        Shader::Destroy(info.pFragShader);
    }

    void SSR::CreateUniforms(CommandBuffer *cmd)
    {
        UBReflection = Buffer::Create(
            4 * sizeof(mat4),
            BufferUsage::UniformBufferBit,
            AllocationCreate::HostAccessSequentialWriteBit,
            "SSR_UB_Reflection_buffer");
        UBReflection->Map();
        UBReflection->Zero();
        UBReflection->Flush();
        UBReflection->Unmap();

        DescriptorBindingInfo bindingInfos[5]{};

        bindingInfos[0].binding = 0;
        bindingInfos[0].type = DescriptorType::CombinedImageSampler;
        bindingInfos[0].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[0].pImage = albedoRT;

        bindingInfos[1].binding = 1;
        bindingInfos[1].type = DescriptorType::CombinedImageSampler;
        bindingInfos[1].imageLayout = ImageLayout::DepthStencilReadOnly;
        bindingInfos[1].pImage = depth;

        bindingInfos[2].binding = 2;
        bindingInfos[2].type = DescriptorType::CombinedImageSampler;
        bindingInfos[2].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[2].pImage = normalRT;

        bindingInfos[3].binding = 3;
        bindingInfos[3].type = DescriptorType::CombinedImageSampler;
        bindingInfos[3].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[3].pImage = srmRT;

        bindingInfos[4].binding = 4;
        bindingInfos[4].type = DescriptorType::UniformBuffer;
        bindingInfos[4].pBuffer = UBReflection;

        DescriptorInfo info{};
        info.count = 5;
        info.bindingInfos = bindingInfos;
        info.stage = ShaderStage::FragmentBit;

        DSet = Descriptor::Create(&info, "SSR_descriptor");
    }

    void SSR::UpdateDescriptorSets()
    {
        DescriptorBindingInfo bindingInfos[5]{};

        bindingInfos[0].binding = 0;
        bindingInfos[0].type = DescriptorType::CombinedImageSampler;
        bindingInfos[0].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[0].pImage = albedoRT;

        bindingInfos[1].binding = 1;
        bindingInfos[1].type = DescriptorType::CombinedImageSampler;
        bindingInfos[1].imageLayout = ImageLayout::DepthStencilReadOnly;
        bindingInfos[1].pImage = depth;

        bindingInfos[2].binding = 2;
        bindingInfos[2].type = DescriptorType::CombinedImageSampler;
        bindingInfos[2].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[2].pImage = normalRT;

        bindingInfos[3].binding = 3;
        bindingInfos[3].type = DescriptorType::CombinedImageSampler;
        bindingInfos[3].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[3].pImage = srmRT;

        bindingInfos[4].binding = 4;
        bindingInfos[4].type = DescriptorType::UniformBuffer;
        bindingInfos[4].pBuffer = UBReflection;

        DescriptorInfo info{};
        info.count = 5;
        info.bindingInfos = bindingInfos;
        info.stage = ShaderStage::FragmentBit;

        DSet->UpdateDescriptor(&info);
    }

    void SSR::Update(Camera *camera)
    {
        if (GUI::show_ssr)
        {
            reflectionInput[0][0] = vec4(camera->position, 1.0f);
            reflectionInput[0][1] = vec4(camera->front, 1.0f);
            reflectionInput[0][2] = vec4();
            reflectionInput[0][3] = vec4();
            reflectionInput[1] = camera->projection;
            reflectionInput[2] = camera->view;
            reflectionInput[3] = camera->invProjection;

            MemoryRange mr{};
            mr.data = &reflectionInput;
            mr.size = sizeof(reflectionInput);
            mr.offset = 0;
            UBReflection->Copy(1 ,&mr, false);
        }
    }

    void SSR::Draw(CommandBuffer *cmd, uint32_t imageIndex)
    {
        cmd->BeginDebugRegion("SSR");

        // SCREEN SPACE REFLECTION
        // Input
        cmd->ChangeLayout(albedoRT, ImageLayout::ShaderReadOnly);
        cmd->ChangeLayout(normalRT, ImageLayout::ShaderReadOnly);
        cmd->ChangeLayout(srmRT, ImageLayout::ShaderReadOnly);
        cmd->ChangeLayout(depth, ImageLayout::DepthStencilReadOnly);
        // Output
        cmd->ChangeLayout(ssrRT, ImageLayout::ColorAttachment);

        cmd->BeginPass(renderPass, framebuffers[imageIndex]);
        cmd->BindPipeline(pipeline);
        cmd->BindDescriptors(pipeline, 1, &DSet);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();

        cmd->ChangeLayout(ssrRT, ImageLayout::ShaderReadOnly);

        cmd->EndDebugRegion();
    }

    void SSR::Resize(uint32_t width, uint32_t height)
    {
        for (auto *frameBuffer : framebuffers)
            FrameBuffer::Destroy(frameBuffer);
        RenderPass::Destroy(renderPass);
        Pipeline::Destroy(pipeline);

        Init();
        CreateRenderPass();
        CreateFrameBuffers();
        UpdateDescriptorSets();
        CreatePipeline();
    }

    void SSR::Destroy()
    {
        Descriptor::Destroy(DSet);

        for (auto framebuffer : framebuffers)
            FrameBuffer::Destroy(framebuffer);

        RenderPass::Destroy(renderPass);
        Pipeline::Destroy(pipeline);
        Buffer::Destroy(UBReflection);
    }
}