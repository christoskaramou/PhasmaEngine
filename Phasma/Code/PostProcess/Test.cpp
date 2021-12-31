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

#if BINDLESS_TESTING
#include "Test.h"
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

namespace pe
{
    Test::Test()
    {
        DSet0 = {};
        DSet1 = {};
    }

    Test::~Test()
    {
    }

    Buffer *buffer = nullptr;
    void Test::Init()
    {
        vec4 color = vec4(1.0f, 0.0f, 1.0f, 1.0f);
        buffer = Buffer::Create(
            sizeof(vec4),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);
        buffer->Map();
        buffer->CopyData(&color, sizeof(vec4));
        buffer->Flush();
        buffer->Unmap();

        ImageCreateInfo info{};
        info.format = RHII.surface->format;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        info.width = static_cast<uint32_t>(WIDTH_f * GUI::renderTargetsScale);
        info.height = static_cast<uint32_t>(HEIGHT_f * GUI::renderTargetsScale);
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        info.properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        frameImage = Image::Create(info);

        ImageViewCreateInfo viewInfo{};
        viewInfo.image = frameImage;
        frameImage->CreateImageView(viewInfo);

        SamplerCreateInfo samplerInfo{};
        frameImage->CreateSampler(samplerInfo);

        frameImage->TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    void Test::createRenderPass(std::map<std::string, Image *> &renderTargets)
    {
        Attachment attachment{};
        attachment.format = renderTargets["viewport"]->imageInfo.format;
        renderPass = RenderPass::Create(attachment);
    }

    void Test::createFrameBuffers(std::map<std::string, Image *> &renderTargets)
    {
        framebuffers.resize(RHII.swapchain->images.size());
        for (size_t i = 0; i < RHII.swapchain->images.size(); ++i)
        {
            uint32_t width = renderTargets["viewport"]->imageInfo.width;
            uint32_t height = renderTargets["viewport"]->imageInfo.height;
            ImageViewHandle view = renderTargets["viewport"]->view;
            framebuffers[i] = FrameBuffer::Create(width, height, view, renderPass);
        }
    }

    static DescriptorLayout *s_bindlessLayout0 = nullptr;
    static DescriptorLayout *s_bindlessLayout1 = nullptr;
    static DescriptorLayout *s_bindlessLayout2 = nullptr;
    // static DescriptorLayout *s_bindlessLayout3 = nullptr;
    void Test::createUniforms(std::map<std::string, Image *> &renderTargets)
    {
        vec4 color = vec4(1.0f, 0.0f, 1.0f, 1.0f);
        uniform = Buffer::Create(
            sizeof(vec4),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);
        uniform->Map();
        uniform->CopyData(&color);
        uniform->Flush();
        uniform->Unmap();

        storage = Buffer::Create(
            sizeof(vec4),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);
        storage->Map();
        storage->CopyData(&color);
        storage->Flush();
        storage->Unmap();

        DSet0 = Descriptor::Create(s_bindlessLayout0, 2);
        DSet1 = Descriptor::Create(s_bindlessLayout1, 2);
        DSet2 = Descriptor::Create(s_bindlessLayout2, 2);

        updateDescriptorSets(renderTargets);
    }

    void Test::updateDescriptorSets(std::map<std::string, Image *> &renderTargets)
    {
        std::array<DescriptorUpdateInfo, 2> infos{};
        infos[0].binding = 0;
        infos[0].pImage = frameImage;
        infos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        infos[1].binding = 0;
        infos[1].pImage = RHII.depth;
        infos[1].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        DSet0->UpdateDescriptor(static_cast<uint32_t>(infos.size()), infos.data());

        std::array<DescriptorUpdateInfo, 1> bufInfos{};
        bufInfos[0].binding = 0;
        bufInfos[0].pBuffer = uniform;
        bufInfos[0].bufferUsage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        DSet1->UpdateDescriptor(static_cast<uint32_t>(bufInfos.size()), bufInfos.data());

        bufInfos[0].pBuffer = storage;
        bufInfos[0].bufferUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        DSet2->UpdateDescriptor(static_cast<uint32_t>(bufInfos.size()), bufInfos.data());
    }

    void Test::draw(CommandBuffer *cmd, uint32_t imageIndex, std::map<std::string, Image *> &renderTargets)
    {
        std::vector<Descriptor *> DSetHandles{DSet0, DSet1, DSet2};

        std::vector<float> values{GUI::DOF_focus_scale, GUI::DOF_blur_range, 0.0f, 0.0f};

        cmd->BeginPass(renderPass, framebuffers[imageIndex]);
        cmd->PushConstants(pipeline, VK_SHADER_STAGE_FRAGMENT_BIT, 0, uint32_t(sizeof(float) * values.size()),
                           values.data());
        cmd->BindPipeline(pipeline);
        cmd->BindDescriptors(pipeline, static_cast<uint32_t>(DSetHandles.size()), DSetHandles.data());
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
    }

    void Test::createPipeline(std::map<std::string, Image *> &renderTargets)
    {
        std::vector<DescriptorBinding> bindings(1);
        
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[0].descriptorCount = 2;
        s_bindlessLayout0 = DescriptorLayout::Create(bindings);

        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        s_bindlessLayout1 = DescriptorLayout::Create(bindings);

        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        s_bindlessLayout2 = DescriptorLayout::Create(bindings);

        Shader vert{"Shaders/Common/quad.vert", ShaderType::Vertex};
        Shader frag{"Shaders/Test/Test.frag", ShaderType::Fragment};

        PipelineCreateInfo info{};
        info.pVertShader = &vert;
        info.pFragShader = &frag;
        info.width = renderTargets["viewport"]->width_f;
        info.height = renderTargets["viewport"]->height_f;
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {renderTargets["viewport"]->blendAttachment};
        info.pushConstantStage = PushConstantStage::Fragment;
        info.pushConstantSize = 5 * sizeof(vec4);
        info.descriptorSetLayouts = {s_bindlessLayout0, s_bindlessLayout1, s_bindlessLayout2};
        info.renderPass = renderPass;

        pipeline = Pipeline::Create(info);
    }

    void Test::destroy()
    {
        for (auto framebuffer : framebuffers)
            framebuffer->Destroy();

        renderPass->Destroy();
        Pipeline::getDescriptorSetLayoutDOF()->Destroy();
        frameImage->Destroy();
        pipeline->Destroy();
    }
}
#endif
