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
        attachments[5].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[5].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[5].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[5].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[5].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[5].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[5].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

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
        CreateGBufferPipeline();
        CreateCompositionPipeline();
    }

    void Deferred::CreateGBufferPipeline()
    {
        PipelineCreateInfo info{};
        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Deferred/gBuffer.vert", ShaderType::Vertex});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/Deferred/gBuffer.frag", ShaderType::Fragment});
        info.vertexInputBindingDescriptions = info.pVertShader->GetReflection().GetVertexBindings();
        info.vertexInputAttributeDescriptions = info.pVertShader->GetReflection().GetVertexAttributes();
        info.width = albedoRT->width_f;
        info.height = albedoRT->height_f;
        info.pushConstantStage = PushConstantStage::VertexAndFragment;
        info.pushConstantSize = 5 * sizeof(uint32_t);
        info.cullMode = CullMode::Front;
        info.colorBlendAttachments = {
            normalRT->blendAttachment,
            albedoRT->blendAttachment,
            srmRT->blendAttachment,
            velocityRT->blendAttachment,
            emissiveRT->blendAttachment,
        };

        info.descriptorSetLayouts = {
            Pipeline::getDescriptorSetLayoutGbufferVert(),
            Pipeline::getDescriptorSetLayoutGbufferFrag()};
        info.renderPass = renderPass;
        info.name = "gbuffer_pipeline";

        pipeline = Pipeline::Create(info);

        Shader::Destroy(info.pVertShader);
        Shader::Destroy(info.pFragShader);
    }

    void Deferred::CreateCompositionPipeline()
    {
        const std::vector<Define> definesFrag{
            Define{"SHADOWMAP_CASCADES", std::to_string(SHADOWMAP_CASCADES)},
            Define{"SHADOWMAP_SIZE", std::to_string((float)SHADOWMAP_SIZE)},
            Define{"SHADOWMAP_TEXEL_SIZE", std::to_string(1.0f / (float)SHADOWMAP_SIZE)},
            Define{"MAX_POINT_LIGHTS", std::to_string(MAX_POINT_LIGHTS)},
            Define{"MAX_SPOT_LIGHTS", std::to_string(MAX_SPOT_LIGHTS)}};

        PipelineCreateInfo info{};
        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.vert", ShaderType::Vertex});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/Deferred/composition.frag", ShaderType::Fragment, definesFrag});
        info.width = viewportRT->width_f;
        info.height = viewportRT->height_f;
        info.pushConstantStage = PushConstantStage::Fragment;
        info.pushConstantSize = sizeof(mat4);
        info.colorBlendAttachments = {viewportRT->blendAttachment};
        info.descriptorSetLayouts = {
            Pipeline::getDescriptorSetLayoutComposition(),
            Pipeline::getDescriptorSetLayoutShadowsDeferred(),
            Pipeline::getDescriptorSetLayoutSkybox()};
        info.renderPass = compositionRenderPass;
        info.name = "composition_pipeline";

        pipelineComposition = Pipeline::Create(info);

        Shader::Destroy(info.pVertShader);
        Shader::Destroy(info.pFragShader);
    }

    void Deferred::CreateUniforms()
    {
        uniform = Buffer::Create(
            sizeof(ubo),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            "Deferred_uniform_buffer");
        uniform->Map();
        uniform->Zero();
        uniform->Flush();
        uniform->Unmap();

        DSComposition = Descriptor::Create(Pipeline::getDescriptorSetLayoutComposition(), 1, "Deferred_Composition_descriptor");

        // Check if ibl_brdf_lut is already loaded
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
            const VkDeviceSize imageSize = texWidth * texHeight * STBI_rgb_alpha;

            Buffer *staging = Buffer::Create(
                imageSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                "Deferred_ibl_brdf_lut_staging_buffer");
            staging->Map();
            staging->CopyData(pixels);
            staging->Flush();
            staging->Unmap();

            stbi_image_free(pixels);

            ImageCreateInfo info{};
            info.format = (Format)VK_FORMAT_R8G8B8A8_UNORM;
            info.mipLevels = static_cast<uint32_t>(std::floor(std::log2(texWidth > texHeight ? texWidth : texHeight))) + 1;
            info.width = texWidth;
            info.height = texHeight;
            info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            info.properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            info.initialState = LayoutState::Preinitialized;
            info.name = "Deferred_ibl_brdf_lut_image";
            ibl_brdf_lut = Image::Create(info);

            ImageViewCreateInfo viewInfo{};
            viewInfo.image = ibl_brdf_lut;
            ibl_brdf_lut->CreateImageView(viewInfo);

            SamplerCreateInfo samplerInfo{};
            samplerInfo.maxLod = static_cast<float>(info.mipLevels);
            ibl_brdf_lut->CreateSampler(samplerInfo);

            ibl_brdf_lut->CopyBufferToImage(nullptr, staging);
            ibl_brdf_lut->GenerateMipMaps();

            Buffer::Destroy(staging);

            Mesh::uniqueTextures[path] = ibl_brdf_lut;
        }

        UpdateDescriptorSets();
    }

    void Deferred::UpdateDescriptorSets()
    {
        std::array<DescriptorUpdateInfo, 10> infos{};

        infos[0].binding = 0;
        infos[0].pImage = depth;
        infos[0].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        infos[1].binding = 1;
        infos[1].pImage = normalRT;

        infos[2].binding = 2;
        infos[2].pImage = albedoRT;

        infos[3].binding = 3;
        infos[3].pImage = srmRT;

        infos[4].binding = 4;
        infos[4].pBuffer = &CONTEXT->GetSystem<LightSystem>()->GetUniform();

        infos[5].binding = 5;
        infos[5].pImage = ssaoBlurRT;

        infos[6].binding = 6;
        infos[6].pImage = ssrRT;

        infos[7].binding = 7;
        infos[7].pImage = emissiveRT;

        infos[8].binding = 8;
        infos[8].pImage = ibl_brdf_lut;

        infos[9].binding = 9;
        infos[9].pBuffer = uniform;

        DSComposition->UpdateDescriptor(10, infos.data());
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

        MemoryRange range{};
        range.data = &ubo;
        range.size = sizeof(ubo);
        range.offset = 0;
        uniform->Copy(&range, 1, false);
    }

    void Deferred::Draw(CommandBuffer *cmd, uint32_t imageIndex)
    {
        Debug::BeginCmdRegion(cmd->Handle(), "Composition");

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
        normalRT->ChangeLayout(cmd, LayoutState::ShaderReadOnly);
        albedoRT->ChangeLayout(cmd, LayoutState::ShaderReadOnly);
        srmRT->ChangeLayout(cmd, LayoutState::ShaderReadOnly);
        velocityRT->ChangeLayout(cmd, LayoutState::ShaderReadOnly);
        emissiveRT->ChangeLayout(cmd, LayoutState::ShaderReadOnly);
        ssaoBlurRT->ChangeLayout(cmd, LayoutState::ShaderReadOnly);
        ssrRT->ChangeLayout(cmd, LayoutState::ShaderReadOnly);
        ibl_brdf_lut->ChangeLayout(cmd, LayoutState::ShaderReadOnly);
        skybox.texture->ChangeLayout(cmd, LayoutState::ShaderReadOnly, 0, skybox.texture->imageInfo.arrayLayers);
        depth->ChangeLayout(cmd, LayoutState::DepthStencilReadOnly);
        for (auto &shadowMap : shadows.textures)
            shadowMap->ChangeLayout(cmd, LayoutState::DepthStencilReadOnly);
        // Output
        viewportRT->ChangeLayout(cmd, LayoutState::ColorAttachment);

        cmd->BeginPass(compositionRenderPass, compositionFramebuffers[imageIndex]);
        cmd->PushConstants(pipelineComposition, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(mat4), &values);
        cmd->BindPipeline(pipelineComposition);
        cmd->BindDescriptors(pipelineComposition, (uint32_t)handles.size(), handles.data());
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();

        Debug::EndCmdRegion(cmd->Handle());
    }

    void Deferred::Resize(uint32_t width, uint32_t height)
    {
        for (auto *framebuffer : framebuffers)
            FrameBuffer::Destroy(framebuffer);
        for (auto *framebuffer : compositionFramebuffers)
            FrameBuffer::Destroy(framebuffer);

        RenderPass::Destroy(renderPass);
        RenderPass::Destroy(compositionRenderPass);

        Pipeline::Destroy(pipeline);
        Pipeline::Destroy(pipelineComposition);

        Init();
        CreateRenderPass();
        CreateFrameBuffers();
        CreatePipeline();
        UpdateDescriptorSets();
    }

    void Deferred::BeginPass(CommandBuffer *cmd, uint32_t imageIndex)
    {
        albedoRT->ChangeLayout(cmd, LayoutState::ColorAttachment);
        normalRT->ChangeLayout(cmd, LayoutState::ColorAttachment);
        velocityRT->ChangeLayout(cmd, LayoutState::ColorAttachment);
        emissiveRT->ChangeLayout(cmd, LayoutState::ColorAttachment);
        srmRT->ChangeLayout(cmd, LayoutState::ColorAttachment); // TODO: Check why it throws
        depth->ChangeLayout(cmd, LayoutState::DepthStencilAttachment);

        cmd->BeginPass(renderPass, framebuffers[imageIndex]);

        Model::commandBuffer = cmd;
        Model::pipeline = pipeline;
    }

    void Deferred::EndPass(CommandBuffer *cmd)
    {
        Model::commandBuffer->EndPass();

        albedoRT->ChangeLayout(cmd, LayoutState::ShaderReadOnly);
        normalRT->ChangeLayout(cmd, LayoutState::ShaderReadOnly);
        velocityRT->ChangeLayout(cmd, LayoutState::ShaderReadOnly);
        emissiveRT->ChangeLayout(cmd, LayoutState::ShaderReadOnly);
        srmRT->ChangeLayout(cmd, LayoutState::ShaderReadOnly);
        depth->ChangeLayout(cmd, LayoutState::DepthStencilReadOnly);

        Model::commandBuffer = nullptr;
        Model::pipeline = nullptr;
    }

    void Deferred::Destroy()
    {
        RenderPass::Destroy(renderPass);
        RenderPass::Destroy(compositionRenderPass);

        for (auto &framebuffer : framebuffers)
            FrameBuffer::Destroy(framebuffer);
        for (auto &framebuffer : compositionFramebuffers)
            FrameBuffer::Destroy(framebuffer);

        DescriptorLayout::Destroy(Pipeline::getDescriptorSetLayoutComposition());
        Buffer::Destroy(uniform);
        Pipeline::Destroy(pipeline);
        Pipeline::Destroy(pipelineComposition);
    }
}
#endif
