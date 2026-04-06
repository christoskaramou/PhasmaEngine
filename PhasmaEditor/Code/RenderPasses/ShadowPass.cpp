#include "ShadowPass.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/Shader.h"
#include "Camera/Camera.h"
#include "GbufferPass.h"
#include "Scene/Scene.h"
#include "Systems/RendererSystem.h"

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
        m_passInfo->pVertShader = Shader::Create(Path::Assets + "Shaders/Shadows/ShadowsVS.hlsl", vk::ShaderStageFlagBits::eVertex, "mainVS", std::vector<Define>{}, ShaderCodeType::HLSL);
        m_passInfo->dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor, vk::DynamicState::eDepthBias};
        m_passInfo->cullMode = vk::CullModeFlagBits::eNone;
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
            Camera *camera_main = GetGlobalSystem<RendererSystem>()->GetScene().GetActiveCamera();
            CalculateCascades(camera_main);

            BufferRange range{};
            range.data = m_cascades.data();
            range.size = gSettings.num_cascades * sizeof(mat4);
            range.offset = 0;
            m_uniforms[RHII.GetFrameIndex()]->Copy(1, &range, false);

            Scene &scene = GetGlobalSystem<RendererSystem>()->GetScene();
            if (scene.HasOpaqueDrawInfo())
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

    constexpr float cascadeSplitLambda = 0.95f;
    void ShadowPass::CalculateCascades(Camera *camera)
    {
        uint32_t cascades = Settings::Get<GlobalSettings>().num_cascades;
        m_cascades.resize(cascades);
        m_cascadePlanes.resize(cascades);

        float cascadeSplits[8];

        float nearClip = camera->GetNearPlane();
        float farClip = 100.0f;
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
            Image *displayRT = Context::Get()->GetSystem<RendererSystem>()->GetDisplayRT();
            const float aspect = displayRT->GetWidth_f() / displayRT->GetHeight_f();
            const vec3 position = camera->GetPosition();
            mat4 projection = perspective(camera->Fovy(), aspect, farClip, nearClip);
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

            auto &dirLights = GetGlobalSystem<RendererSystem>()->GetScene().GetDirectionalLights();
            vec3 lightDir = vec3(0, -1, 0); // Default
            if (!dirLights.empty())
            {
                glm::quat rot = glm::quat(dirLights[0].rotation.w, dirLights[0].rotation.x, dirLights[0].rotation.y, dirLights[0].rotation.z);
                lightDir = rot * glm::vec3(0, 0, -1);
            }
            mat4 lightViewMatrix = lookAt(frustumCenter - (lightDir * radius), frustumCenter, camera->WorldUp());
            mat4 lightOrthoMatrix = ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, maxExtents.z - minExtents.z, -clipRange);

            // Store split distance and matrix in cascade
            m_cascades[i] = lightOrthoMatrix * lightViewMatrix;
            m_cascadePlanes[i] = ExtractFrustumPlanes(m_cascades[i]);
            m_viewZ[i] = (camera->GetNearPlane() + splitDist * clipRange) * 1.5f;

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

        if (!m_scene->HasDrawInfo())
        {
            ClearDepths(cmd);
        }
        else
        {
            uint32_t frame = RHII.GetFrameIndex();
            BuildShadowIndirects(frame);

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
                cmd->BindIndexBuffer(m_scene->GetBuffer(), 0);
                cmd->BindVertexBuffer(m_scene->GetBuffer(), m_scene->GetPositionsOffset());
                cmd->SetConstants(pushConstants);
                cmd->PushConstants();
                if (!m_shadowIndirects.empty() && m_shadowIndirectCounts[i] > 0)
                    cmd->DrawIndexedIndirect(m_shadowIndirects[i][frame], 0, m_shadowIndirectCounts[i]);
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

    void ShadowPass::EnsureShadowIndirectCapacity(uint32_t meshCount, uint32_t cascades)
    {
        uint32_t frameCount = static_cast<uint32_t>(RHII.GetSwapchainImageCount());

        if (m_shadowIndirects.empty())
        {
            m_shadowIndirects.resize(cascades);
            m_shadowCmdScratch.resize(cascades);
            m_shadowIndirectCounts.resize(cascades, 0);
            for (uint32_t c = 0; c < cascades; c++)
                m_shadowIndirects[c].resize(frameCount, nullptr);
        }

        if (meshCount <= m_shadowIndirectCapacity)
            return;

        m_shadowIndirectCapacity = meshCount;
        size_t byteSize = meshCount * sizeof(vk::DrawIndexedIndirectCommand);
        for (uint32_t c = 0; c < cascades; c++)
        {
            for (uint32_t f = 0; f < frameCount; f++)
            {
                Buffer::Destroy(m_shadowIndirects[c][f]);
                m_shadowIndirects[c][f] = Buffer::Create(
                    byteSize,
                    vk::BufferUsageFlagBits2::eIndirectBuffer,
                    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                    "ShadowIndirect_c" + std::to_string(c) + "_f" + std::to_string(f));
                m_shadowIndirects[c][f]->Map();
            }
            m_shadowCmdScratch[c].reserve(meshCount);
        }
    }

    void ShadowPass::BuildShadowIndirects(uint32_t frame)
    {
        const uint32_t cascades = static_cast<uint32_t>(m_cascadePlanes.size());
        if (cascades == 0)
            return;

        const auto &allCmds = m_scene->GetIndirectCommands();
        const uint32_t meshCount = static_cast<uint32_t>(allCmds.size());
        if (meshCount == 0)
            return;

        EnsureShadowIndirectCapacity(meshCount, cascades);

        // Iterate ALL nodes instead of camera-culled draw infos.
        // Shadow casters outside the camera frustum must still be rendered
        // into shadow maps — otherwise shadows disappear when casters are
        // culled by the camera but still visible via the light's projection.
        const uint32_t nodeCount = m_scene->GetNodeCount();

        for (uint32_t c = 0; c < cascades; c++)
        {
            const std::array<vec4, 6> &planes = m_cascadePlanes[c];
            auto &scratch = m_shadowCmdScratch[c];
            scratch.clear();

            for (uint32_t n = 0; n < nodeCount; n++)
            {
                NodeId *node = m_scene->GetNodeId(n);
                const NodeRuntime &rt = m_scene->GetNodeRuntime(node);
                if (!rt.hasUniformData)
                    continue;

                const AABB &aabb = rt.worldAABB;
                if (aabb.min.x > aabb.max.x)
                    continue; // degenerate (not yet uploaded)
                if (!AABBInFrustum(aabb, planes))
                    continue;

                const auto &refs = m_scene->GetMeshRefs(node);
                for (uint32_t slot = 0; slot < static_cast<uint32_t>(refs.size()); slot++)
                {
                    if (slot >= static_cast<uint32_t>(rt.meshRefIndirect.size()))
                        break;
                    uint32_t cmdIdx = rt.meshRefIndirect[slot];
                    if (cmdIdx < meshCount)
                        scratch.push_back(allCmds[cmdIdx]);
                }
            }

            m_shadowIndirectCounts[c] = static_cast<uint32_t>(scratch.size());
            if (!scratch.empty())
            {
                Buffer *buf = m_shadowIndirects[c][frame];
                std::memcpy(buf->Data(), scratch.data(),
                            scratch.size() * sizeof(vk::DrawIndexedIndirectCommand));
                buf->Flush();
            }
        }
    }

    void ShadowPass::DestroyShadowIndirects()
    {
        for (auto &frameBuffers : m_shadowIndirects)
            for (auto *buf : frameBuffers)
                Buffer::Destroy(buf);
        m_shadowIndirects.clear();
        m_shadowCmdScratch.clear();
        m_shadowIndirectCounts.clear();
        m_shadowIndirectCapacity = 0;
    }

    void ShadowPass::Destroy()
    {
        DestroyShadowIndirects();

        for (auto &texture : m_textures)
            Image::Destroy(texture);

        Sampler::Destroy(m_sampler);

        for (auto &uniform : m_uniforms)
            Buffer::Destroy(uniform);
    }
} // namespace pe
