#if PE_VULKAN
#include "Renderer/Shadows.h"
#include "GUI/GUI.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Vertex.h"
#include "Shader/Shader.h"
#include "Renderer/RHI.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Image.h"
#include "Renderer/Buffer.h"
#include "Renderer/Pipeline.h"
#include "Renderer/Command.h"
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

        textures.resize(SHADOWMAP_CASCADES);
        int i = 0;
        for (auto *&texture : textures)
        {
            ImageCreateInfo info{};
            info.format = depthFormat;
            info.initialLayout = ImageLayout::Undefined;
            info.width = SHADOWMAP_SIZE;
            info.height = SHADOWMAP_SIZE;
            info.tiling = ImageTiling::Optimal;
            info.usage = ImageUsage::DepthStencilAttachmentBit | ImageUsage::SampledBit;
            info.properties = MemoryProperty::DeviceLocalBit;
            info.name = "ShadowMap_" + std::to_string(i++);
            texture = Image::Create(info);

            ImageViewCreateInfo viewInfo{};
            viewInfo.image = texture;
            viewInfo.aspectMask = ImageAspect::DepthBit; // | ImageAspect::StencilBit;
            texture->CreateImageView(viewInfo);

            SamplerCreateInfo samplerInfo{};
            samplerInfo.addressModeU = SamplerAddressMode::ClampToEdge;
            samplerInfo.addressModeV = SamplerAddressMode::ClampToEdge;
            samplerInfo.addressModeW = SamplerAddressMode::ClampToEdge;
            samplerInfo.maxAnisotropy = 1.f;
            samplerInfo.borderColor = BorderColor::FloatOpaqueWhite;
            samplerInfo.compareEnable = VK_TRUE;
            samplerInfo.compareOp = CompareOp::GreaterOrEqual;
            texture->CreateSampler(samplerInfo);
        }
    }

    void Shadows::UpdatePipelineInfo()
    {
    }

    void Shadows::CreateUniforms(CommandBuffer *cmd)
    {
        uniformBuffer = Buffer::Create(
            RHII.AlignUniform(SHADOWMAP_CASCADES * sizeof(mat4)) * SWAPCHAIN_IMAGES,
            BufferUsage::UniformBufferBit,
            AllocationCreate::HostAccessSequentialWriteBit,
            "Shadows_uniform_buffer");
        uniformBuffer->Map();
        uniformBuffer->Zero();
        uniformBuffer->Flush();

        DescriptorBindingInfo bindingInfos[SHADOWMAP_CASCADES + 1]{};

        bindingInfos[0].binding = 0;
        bindingInfos[0].type = DescriptorType::UniformBufferDynamic;
        bindingInfos[0].pBuffer = uniformBuffer;

        for (int i = 0; i < SHADOWMAP_CASCADES; i++)
        {
            bindingInfos[i + 1].binding = 1;
            bindingInfos[i + 1].type = DescriptorType::CombinedImageSampler;
            bindingInfos[i + 1].imageLayout = ImageLayout::DepthStencilReadOnly;
            bindingInfos[i + 1].pImage = textures[i];
        }

        DescriptorInfo info{};
        info.count = SHADOWMAP_CASCADES + 1;
        info.bindingInfos = bindingInfos;
        info.stage = ShaderStage::FragmentBit;

        descriptorSetDeferred = Descriptor::Create(&info, "Shadows_deferred_descriptor");
    }

    void Shadows::UpdateDescriptorSets()
    {
        DescriptorBindingInfo bindingInfos[SHADOWMAP_CASCADES + 1]{};

        bindingInfos[0].binding = 0;
        bindingInfos[0].type = DescriptorType::UniformBufferDynamic;
        bindingInfos[0].pBuffer = uniformBuffer;

        for (int i = 0; i < SHADOWMAP_CASCADES; i++)
        {
            bindingInfos[i + 1].binding = 1;
            bindingInfos[i + 1].type = DescriptorType::CombinedImageSampler;
            bindingInfos[i + 1].imageLayout = ImageLayout::DepthStencilReadOnly;
            bindingInfos[i + 1].pImage = textures[i];
        }

        DescriptorInfo info{};
        info.count = SHADOWMAP_CASCADES + 1;
        info.bindingInfos = bindingInfos;
        info.stage = ShaderStage::FragmentBit;

        descriptorSetDeferred->UpdateDescriptor(&info);
    }

    void Shadows::Update(Camera *camera)
    {
        if (GUI::shadow_cast)
        {
            CalculateCascades(camera);

            MemoryRange mr{};
            mr.data = &cascades;
            mr.size = SHADOWMAP_CASCADES * sizeof(mat4);
            mr.offset = RHII.GetFrameDynamicOffset(uniformBuffer->Size(), RHII.GetFrameIndex());
            uniformBuffer->Copy(1, &mr, false);
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
            mat4 projection = perspective(camera->Fovy(), aspect, nearClip, farClip);
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
                radius = max(radius, distance);
            }
            radius = std::ceil(radius * 16.0f) / 16.0f;

            vec3 maxExtents = vec3(radius);
            vec3 minExtents = -maxExtents;

            vec3 lightDir = -normalize(make_vec3(GUI::sun_direction.data()));
            auto v0 = frustumCenter - (lightDir * radius);
            mat4 lightViewMatrix = lookAt(frustumCenter - (lightDir * radius), frustumCenter, camera->WorldUp());
            mat4 lightOrthoMatrix = ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, maxExtents.z - minExtents.z, -clipRange);

            // Store split distance and matrix in cascade
            cascades[i] = lightOrthoMatrix * lightViewMatrix;
            viewZ[i] = (camera->nearPlane + splitDist * clipRange) * 1.5f;

            lastSplitDist = cascadeSplits[i];
        }
    }

    void Shadows::Draw(CommandBuffer *cmd, uint32_t imageIndex)
    {
        // PE_ERROR("Not implemented");
    }

    void Shadows::Resize(uint32_t width, uint32_t height)
    {
        // PE_ERROR("Not implemented");
    }

    void Shadows::BeginPass(CommandBuffer *cmd, uint32_t imageIndex, uint32_t cascade)
    {
        cmd->ImageBarrier(textures[cascade], ImageLayout::DepthStencilAttachment);

        AttachmentInfo depthInfo{};
        depthInfo.image = textures[cascade];
        depthInfo.initialLayout = textures[cascade]->GetCurrentLayout();
        depthInfo.finalLayout = ImageLayout::DepthStencilAttachment;

        cmd->BeginPass(0, nullptr, &depthInfo, &m_renderPassShadows);
        cmd->SetViewport(0.f, 0.f, textures[cascade]->width_f, textures[cascade]->height_f);
        cmd->SetScissor(0, 0, textures[cascade]->imageInfo.width, textures[cascade]->imageInfo.height);
        cmd->SetDepthBias(GUI::depthBias[0], GUI::depthBias[1], GUI::depthBias[2]);
    }

    void Shadows::EndPass(CommandBuffer *cmd, uint32_t cascade)
    {
        cmd->EndPass();
        cmd->ImageBarrier(textures[cascade], ImageLayout::DepthStencilReadOnly);
    }

    void Shadows::Destroy()
    {
        for (auto &texture : textures)
            Image::Destroy(texture);

        Buffer::Destroy(uniformBuffer);
        Descriptor::Destroy(descriptorSetDeferred);
    }
}
#endif
