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
#include "Renderer/Shadows.h"
#include "GUI/GUI.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Vertex.h"
#include "Shader/Shader.h"
#include "Renderer/RHI.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/Image.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/Buffer.h"
#include "Renderer/Pipeline.h"
#include "Renderer/RenderPass.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    Shadows::Shadows()
    {
        descriptorSetDeferred = {};
    }

    Shadows::~Shadows()
    {
    }

    void Shadows::Init()
    {
        depthFormat = RHII.GetDepthFormat();
    }

    void Shadows::CreateRenderPass()
    {
        Attachment attachment{};
        attachment.format = depthFormat;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        renderPass = RenderPass::Create(&attachment, 1, "shadows_renderPass");
    }

    void Shadows::CreateFrameBuffers()
    {
        textures.resize(SHADOWMAP_CASCADES);
        int i = 0;
        for (auto *&texture : textures)
        {
            ImageCreateInfo info{};
            info.format = depthFormat;
            info.initialState = LayoutState::Undefined;
            info.width = SHADOWMAP_SIZE;
            info.height = SHADOWMAP_SIZE;
            info.tiling = VK_IMAGE_TILING_OPTIMAL;
            info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            info.properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            info.name = "ShadowMap_" + std::to_string(i++);
            texture = Image::Create(info);

            ImageViewCreateInfo viewInfo{};
            viewInfo.image = texture;
            viewInfo.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT; // | VK_IMAGE_ASPECT_STENCIL_BIT;
            texture->CreateImageView(viewInfo);

            SamplerCreateInfo samplerInfo{};
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.maxAnisotropy = 1.f;
            samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
            samplerInfo.compareEnable = VK_TRUE;
            samplerInfo.compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
            texture->CreateSampler(samplerInfo);

            texture->ChangeLayout(nullptr, LayoutState::DepthStencilAttachment);
        }

        framebuffers.resize(RHII.swapchain->images.size());
        for (uint32_t i = 0; i < framebuffers.size(); ++i)
        {
            framebuffers[i] = std::array<FrameBuffer *, SHADOWMAP_CASCADES>{};
            for (int j = 0; j < SHADOWMAP_CASCADES; j++)
            {
                ImageViewHandle view = textures[j]->view;
                framebuffers[i][j] = FrameBuffer::Create(SHADOWMAP_SIZE, SHADOWMAP_SIZE, &view, 1, renderPass, "shadows_frameBuffer_" + std::to_string(i));
            }
        }
    }

    void Shadows::CreatePipeline()
    {
        PipelineCreateInfo info{};

        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Shadows/shaderShadows.vert", ShaderType::Vertex});
        info.vertexInputBindingDescriptions = info.pVertShader->GetReflection().GetVertexBindings();
        info.vertexInputAttributeDescriptions = info.pVertShader->GetReflection().GetVertexAttributes();
        info.width = static_cast<float>(SHADOWMAP_SIZE);
        info.height = static_cast<float>(SHADOWMAP_SIZE);
        info.pushConstantStage = PushConstantStage::Vertex;
        info.pushConstantSize = sizeof(ShadowPushConstData);
        info.colorBlendAttachments = {textures[0]->blendAttachment};
        info.dynamicStates = {VK_DYNAMIC_STATE_DEPTH_BIAS};
        info.descriptorSetLayouts = {Pipeline::getDescriptorSetLayoutGbufferVert()};
        info.renderPass = renderPass;
        info.name = "shadows_pipeline";

        pipeline = Pipeline::Create(info);

        Shader::Destroy(info.pVertShader);
    }

    void Shadows::CreateUniforms()
    {
        uniformBuffer = Buffer::Create(
            SHADOWMAP_CASCADES * sizeof(mat4),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            "Shadows_uniform_buffer");
        uniformBuffer->Map();
        uniformBuffer->Zero();
        uniformBuffer->Flush();
        uniformBuffer->Unmap();

        UpdateDescriptorSets();
    }

    void Shadows::UpdateDescriptorSets()
    {
        std::vector<DescriptorUpdateInfo> infos{};

        infos.emplace_back();
        infos.back().pBuffer = uniformBuffer;

        for (int i = 0; i < SHADOWMAP_CASCADES; i++)
        {
            infos.emplace_back();
            infos.back().binding = 1;
            infos.back().pImage = textures[i];
            infos.back().imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        }

        uint32_t infosCount = static_cast<uint32_t>(infos.size());
        DescriptorLayout *layout = Pipeline::getDescriptorSetLayoutShadowsDeferred();

        descriptorSetDeferred = Descriptor::Create(layout, SHADOWMAP_CASCADES, "Shadows_deferred_descriptor");
        descriptorSetDeferred->UpdateDescriptor(infosCount, infos.data());
    }

    void Shadows::Update(Camera *camera)
    {
        if (GUI::shadow_cast)
        {
            CalculateCascades(camera);

            MemoryRange range{};
            range.data = &cascades;
            range.size = SHADOWMAP_CASCADES * sizeof(mat4);
            range.offset = 0;
            uniformBuffer->Copy(&range, 1, false);
        }
    }

    constexpr float cascadeSplitLambda = 0.95f;
    void Shadows::CalculateCascades(Camera *camera)
    {
        float cascadeSplits[SHADOWMAP_CASCADES];

        float nearClip = camera->nearPlane;
        float farClip = camera->farPlane;
        float clipRange = farClip - nearClip;

        float minZ = nearClip;
        float maxZ = nearClip + clipRange;

        float range = maxZ - minZ;
        float ratio = maxZ / minZ;

        // Calculate split depths based on view camera frustum
        // Based on method presented in https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch10.html
        for (uint32_t i = 0; i < SHADOWMAP_CASCADES; i++)
        {
            float p = (i + 1) / static_cast<float>(SHADOWMAP_CASCADES);
            float log = minZ * std::pow(ratio, p);
            float uniform = minZ + range * p;
            float d = cascadeSplitLambda * (log - uniform) + uniform;
            cascadeSplits[i] = (d - nearClip) / clipRange;
        }

        // Calculate orthographic projection matrix for each cascade
        float lastSplitDist = 0.0;
        for (uint32_t i = 0; i < SHADOWMAP_CASCADES; i++)
        {
            float splitDist = cascadeSplits[i];

            vec3 frustumCorners[8] = {
                vec3(-1.0f, +1.0f, -1.0f),
                vec3(+1.0f, +1.0f, -1.0f),
                vec3(+1.0f, -1.0f, -1.0f),
                vec3(-1.0f, -1.0f, -1.0f),
                vec3(-1.0f, +1.0f, +1.0f),
                vec3(+1.0f, +1.0f, +1.0f),
                vec3(+1.0f, -1.0f, +1.0f),
                vec3(-1.0f, -1.0f, +1.0f),
            };

            // Project frustum corners into world space
            auto &renderArea = Context::Get()->GetSystem<RendererSystem>()->GetRenderArea();
            const float aspect = renderArea.viewport.width / renderArea.viewport.height;
            mat4 projection = perspective(camera->FovxToFovy(camera->FOV, aspect), aspect, nearClip, farClip, false);
            mat4 view = lookAt(camera->position, camera->position + camera->front, camera->WorldUp());
            mat4 invVP = inverse(projection * view);
            for (uint32_t i = 0; i < 8; i++)
            {
                vec4 invCorner = invVP * vec4(frustumCorners[i], 1.0f);
                frustumCorners[i] = invCorner / invCorner.w;
            }

            for (uint32_t i = 0; i < 4; i++)
            {
                vec3 dist = frustumCorners[i + 4] - frustumCorners[i];
                frustumCorners[i + 4] = frustumCorners[i] + (dist * splitDist);
                frustumCorners[i] = frustumCorners[i] + (dist * lastSplitDist);
            }

            // Get frustum center
            vec3 frustumCenter = vec3(0.0f);
            for (uint32_t i = 0; i < 8; i++)
            {
                frustumCenter += frustumCorners[i];
            }
            frustumCenter /= 8.0f;

            float radius = 0.0f;
            for (uint32_t i = 0; i < 8; i++)
            {
                float distance = length(frustumCorners[i] - frustumCenter);
                radius = maximum(radius, distance);
            }
            radius = std::ceil(radius * 16.0f) / 16.0f;

            vec3 maxExtents = vec3(radius);
            vec3 minExtents = -maxExtents;

            vec3 lightDir = -normalize(vec3((float *)&GUI::sun_direction));
            auto v0 = frustumCenter - (lightDir * radius);
            mat4 lightViewMatrix = lookAt(frustumCenter - (lightDir * radius), frustumCenter, camera->WorldUp());
            mat4 lightOrthoMatrix = ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, -clipRange, maxExtents.z - minExtents.z, GlobalSettings::ReverseZ);

            // Store split distance and matrix in cascade
            cascades[i] = lightOrthoMatrix * lightViewMatrix;
            viewZ[i] = (camera->nearPlane + splitDist * clipRange) * 1.5f;

            lastSplitDist = cascadeSplits[i];
        }
    }

    void Shadows::Draw(CommandBuffer *cmd, uint32_t imageIndex)
    {
    }

    void Shadows::Resize(uint32_t width, uint32_t height)
    {
    }

    void Shadows::Destroy()
    {
        if (VkRenderPass(renderPass->Handle()))
        {
            vkDestroyRenderPass(RHII.device, renderPass->Handle(), nullptr);
            renderPass->Handle() = {};
        }

        for (auto &texture : textures)
            Image::Destroy(texture);

        for (auto fba : framebuffers)
            for (auto fb : fba)
                FrameBuffer::Destroy(fb);

        Buffer::Destroy(uniformBuffer);
        Pipeline::Destroy(pipeline);
    }
}
#endif
