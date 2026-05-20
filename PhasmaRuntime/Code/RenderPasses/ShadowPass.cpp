#include "ShadowPass.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/RenderGraph.h"
#include "API/Shader.h"
#include "Camera/Camera.h"
#include "GbufferPass.h"
#include "Render/SceneRendererHost.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"

namespace pe
{
    void ShadowPass::Init()
    {
        m_textures.resize(Settings::Get<GlobalSettings>().num_cascades);
        int i = 0;
        for (auto *&texture : m_textures)
        {
            ImageDesc desc{};
            desc.format = RHII.GetDepthFormat();
            desc.width = Settings::Get<GlobalSettings>().shadow_map_size;
            desc.height = Settings::Get<GlobalSettings>().shadow_map_size;
            desc.usage = PE_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT | PE_IMAGE_USAGE_SAMPLED | PE_IMAGE_USAGE_TRANSFER_DST;
            desc.clearColor = vec4(Color::Depth, Color::Stencil, 0.0f, 1.0f);
            desc.name = "ShadowMap_" + std::to_string(i++);
            texture = Image::Create(desc);
            texture->SetClearColor(desc.clearColor);

            texture->CreateRTV();
            texture->CreateSRV(PE_IMAGE_VIEW_TYPE_2D);
        }

        SamplerDesc samplerInfo = Sampler::CreateInfoInit();
        samplerInfo.addressModeU = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxAnisotropy = 1.f;
        samplerInfo.borderColor = PE_SAMPLER_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        samplerInfo.compareEnable = true;
        samplerInfo.compareOp = PE_COMPARE_OP_GREATER_OR_EQUAL;
        m_sampler = Sampler::Create(samplerInfo, "Sampler_ClampToEdge_GE_FOW");

        m_attachments.resize(1);
        m_attachments[0] = {};
        m_attachments[0].loadOp = PE_LOAD_OP_CLEAR;
        m_attachments[0].storeOp = PE_STORE_OP_STORE;

        m_uniforms.resize(RHII.GetSwapchainImageCount());
    }

    void ShadowPass::UpdatePassInfo()
    {
        m_passInfo->name = "shadows_pipeline";
        m_passInfo->pVertShader = Shader::Create({.sourcePath = Path::Assets + "Shaders/Shadows/ShadowsVS.hlsl", .entryPoint = "mainVS", .stage = PE_SHADER_STAGE_VERTEX, .defines = std::vector<Define>{}});
        m_passInfo->dynamicStates = {PE_DYNAMIC_STATE_VIEWPORT, PE_DYNAMIC_STATE_SCISSOR, PE_DYNAMIC_STATE_DEPTH_BIAS};
        m_passInfo->cullMode = PE_CULL_MODE_NONE;
        m_passInfo->depthFormat = RHII.GetDepthFormat();
        m_passInfo->depthBiasEnable = true;
        m_passInfo->depthBiasConstantFactor = Settings::Get<GlobalSettings>().depth_bias[0];
        m_passInfo->depthBiasClamp = Settings::Get<GlobalSettings>().depth_bias[1];
        m_passInfo->depthBiasSlopeFactor = Settings::Get<GlobalSettings>().depth_bias[2];
        m_passInfo->Update();
    }

    void ShadowPass::CreateUniforms(CommandBuffer *cmd)
    {
        for (auto &uniform : m_uniforms)
        {
            uniform = Buffer::Create({
                .size = RHII.AlignUniform(Settings::Get<GlobalSettings>().num_cascades * sizeof(mat4)),
                .usage = PE_BUFFER_USAGE_UNIFORM_BUFFER,
                .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU,
                .name = "Shadows_uniform_buffer",
            });
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
            Camera *camera_main = GetActiveScene()->GetActiveCamera();
            CalculateCascades(camera_main);

            BufferRange range{};
            range.data = m_cascades.data();
            range.size = gSettings.num_cascades * sizeof(mat4);
            range.offset = 0;
            m_uniforms[RHII.GetFrameIndex()]->Copy(1, &range, false);

            Scene &scene = *GetActiveScene();
            if (scene.GetMeshCount() > 0)
            {
                uint32_t frame = RHII.GetFrameIndex();
                const auto &sets = m_passInfo->GetDescriptors(frame);
                Descriptor *setUniforms = sets[0];
                setUniforms->SetBuffer(0, scene.GetUniforms(frame));
                setUniforms->SetBuffer(1, scene.GetMeshConstants());
                setUniforms->Update();
            }
        }
    }

    void ShadowPass::DeclareOutputs(RGBuilder &builder)
    {
        for (auto *texture : m_textures)
        {
            if (!texture)
                continue;

            builder.OutputDepth(texture);
        }
    }

    void ShadowPass::CalculateCascades(Camera *camera)
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        uint32_t cascades = gSettings.num_cascades;
        m_cascades.resize(cascades);
        m_cascadePlanes.resize(cascades);

        float cascadeSplits[8];

        float nearClip = camera->GetNearPlane();
        float farClip = std::max(gSettings.shadow_distance, nearClip + 1.0f);
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
            float lambda = std::clamp(gSettings.shadow_cascade_lambda, 0.0f, 1.0f);
            float d = lambda * (log - uniform) + uniform;
            cascadeSplits[i] = (d - nearClip) / clipRange;
        }

        // Calculate orthographic projection matrix for each cascade
        float lastSplitDist = 0.0;
        for (uint32_t i = 0; i < cascades; i++)
        {
            float splitDist = cascadeSplits[i];

            vec3 frustumCorners[8] = {
                vec3(-1.0f, +1.0f, 1.0f),
                vec3(+1.0f, +1.0f, 1.0f),
                vec3(+1.0f, -1.0f, 1.0f),
                vec3(-1.0f, -1.0f, 1.0f),
                vec3(-1.0f, +1.0f, 0.0f),
                vec3(+1.0f, +1.0f, 0.0f),
                vec3(+1.0f, -1.0f, 0.0f),
                vec3(-1.0f, -1.0f, 0.0f),
            };

            // Project frustum corners into world space
            Image *displayRT = RequireActiveSceneRendererHost().GetDisplayRT();
            const float aspect = displayRT->GetWidth_f() / displayRT->GetHeight_f();
            const vec3 position = camera->GetPosition();
            mat4 projection;
            if (camera->IsOrthographic())
            {
                const float halfHeight = std::max(0.001f, camera->GetOrthographicSize()) * 0.5f;
                const float halfWidth = halfHeight * aspect;
                projection = ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, nearClip, farClip);
            }
            else
            {
                projection = perspective(camera->Fovy(), aspect, farClip, nearClip);
            }
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

            auto &dirLights = GetActiveScene()->GetDirectionalLights();
            vec3 lightDir = vec3(0, -1, 0); // Default
            if (!dirLights.empty())
            {
                glm::quat rot = glm::quat(dirLights[0].rotation.w, dirLights[0].rotation.x, dirLights[0].rotation.y, dirLights[0].rotation.z);
                lightDir = rot * glm::vec3(0, 0, -1);
            }
            lightDir = glm::normalize(lightDir);
            vec3 lightUp = camera->WorldUp();
            if (std::abs(glm::dot(lightDir, glm::normalize(lightUp))) > 0.95f)
                lightUp = camera->WorldFront();

            const float texelSizeWorld = (radius * 2.0f) / static_cast<float>(gSettings.shadow_map_size);
            mat4 lightViewMatrix = lookAt(frustumCenter - (lightDir * radius), frustumCenter, lightUp);
            vec4 centerLightSpace = lightViewMatrix * vec4(frustumCenter, 1.0f);
            centerLightSpace.x = std::floor(centerLightSpace.x / texelSizeWorld) * texelSizeWorld;
            centerLightSpace.y = std::floor(centerLightSpace.y / texelSizeWorld) * texelSizeWorld;

            mat4 invLightViewMatrix = inverse(lightViewMatrix);
            vec4 snappedCenter = invLightViewMatrix * centerLightSpace;
            frustumCenter = vec3(snappedCenter) / snappedCenter.w;
            lightViewMatrix = lookAt(frustumCenter - (lightDir * radius), frustumCenter, lightUp);
            mat4 lightOrthoMatrix = ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, maxExtents.z - minExtents.z, -clipRange);

            // Store split distance and matrix in cascade
            m_cascades[i] = lightOrthoMatrix * lightViewMatrix;
            m_cascadePlanes[i] = ExtractFrustumPlanes(m_cascades[i]);
            m_viewZ[i] = camera->GetNearPlane() + splitDist * clipRange;
            m_texelSizeWorld[i] = texelSizeWorld;

            lastSplitDist = cascadeSplits[i];
        }
    }

    void ShadowPass::ClearDepths(CommandBuffer *cmd)
    {
        cmd->ClearDepthStencils(m_textures);
    }

    void ShadowPass::ExecutePass(CommandBuffer *cmd)
    {
        PE_ERROR_IF(m_scene == nullptr, "Scene was not set");

        cmd->BeginDebugRegion("ShadowPass");

        if (m_scene->GetMeshCount() == 0)
        {
            ClearDepths(cmd);
        }
        else
        {
            PushConstants_Shadows pushConstants{};
            pushConstants.jointsCount = static_cast<uint32_t>(m_scene->GetSkeleton().GetBoneCount());

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
                const auto &gSettings = Settings::Get<GlobalSettings>();
                cmd->SetDepthBias(gSettings.depth_bias[0], gSettings.depth_bias[1], gSettings.depth_bias[2]);
                cmd->BindIndexBuffer(m_scene->GetBuffer(), 0);
                cmd->BindVertexBuffer(m_scene->GetBuffer(), m_scene->GetPositionsOffset());
                cmd->SetConstants(pushConstants);
                cmd->PushConstants();
                // TODO: per-cascade light-frustum culling
                cmd->DrawIndexedIndirect(m_scene->GetIndirectAll(), 0, m_scene->GetMeshCount());
                cmd->EndPass();
            }
        }

        cmd->EndDebugRegion();

        m_scene = nullptr;
    }

    void ShadowPass::Resize(uint32_t width, uint32_t height)
    {
        // PE_ERROR("Not implemented");
    }

    std::array<vec4, 6> ShadowPass::ExtractFrustumPlanes(const mat4 &M)
    {
        // Gribb/Hartmann: extract rows from column-major GLM matrix, then combine.
        // For Vulkan depth [0, 1]:  near = row2, far = row3 - row2.
        vec4 row0 = {M[0][0], M[1][0], M[2][0], M[3][0]};
        vec4 row1 = {M[0][1], M[1][1], M[2][1], M[3][1]};
        vec4 row2 = {M[0][2], M[1][2], M[2][2], M[3][2]};
        vec4 row3 = {M[0][3], M[1][3], M[2][3], M[3][3]};

        std::array<vec4, 6> planes;
        planes[0] = row3 + row0; // left
        planes[1] = row3 - row0; // right
        planes[2] = row3 + row1; // bottom
        planes[3] = row3 - row1; // top
        planes[4] = row2;        // near
        planes[5] = row3 - row2; // far

        for (auto &p : planes)
        {
            float len = glm::length(vec3(p));
            if (len > 1e-6f)
                p /= len;
        }
        return planes;
    }

    bool ShadowPass::AABBInFrustum(const AABB &aabb, const std::array<vec4, 6> &planes)
    {
        for (const vec4 &p : planes)
        {
            // Positive vertex: component-wise pick the corner most along the plane normal.
            vec3 pv;
            pv.x = (p.x >= 0.f) ? aabb.max.x : aabb.min.x;
            pv.y = (p.y >= 0.f) ? aabb.max.y : aabb.min.y;
            pv.z = (p.z >= 0.f) ? aabb.max.z : aabb.min.z;
            if (glm::dot(vec3(p), pv) + p.w < 0.f)
                return false;
        }
        return true;
    }

    void ShadowPass::Destroy()
    {
        for (auto &texture : m_textures)
            Image::Destroy(texture);

        Sampler::Destroy(m_sampler);

        for (auto &uniform : m_uniforms)
            Buffer::Destroy(uniform);
    }
} // namespace pe
