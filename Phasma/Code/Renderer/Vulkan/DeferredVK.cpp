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

#if PE_VULKAN
#include "Renderer/Deferred.h"
#include "Model/Model.h"
#include "Model/Mesh.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Surface.h"
#include "Shader/Shader.h"
#include "GUI/GUI.h"
#include "tinygltf/stb_image.h"
#include "Shader/Reflection.h"
#include "Renderer/RHI.h"
#include "Renderer/Command.h"
#include "Renderer/Descriptor.h"
#include "Systems/LightSystem.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/Image.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Buffer.h"
#include "Renderer/Pipeline.h"
#include "Renderer/Queue.h"
#include "Systems/RendererSystem.h"
#include "PostProcess/SSAO.h"
#include "PostProcess/SSR.h"

namespace pe
{
    Deferred::Deferred()
    {
        DSComposition = {};
    }

    Deferred::~Deferred()
    {
    }

    void Deferred::Init()
    {
        RendererSystem *rs = CONTEXT->GetSystem<RendererSystem>();

        normalRT = rs->GetRenderTarget("normal");
        albedoRT = rs->GetRenderTarget("albedo");
        srmRT = rs->GetRenderTarget("srm"); // Specular Roughness Metallic
        velocityRT = rs->GetRenderTarget("velocity");
        emissiveRT = rs->GetRenderTarget("emissive");
        viewportRT = rs->GetRenderTarget("viewport");
        depth = rs->GetDepthTarget("depth");
        ssaoBlurRT = rs->GetRenderTarget("ssaoBlur");
        ssrRT = rs->GetRenderTarget("ssr");
    }

    void Deferred::CreateRenderPass()
    {
        Attachment attachments[6];

        // Target attachments are initialized to match render targets by default
        attachments[0].format = normalRT->imageInfo.format;
        attachments[1].format = albedoRT->imageInfo.format;
        attachments[2].format = srmRT->imageInfo.format;
        attachments[3].format = velocityRT->imageInfo.format;
        attachments[4].format = emissiveRT->imageInfo.format;

        // Depth
        attachments[5].flags = {};
        attachments[5].format = depth->imageInfo.format;
        attachments[5].samples = SampleCount::Count1;
        attachments[5].loadOp = AttachmentLoadOp::Clear;
        attachments[5].storeOp = AttachmentStoreOp::Store;
        attachments[5].stencilLoadOp = AttachmentLoadOp::DontCare;
        attachments[5].stencilStoreOp = AttachmentStoreOp::Store;
        attachments[5].initialLayout = ImageLayout::Undefined;
        attachments[5].finalLayout = ImageLayout::DepthStencilAttachment;

        renderPass = RenderPass::Create(attachments, 6, "gbuffer_renderpass");

        Attachment attachment{};
        attachment.format = viewportRT->imageInfo.format;
        compositionRenderPass = RenderPass::Create(&attachment, 1, "composition_renderpass");
    }

    void Deferred::CreateFrameBuffers()
    {
        CreateGBufferFrameBuffers();
        CreateCompositionFrameBuffers();
    }

    void Deferred::CreateGBufferFrameBuffers()
    {
        framebuffers.resize(SWAPCHAIN_IMAGES);
        for (size_t i = 0; i < SWAPCHAIN_IMAGES; ++i)
        {
            uint32_t width = albedoRT->imageInfo.width;
            uint32_t height = albedoRT->imageInfo.height;
            std::array<ImageViewHandle, 6> views{
                normalRT->view,
                albedoRT->view,
                srmRT->view,
                velocityRT->view,
                emissiveRT->view,
                depth->view};
            framebuffers[i] = FrameBuffer::Create(width, height, views.data(), 6, renderPass, "gbuffer_frameBuffer_" + std::to_string(i));
        }
    }

    void Deferred::CreateCompositionFrameBuffers()
    {
        compositionFramebuffers.resize(SWAPCHAIN_IMAGES);
        for (size_t i = 0; i < SWAPCHAIN_IMAGES; ++i)
        {
            uint32_t width = viewportRT->imageInfo.width;
            uint32_t height = viewportRT->imageInfo.height;
            ImageViewHandle view = viewportRT->view;
            compositionFramebuffers[i] = FrameBuffer::Create(width, height, &view, 1, compositionRenderPass, "composition_frameBuffer_" + std::to_string(i));
        }
    }

    void Deferred::CreatePipeline()
    {
        Shadows &shadows = *WORLD_ENTITY->GetComponent<Shadows>();
        SkyBox &skybox = CONTEXT->GetSystem<RendererSystem>()->skyBoxDay;

        const std::vector<Define> definesFrag{
            Define{"SHADOWMAP_CASCADES", std::to_string(SHADOWMAP_CASCADES)},
            Define{"SHADOWMAP_SIZE", std::to_string((float)SHADOWMAP_SIZE)},
            Define{"SHADOWMAP_TEXEL_SIZE", std::to_string(1.0f / (float)SHADOWMAP_SIZE)},
            Define{"MAX_POINT_LIGHTS", std::to_string(MAX_POINT_LIGHTS)},
            Define{"MAX_SPOT_LIGHTS", std::to_string(MAX_SPOT_LIGHTS)}};

        PipelineCreateInfo info{};
        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.vert", ShaderStage::VertexBit});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/Deferred/composition.frag", ShaderStage::FragmentBit, definesFrag});
        info.width = viewportRT->width_f;
        info.height = viewportRT->height_f;
        info.pushConstantStage = ShaderStage::FragmentBit;
        info.pushConstantSize = sizeof(mat4);
        info.colorBlendAttachments = {viewportRT->blendAttachment};
        info.descriptorSetLayouts = {
            DSComposition->GetLayout(),
            shadows.descriptorSetDeferred->GetLayout(),
            skybox.descriptorSet->GetLayout()};
        info.renderPass = compositionRenderPass;
        info.name = "composition_pipeline";

        pipelineComposition = Pipeline::Create(info);

        Shader::Destroy(info.pVertShader);
        Shader::Destroy(info.pFragShader);
    }

    void Deferred::CreateUniforms(CommandBuffer *cmd)
    {
        const std::string path = Path::Assets + "Objects/ibl_brdf_lut.png";
        if (Mesh::uniqueTextures.find(path) != Mesh::uniqueTextures.end())
        {
            ibl_brdf_lut = Mesh::uniqueTextures[path];
        }
        else
        {
            int texWidth, texHeight, texChannels;
            stbi_set_flip_vertically_on_load(true);
            unsigned char *pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
            stbi_set_flip_vertically_on_load(false);

            if (!pixels)
                PE_ERROR("No pixel data loaded");

            ImageCreateInfo info{};
            info.format = Format::RGBA8Unorm;
            info.mipLevels = static_cast<uint32_t>(std::floor(std::log2(texWidth > texHeight ? texWidth : texHeight))) + 1;
            info.width = texWidth;
            info.height = texHeight;
            info.usage = ImageUsage::TransferSrcBit | ImageUsage::TransferDstBit | ImageUsage::SampledBit;
            info.properties = MemoryProperty::DeviceLocalBit;
            info.initialLayout = ImageLayout::Preinitialized;
            info.name = "Deferred_ibl_brdf_lut_image";
            ibl_brdf_lut = Image::Create(info);

            ImageViewCreateInfo viewInfo{};
            viewInfo.image = ibl_brdf_lut;
            ibl_brdf_lut->CreateImageView(viewInfo);

            SamplerCreateInfo samplerInfo{};
            samplerInfo.maxLod = static_cast<float>(info.mipLevels);
            ibl_brdf_lut->CreateSampler(samplerInfo);

            cmd->CopyDataToImageStaged(ibl_brdf_lut, pixels, texWidth * texHeight * STBI_rgb_alpha);
            cmd->GenerateMipMaps(ibl_brdf_lut);

            Mesh::uniqueTextures[path] = ibl_brdf_lut;
            cmd->AfterWaitCallback([pixels]()
                                     { stbi_image_free(pixels); });
        }

        uniform = Buffer::Create(
            sizeof(ubo),
            BufferUsage::UniformBufferBit,
            AllocationCreate::HostAccessSequentialWriteBit,
            "Deferred_uniform_buffer");
        uniform->Map();
        uniform->Zero();
        uniform->Flush();
        uniform->Unmap();

        DescriptorBindingInfo bindingInfos[10]{};

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
        bindingInfos[2].pImage = albedoRT;

        bindingInfos[3].binding = 3;
        bindingInfos[3].type = DescriptorType::CombinedImageSampler;
        bindingInfos[3].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[3].pImage = srmRT;

        bindingInfos[4].binding = 4;
        bindingInfos[4].type = DescriptorType::UniformBuffer;
        bindingInfos[4].pBuffer = &CONTEXT->GetSystem<LightSystem>()->GetUniform();

        bindingInfos[5].binding = 5;
        bindingInfos[5].type = DescriptorType::CombinedImageSampler;
        bindingInfos[5].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[5].pImage = ssaoBlurRT;

        bindingInfos[6].binding = 6;
        bindingInfos[6].type = DescriptorType::CombinedImageSampler;
        bindingInfos[6].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[6].pImage = ssrRT;

        bindingInfos[7].binding = 7;
        bindingInfos[7].type = DescriptorType::CombinedImageSampler;
        bindingInfos[7].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[7].pImage = emissiveRT;

        bindingInfos[8].binding = 8;
        bindingInfos[8].type = DescriptorType::CombinedImageSampler;
        bindingInfos[8].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[8].pImage = ibl_brdf_lut;

        bindingInfos[9].binding = 9;
        bindingInfos[9].type = DescriptorType::UniformBuffer;
        bindingInfos[9].pBuffer = uniform;

        DescriptorInfo info{};
        info.count = 10;
        info.bindingInfos = bindingInfos;
        info.stage = ShaderStage::FragmentBit;

        DSComposition = Descriptor::Create(&info, "Deferred_Composition_descriptor");
    }

    void Deferred::UpdateDescriptorSets()
    {
        DescriptorBindingInfo bindingInfos[10]{};

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
        bindingInfos[2].pImage = albedoRT;

        bindingInfos[3].binding = 3;
        bindingInfos[3].type = DescriptorType::CombinedImageSampler;
        bindingInfos[3].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[3].pImage = srmRT;

        bindingInfos[4].binding = 4;
        bindingInfos[4].type = DescriptorType::UniformBuffer;
        bindingInfos[4].pBuffer = &CONTEXT->GetSystem<LightSystem>()->GetUniform();

        bindingInfos[5].binding = 5;
        bindingInfos[5].type = DescriptorType::CombinedImageSampler;
        bindingInfos[5].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[5].pImage = ssaoBlurRT;

        bindingInfos[6].binding = 6;
        bindingInfos[6].type = DescriptorType::CombinedImageSampler;
        bindingInfos[6].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[6].pImage = ssrRT;

        bindingInfos[7].binding = 7;
        bindingInfos[7].type = DescriptorType::CombinedImageSampler;
        bindingInfos[7].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[7].pImage = emissiveRT;

        bindingInfos[8].binding = 8;
        bindingInfos[8].type = DescriptorType::CombinedImageSampler;
        bindingInfos[8].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[8].pImage = ibl_brdf_lut;

        bindingInfos[9].binding = 9;
        bindingInfos[9].type = DescriptorType::UniformBuffer;
        bindingInfos[9].pBuffer = uniform;

        DescriptorInfo info{};
        info.count = 10;
        info.bindingInfos = bindingInfos;
        info.stage = ShaderStage::FragmentBit;

        DSComposition->UpdateDescriptor(&info);
    }

    void Deferred::Update(Camera *camera)
    {
        ubo.screenSpace[0] = {camera->invViewProjection[0]};
        ubo.screenSpace[1] = {camera->invViewProjection[1]};
        ubo.screenSpace[2] = {camera->invViewProjection[2]};
        ubo.screenSpace[3] = {camera->invViewProjection[3]};
        ubo.screenSpace[4] = {
            static_cast<float>(GUI::show_ssao), static_cast<float>(GUI::show_ssr),
            static_cast<float>(GUI::show_tonemapping), static_cast<float>(GUI::use_AntiAliasing)};
        ubo.screenSpace[5] = {
            static_cast<float>(GUI::use_IBL), static_cast<float>(GUI::use_Volumetric_lights),
            static_cast<float>(GUI::volumetric_steps), static_cast<float>(GUI::volumetric_dither_strength)};
        ubo.screenSpace[6] = {GUI::fog_global_thickness, GUI::lights_intensity, GUI::lights_range, GUI::fog_max_height};
        ubo.screenSpace[7] = {
            GUI::fog_ground_thickness, static_cast<float>(GUI::use_fog), static_cast<float>(GUI::shadow_cast), 0.0f};

        MemoryRange mr{};
        mr.data = &ubo;
        mr.size = sizeof(ubo);
        uniform->Copy(1, &mr, false);
    }

    void Deferred::Draw(CommandBuffer *cmd, uint32_t imageIndex)
    {
        cmd->BeginDebugRegion("Composition");

        Shadows &shadows = *WORLD_ENTITY->GetComponent<Shadows>();
        RendererSystem &rs = *CONTEXT->GetSystem<RendererSystem>();

        // SKYBOX
        SkyBox &skybox = GUI::shadow_cast ? rs.skyBoxDay : rs.skyBoxNight;

        mat4 values{};
        values[0] = shadows.viewZ;
        values[1] = vec4(GUI::shadow_cast);
        std::vector<Descriptor *> handles{DSComposition, shadows.descriptorSetDeferred, skybox.descriptorSet};

        // COMBINE
        // Input
        cmd->ImageBarrier(normalRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(albedoRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(srmRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(velocityRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(emissiveRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(ssaoBlurRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(ssrRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(ibl_brdf_lut, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(skybox.texture, ImageLayout::ShaderReadOnly, 0, skybox.texture->imageInfo.arrayLayers);
        cmd->ImageBarrier(depth, ImageLayout::DepthStencilReadOnly);
        for (auto &shadowMap : shadows.textures)
            cmd->ImageBarrier(shadowMap, ImageLayout::DepthStencilReadOnly);
        // Output
        cmd->ImageBarrier(viewportRT, ImageLayout::ColorAttachment);

        cmd->BeginPass(compositionRenderPass, compositionFramebuffers[imageIndex]);
        cmd->PushConstants(pipelineComposition, ShaderStage::FragmentBit, 0, sizeof(mat4), &values);
        cmd->BindPipeline(pipelineComposition);
        cmd->BindDescriptors(pipelineComposition, (uint32_t)handles.size(), handles.data());
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();

        cmd->EndDebugRegion();
    }

    void Deferred::Resize(uint32_t width, uint32_t height)
    {
        for (auto *framebuffer : framebuffers)
            FrameBuffer::Destroy(framebuffer);
        for (auto *framebuffer : compositionFramebuffers)
            FrameBuffer::Destroy(framebuffer);

        RenderPass::Destroy(renderPass);
        RenderPass::Destroy(compositionRenderPass);

        Pipeline::Destroy(pipelineComposition);

        Init();
        CreateRenderPass();
        CreateFrameBuffers();
        UpdateDescriptorSets();
        CreatePipeline();
    }

    void Deferred::BeginPass(CommandBuffer *cmd, uint32_t imageIndex)
    {
        cmd->ImageBarrier(albedoRT, ImageLayout::ColorAttachment);
        cmd->ImageBarrier(normalRT, ImageLayout::ColorAttachment);
        cmd->ImageBarrier(velocityRT, ImageLayout::ColorAttachment);
        cmd->ImageBarrier(emissiveRT, ImageLayout::ColorAttachment);
        cmd->ImageBarrier(srmRT, ImageLayout::ColorAttachment); // TODO: Check why it throws
        cmd->ImageBarrier(depth, ImageLayout::DepthStencilAttachment);

        cmd->BeginPass(renderPass, framebuffers[imageIndex]);
    }

    void Deferred::EndPass(CommandBuffer *cmd)
    {
        cmd->EndPass();

        cmd->ImageBarrier(albedoRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(normalRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(velocityRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(emissiveRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(srmRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(depth, ImageLayout::DepthStencilReadOnly);
    }

    void Deferred::Destroy()
    {
        RenderPass::Destroy(renderPass);
        RenderPass::Destroy(compositionRenderPass);

        for (auto &framebuffer : framebuffers)
            FrameBuffer::Destroy(framebuffer);
        for (auto &framebuffer : compositionFramebuffers)
            FrameBuffer::Destroy(framebuffer);

        Descriptor::Destroy(DSComposition);
        Buffer::Destroy(uniform);
        Pipeline::Destroy(pipelineComposition);
    }
}
#endif
