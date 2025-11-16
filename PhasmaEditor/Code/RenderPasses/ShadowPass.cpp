#include "ShadowPass.h"
#include "API/Shader.h"
#include "API/RHI.h"
#include "API/Image.h"
#include "API/Buffer.h"
#include "API/Pipeline.h"
#include "API/Command.h"
#include "Systems/RendererSystem.h"
#include "Systems/CameraSystem.h"
#include "Scene/Geometry.h"

namespace pe
{
    void ShadowPass::Init()
    {
        m_textures.resize(Settings::Get<GlobalSettings>().num_cascades);
        int i = 0;
        for (auto *&texture : m_textures)
        {
            vk::ImageCreateInfo info = Image::CreateInfoInit();
            info.format = RHII.GetDepthFormat();
            info.extent = vk::Extent3D{Settings::Get<GlobalSettings>().shadow_map_size, Settings::Get<GlobalSettings>().shadow_map_size, 1};
            info.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
            texture = Image::Create(info, "ShadowMap_" + std::to_string(i++));
            texture->SetClearColor(vec4(Color::Depth, Color::Stencil, 0.0f, 1.0f));

            texture->CreateRTV();
            texture->CreateSRV(vk::ImageViewType::e2D);
        }

        vk::SamplerCreateInfo samplerInfo = Sampler::CreateInfoInit();
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.maxAnisotropy = 1.f;
        samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
        samplerInfo.compareEnable = VK_TRUE;
        samplerInfo.compareOp = vk::CompareOp::eGreaterOrEqual;
        m_sampler = Sampler::Create(samplerInfo, "Sampler_ClampToEdge_GE_FOW");

        m_attachments.resize(1);
        m_attachments[0] = {};
        m_attachments[0].loadOp = vk::AttachmentLoadOp::eClear;
        m_attachments[0].storeOp = vk::AttachmentStoreOp::eStore;

        m_uniforms.resize(RHII.GetSwapchainImageCount());
    }

    void ShadowPass::UpdatePassInfo()
    {
        m_passInfo->name = "shadows_pipeline";
        m_passInfo->pVertShader = Shader::Create(Path::Executable + "Assets/Shaders/Shadows/ShadowsVS.hlsl", vk::ShaderStageFlagBits::eVertex, "mainVS", std::vector<Define>{}, ShaderCodeType::HLSL);
        m_passInfo->dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor, vk::DynamicState::eDepthBias};
        m_passInfo->cullMode = vk::CullModeFlagBits::eFront;
        m_passInfo->depthFormat = RHII.GetDepthFormat();
        m_passInfo->Update();
    }

    void ShadowPass::CreateUniforms(CommandBuffer *cmd)
    {
        for (auto &uniform : m_uniforms)
        {
            uniform = Buffer::Create(
                RHII.AlignUniform(Settings::Get<GlobalSettings>().num_cascades * sizeof(mat4)),
                vk::BufferUsageFlagBits2::eUniformBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                "Shadows_uniform_buffer");
            uniform->Map();
            uniform->Zero();
            uniform->Flush();
        }
    }

    void ShadowPass::UpdateDescriptorSets()
    {
    }

    void ShadowPass::Update()
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        if (gSettings.shadows)
        {
            Camera *camera_main = GetGlobalSystem<CameraSystem>()->GetCamera(0);
            CalculateCascades(camera_main);

            BufferRange range{};
            range.data = m_cascades.data();
            range.size = gSettings.num_cascades * sizeof(mat4);
            range.offset = 0;
            m_uniforms[RHII.GetFrameIndex()]->Copy(1, &range, false);
        }
    }

    constexpr float cascadeSplitLambda = 0.95f;
    void ShadowPass::CalculateCascades(Camera *camera)
    {
        uint32_t cascades = Settings::Get<GlobalSettings>().num_cascades;
        m_cascades.resize(cascades);

        std::vector<float> cascadeSplits;
        cascadeSplits.resize(cascades);

        float nearClip = camera->GetNearPlane();
        float farClip = camera->GetFarPlane();
        float clipRange = farClip - nearClip;

        float minZ = nearClip;
        float maxZ = nearClip + clipRange;

        float range = maxZ - minZ;
        float ratio = maxZ / minZ;

        // Calculate split depths based on view camera frustum
        // Based on method presented in https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch10.html
        for (uint32_t i = 0; i < cascades; i++)
        {
            float p = (i + 1) / static_cast<float>(cascades);
            float log = minZ * std::pow(ratio, p);
            float uniform = minZ + range * p;
            float d = cascadeSplitLambda * (log - uniform) + uniform;
            cascadeSplits[i] = (d - nearClip) / clipRange;
        }

        // Calculate orthographic projection matrix for each cascade
        float lastSplitDist = 0.0;
        for (uint32_t i = 0; i < cascades; i++)
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
            const vec3 position = camera->GetPosition();
            mat4 projection = perspective(camera->Fovy(), aspect, nearClip, farClip);
            mat4 view = lookAt(position, position + camera->GetFront(), camera->WorldUp());
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

            vec3 lightDir = -normalize(make_vec3(Settings::Get<GlobalSettings>().sun_direction.data()));
            auto v0 = frustumCenter - (lightDir * radius);
            mat4 lightViewMatrix = lookAt(frustumCenter - (lightDir * radius), frustumCenter, camera->WorldUp());
            mat4 lightOrthoMatrix = ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, maxExtents.z - minExtents.z, -clipRange);

            // Store split distance and matrix in cascade
            m_cascades[i] = lightOrthoMatrix * lightViewMatrix;
            m_viewZ[i] = (camera->GetNearPlane() + splitDist * clipRange) * 1.5f;

            lastSplitDist = cascadeSplits[i];
        }
    }

    void ShadowPass::ClearDepths(CommandBuffer *cmd)
    {
        cmd->ClearDepthStencils(m_textures);
    }

    void ShadowPass::Draw(CommandBuffer *cmd)
    {
        PE_ERROR_IF(m_geometry == nullptr, "Geometry was not set");

        cmd->BeginDebugRegion("ShadowPass");

        if (!m_geometry->HasOpaqueDrawInfo())
        {
            ClearDepths(cmd);
        }
        else
        {
            PushConstants_Shadows pushConstants{};
            pushConstants.jointsCount = 0u;

            uint32_t cascades = Settings::Get<GlobalSettings>().num_cascades;
            for (uint32_t i = 0; i < cascades; i++)
            {
                pushConstants.vp = m_cascades[i];
                
                PassInfo &passInfo = *m_passInfo;
                Attachment &attachment = m_attachments[0];
                attachment.image = m_textures[i];

                cmd->BeginPass(1, m_attachments.data(), "Cascade_" + std::to_string(i));
                cmd->SetViewport(0.f, 0.f, attachment.image->GetWidth_f(), attachment.image->GetHeight_f());
                cmd->SetScissor(0, 0, attachment.image->GetWidth(), attachment.image->GetHeight());
                cmd->BindPipeline(passInfo);
                cmd->BindIndexBuffer(m_geometry->GetBuffer(), 0);
                cmd->BindVertexBuffer(m_geometry->GetBuffer(), m_geometry->GetPositionsOffset());
                cmd->SetConstants(pushConstants);
                cmd->PushConstants();
                cmd->DrawIndexedIndirect(m_geometry->GetIndirectAll(), 0, m_geometry->GetPrimitivesCount());
                cmd->EndPass();
            }
        }
        
        cmd->EndDebugRegion();

        m_geometry = nullptr;
    }

    void ShadowPass::Resize(uint32_t width, uint32_t height)
    {
        // PE_ERROR("Not implemented");
    }

    void ShadowPass::Destroy()
    {
        for (auto &texture : m_textures)
            Image::Destroy(texture);

        Sampler::Destroy(m_sampler);

        for (auto &uniform : m_uniforms)
            Buffer::Destroy(uniform);
    }
}
