#include "Renderer/RenderPasses/ShadowPass.h"
#include "Shader/Shader.h"
#include "Renderer/RHI.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Image.h"
#include "Renderer/Buffer.h"
#include "Renderer/Pipeline.h"
#include "Renderer/Command.h"
#include "Systems/RendererSystem.h"
#include "Scene/Geometry.h"
#include "Scene/Model.h"

namespace pe
{
    void ShadowPass::Init()
    {
        m_textures.resize(Settings::Get<GlobalSettings>().shadow_map_cascades);
        int i = 0;
        for (auto *&texture : m_textures)
        {
            ImageCreateInfo info{};
            info.format = RHII.GetDepthFormat();
            info.initialLayout = ImageLayout::Undefined;
            info.width = Settings::Get<GlobalSettings>().shadow_map_size;
            info.height = Settings::Get<GlobalSettings>().shadow_map_size;
            info.tiling = ImageTiling::Optimal;
            info.usage = ImageUsage::DepthStencilAttachmentBit | ImageUsage::SampledBit;
            info.clearColor = vec4(Color::Depth, Color::Stencil, 0.0f, 1.0f);
            info.name = "ShadowMap_" + std::to_string(i++);
            texture = Image::Create(info);

            texture->CreateRTV();
            texture->CreateSRV(ImageViewType::Type2D);
        }

        SamplerCreateInfo samplerInfo{};
        samplerInfo.addressModeU = SamplerAddressMode::ClampToEdge;
        samplerInfo.addressModeV = SamplerAddressMode::ClampToEdge;
        samplerInfo.addressModeW = SamplerAddressMode::ClampToEdge;
        samplerInfo.maxAnisotropy = 1.f;
        samplerInfo.borderColor = BorderColor::FloatOpaqueWhite;
        samplerInfo.compareEnable = 1;
        samplerInfo.compareOp = CompareOp::GreaterOrEqual;
        samplerInfo.name = "Sampler_ClampToEdge_GE_FOW";
        m_sampler = Sampler::Create(samplerInfo);

        m_attachments.resize(1);
        m_attachments[0] = {};
        m_attachments[0].initialLayout = ImageLayout::Undefined;
        m_attachments[0].finalLayout = ImageLayout::ShaderReadOnly;
        m_attachments[0].loadOp = AttachmentLoadOp::Clear;
        m_attachments[0].storeOp = AttachmentStoreOp::Store;
    }

    void ShadowPass::UpdatePassInfo()
    {
        m_passInfo->name = "shadows_pipeline";
        m_passInfo->pVertShader = Shader::Create("Shaders/Shadows/ShadowsVS.hlsl", ShaderStage::VertexBit);
        m_passInfo->dynamicStates = {DynamicState::Viewport, DynamicState::Scissor, DynamicState::DepthBias};
        m_passInfo->cullMode = CullMode::Front;
        m_passInfo->depthFormat = RHII.GetDepthFormat();
        m_passInfo->ReflectDescriptors();
        m_passInfo->UpdateHash();
    }

    void ShadowPass::CreateUniforms(CommandBuffer *cmd)
    {
        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
        {
            m_uniforms[i] = Buffer::Create(
                RHII.AlignUniform(Settings::Get<GlobalSettings>().shadow_map_cascades * sizeof(mat4)),
                BufferUsage::UniformBufferBit,
                AllocationCreate::HostAccessSequentialWriteBit,
                "Shadows_uniform_buffer");
            m_uniforms[i]->Map();
            m_uniforms[i]->Zero();
            m_uniforms[i]->Flush();
        }
    }

    void ShadowPass::UpdateDescriptorSets()
    {
    }

    void ShadowPass::Update(Camera *camera)
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        if (gSettings.shadows)
        {
            CalculateCascades(camera);

            MemoryRange mr{};
            mr.data = m_cascades.data();
            mr.size = gSettings.shadow_map_cascades * sizeof(mat4);
            mr.offset = 0; // RHII.GetFrameDynamicOffset(uniformBuffer->Size(), RHII.GetFrameIndex());
            m_uniforms[RHII.GetFrameIndex()]->Copy(1, &mr, false);
        }
    }

    constexpr float cascadeSplitLambda = 0.95f;
    void ShadowPass::CalculateCascades(Camera *camera)
    {
        uint32_t cascades = Settings::Get<GlobalSettings>().shadow_map_cascades;
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

    void ShadowPass::DrawNode(CommandBuffer *cmd, ModelGltf &model, int node, uint32_t cascade)
    {
        if (node < 0)
            return;

        int mesh = model.nodes[node].mesh;
        if (mesh < 0)
            return;

        cmd->SetConstantAt(16, GetUboDataOffset(model.m_meshesInfo[mesh].dataOffset)); // mesh index

        auto &primitivesInfo = model.m_meshesInfo[mesh].primitivesInfo;

        for (int primitive = 0; primitive < model.meshes[mesh].primitives.size(); ++primitive)
        {
            PrimitiveInfo &primitiveInfo = model.m_meshesInfo[mesh].primitivesInfo[primitive];

            cmd->PushConstants();
            cmd->DrawIndexed(
                primitiveInfo.indicesCount,
                1,
                primitiveInfo.indexOffset,
                primitiveInfo.vertexOffset,
                0);
        }

        for (int child : model.nodes[node].children)
        {
            DrawNode(cmd, model, child, cascade);
        }
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
            uint32_t cascades = Settings::Get<GlobalSettings>().shadow_map_cascades;
            for (uint32_t i = 0; i < cascades; i++)
            {
                PassInfo &passInfo = *m_passInfo;
                Attachment &attachment = m_attachments[0];
                attachment.image = m_textures[i];

                cmd->BeginPass(m_attachments, "Cascade_" + std::to_string(i));
                cmd->SetViewport(0.f, 0.f, attachment.image->GetWidth_f(), attachment.image->GetHeight_f());
                cmd->SetScissor(0, 0, attachment.image->GetWidth(), attachment.image->GetHeight());
                cmd->BindPipeline(passInfo);
                cmd->BindIndexBuffer(m_geometry->GetBuffer(), 0);
                cmd->BindVertexBuffer(m_geometry->GetBuffer(), m_geometry->GetPositionsOffset());
                cmd->SetConstantAt(0, m_cascades[i]); // cascade view projection
                cmd->SetConstantAt(17, 0u);           // joints count

                for (auto &modelPtr : m_geometry->GetModels())
                {
                    ModelGltf &model = *modelPtr;
                    if (!model.render)
                        continue;

                    for (auto &scene : model.scenes)
                    {
                        for (int node : scene.nodes)
                        {
                            DrawNode(cmd, model, node, i);
                        }
                    }
                }
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

        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
            Buffer::Destroy(m_uniforms[i]);
    }
}
