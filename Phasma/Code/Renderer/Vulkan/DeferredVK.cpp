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

namespace pe
{
    Deferred::Deferred()
    {
        DSComposition = {};
    }

    Deferred::~Deferred()
    {
    }

    void Deferred::BeginPass(CommandBuffer *cmd, uint32_t imageIndex)
    {
        cmd->BeginPass(renderPass, framebuffers[imageIndex]);

        Model::commandBuffer = cmd;
        Model::pipeline = pipeline;
    }

    void Deferred::EndPass()
    {
        Model::commandBuffer->EndPass();
        Model::commandBuffer = nullptr;
        Model::pipeline = nullptr;
    }

    void Deferred::createDeferredUniforms(std::map<std::string, Image *> &renderTargets)
    {
        uniform = Buffer::Create(
            sizeof(ubo),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);
        uniform->Map();
        uniform->Zero();
        uniform->Flush();
        uniform->Unmap();

        DSComposition = Descriptor::Create(Pipeline::getDescriptorSetLayoutComposition());

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

            RHII.WaitGraphicsQueue();
            RHII.WaitAndLockSubmits();

            Buffer *staging = Buffer::Create(
                imageSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VMA_MEMORY_USAGE_CPU_ONLY);
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
            info.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
            ibl_brdf_lut = Image::Create(info);

            ImageViewCreateInfo viewInfo{};
            viewInfo.image = ibl_brdf_lut;
            ibl_brdf_lut->CreateImageView(viewInfo);

            SamplerCreateInfo samplerInfo{};
            samplerInfo.maxLod = static_cast<float>(info.mipLevels);
            ibl_brdf_lut->CreateSampler(samplerInfo);

            ibl_brdf_lut->TransitionImageLayout(VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            ibl_brdf_lut->CopyBufferToImage(staging);
            ibl_brdf_lut->GenerateMipMaps();

            staging->Destroy();

            RHII.UnlockSubmits();

            Mesh::uniqueTextures[path] = ibl_brdf_lut;
        }

        updateDescriptorSets(renderTargets);
    }

    void Deferred::updateDescriptorSets(std::map<std::string, Image *> &renderTargets)
    {
        std::array<DescriptorUpdateInfo, 10> infos{};

        infos[0].binding = 0;
        infos[0].pImage = RHII.depth;
        infos[0].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        infos[1].binding = 1;
        infos[1].pImage = renderTargets["normal"];

        infos[2].binding = 2;
        infos[2].pImage = renderTargets["albedo"];

        infos[3].binding = 3;
        infos[3].pImage = renderTargets["srm"];

        infos[4].binding = 4;
        infos[4].pBuffer = &CONTEXT->GetSystem<LightSystem>()->GetUniform();

        infos[5].binding = 5;
        infos[5].pImage = renderTargets["ssaoBlur"];

        infos[6].binding = 6;
        infos[6].pImage = renderTargets["ssr"];

        infos[7].binding = 7;
        infos[7].pImage = renderTargets["emissive"];

        infos[8].binding = 8;
        infos[8].pImage = ibl_brdf_lut;

        infos[9].binding = 9;
        infos[9].pBuffer = uniform;

        DSComposition->UpdateDescriptor(10, infos.data());
    }

    void Deferred::update(mat4 &invViewProj)
    {
        ubo.screenSpace[0] = {invViewProj[0]};
        ubo.screenSpace[1] = {invViewProj[1]};
        ubo.screenSpace[2] = {invViewProj[2]};
        ubo.screenSpace[3] = {invViewProj[3]};
        ubo.screenSpace[4] = {
            static_cast<float>(GUI::show_ssao), static_cast<float>(GUI::show_ssr),
            static_cast<float>(GUI::show_tonemapping), static_cast<float>(GUI::use_AntiAliasing)};
        ubo.screenSpace[5] = {
            static_cast<float>(GUI::use_IBL), static_cast<float>(GUI::use_Volumetric_lights),
            static_cast<float>(GUI::volumetric_steps), static_cast<float>(GUI::volumetric_dither_strength)};
        ubo.screenSpace[6] = {GUI::fog_global_thickness, GUI::lights_intensity, GUI::lights_range, GUI::fog_max_height};
        ubo.screenSpace[7] = {
            GUI::fog_ground_thickness, static_cast<float>(GUI::use_fog), static_cast<float>(GUI::shadow_cast), 0.0f};

        uniform->CopyRequest<Launch::AsyncDeferred>({&ubo, sizeof(ubo), 0}, false);
    }

    void Deferred::draw(CommandBuffer *cmd, uint32_t imageIndex, Shadows &shadows, SkyBox &skybox)
    {
        mat4 values{};
        values[0] = shadows.viewZ;
        values[1] = vec4(GUI::shadow_cast);
        std::vector<Descriptor *> handles{DSComposition, shadows.descriptorSetDeferred, skybox.descriptorSet};

        cmd->BeginPass(compositionRenderPass, compositionFramebuffers[imageIndex]);
        cmd->PushConstants(pipelineComposition, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(mat4), &values);
        cmd->BindPipeline(pipelineComposition);
        cmd->BindDescriptors(pipelineComposition, (uint32_t)handles.size(), handles.data());
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
    }

    void Deferred::createRenderPasses(std::map<std::string, Image *> &renderTargets)
    {
        std::vector<Attachment> attachments(6);

        // Target attachments are initialized to match render targets by default
        attachments[0].format = renderTargets["normal"]->imageInfo.format;
        attachments[1].format = renderTargets["albedo"]->imageInfo.format;
        attachments[2].format = renderTargets["srm"]->imageInfo.format;
        attachments[3].format = renderTargets["velocity"]->imageInfo.format;
        attachments[4].format = renderTargets["emissive"]->imageInfo.format;

        // Depth
        attachments[5].flags = {};
        attachments[5].format = RHII.depth->imageInfo.format;
        attachments[5].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[5].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[5].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[5].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[5].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[5].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[5].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        renderPass = RenderPass::Create(attachments);

        Attachment attachment{};
        attachment.format = renderTargets["viewport"]->imageInfo.format;
        compositionRenderPass = RenderPass::Create(attachment);
    }

    void Deferred::createFrameBuffers(std::map<std::string, Image *> &renderTargets)
    {
        createGBufferFrameBuffers(renderTargets);
        createCompositionFrameBuffers(renderTargets);
    }

    void Deferred::createGBufferFrameBuffers(std::map<std::string, Image *> &renderTargets)
    {
        framebuffers.resize(RHII.swapchain->images.size());
        for (size_t i = 0; i < RHII.swapchain->images.size(); ++i)
        {
            uint32_t width = renderTargets["albedo"]->imageInfo.width;
            uint32_t height = renderTargets["albedo"]->imageInfo.height;
            std::vector<ImageViewHandle> views{
                renderTargets["normal"]->view,
                renderTargets["albedo"]->view,
                renderTargets["srm"]->view,
                renderTargets["velocity"]->view,
                renderTargets["emissive"]->view,
                RHII.depth->view};
            framebuffers[i] = FrameBuffer::Create(width, height, views, renderPass);
        }
    }

    void Deferred::createCompositionFrameBuffers(std::map<std::string, Image *> &renderTargets)
    {
        compositionFramebuffers.resize(RHII.swapchain->images.size());
        for (size_t i = 0; i < RHII.swapchain->images.size(); ++i)
        {
            uint32_t width = renderTargets["viewport"]->imageInfo.width;
            uint32_t height = renderTargets["viewport"]->imageInfo.height;
            ImageViewHandle view = renderTargets["viewport"]->view;
            compositionFramebuffers[i] = FrameBuffer::Create(width, height, view, compositionRenderPass);
        }
    }

    void Deferred::createPipelines(std::map<std::string, Image *> &renderTargets)
    {
        createGBufferPipeline(renderTargets);
        createCompositionPipeline(renderTargets);
    }

    void Deferred::createGBufferPipeline(std::map<std::string, Image *> &renderTargets)
    {
        PipelineCreateInfo info{};
        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Deferred/gBuffer.vert", ShaderType::Vertex});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/Deferred/gBuffer.frag", ShaderType::Fragment});
        info.vertexInputBindingDescriptions = info.pVertShader->GetReflection().GetVertexBindings();
        info.vertexInputAttributeDescriptions = info.pVertShader->GetReflection().GetVertexAttributes();
        info.width = renderTargets["albedo"]->width_f;
        info.height = renderTargets["albedo"]->height_f;
        info.pushConstantStage = PushConstantStage::VertexAndFragment;
        info.pushConstantSize = 5 * sizeof(uint32_t);
        info.cullMode = CullMode::Front;
        info.colorBlendAttachments = {
            renderTargets["normal"]->blendAttachment,
            renderTargets["albedo"]->blendAttachment,
            renderTargets["srm"]->blendAttachment,
            renderTargets["velocity"]->blendAttachment,
            renderTargets["emissive"]->blendAttachment,
        };

        info.descriptorSetLayouts = {
            Pipeline::getDescriptorSetLayoutGbufferVert(),
            Pipeline::getDescriptorSetLayoutGbufferFrag()};
        info.renderPass = renderPass;

        pipeline = Pipeline::Create(info);

        info.pVertShader->Destroy();
        info.pFragShader->Destroy();
    }

    void Deferred::createCompositionPipeline(std::map<std::string, Image *> &renderTargets)
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
        info.width = renderTargets["viewport"]->width_f;
        info.height = renderTargets["viewport"]->height_f;
        info.pushConstantStage = PushConstantStage::Fragment;
        info.pushConstantSize = sizeof(mat4);
        info.colorBlendAttachments = {renderTargets["viewport"]->blendAttachment};
        info.descriptorSetLayouts = {
            Pipeline::getDescriptorSetLayoutComposition(),
            Pipeline::getDescriptorSetLayoutShadowsDeferred(),
            Pipeline::getDescriptorSetLayoutSkybox()};
        info.renderPass = compositionRenderPass;

        pipelineComposition = Pipeline::Create(info);

        info.pVertShader->Destroy();
        info.pFragShader->Destroy();
    }

    void Deferred::destroy()
    {
        renderPass->Destroy();
        compositionRenderPass->Destroy();

        for (auto &framebuffer : framebuffers)
            framebuffer->Destroy();
        for (auto &framebuffer : compositionFramebuffers)
            framebuffer->Destroy();

        Pipeline::getDescriptorSetLayoutComposition()->Destroy();
        uniform->Destroy();
        pipeline->Destroy();
        pipelineComposition->Destroy();
    }
}
#endif
