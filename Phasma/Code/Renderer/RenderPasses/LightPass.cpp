#include "Renderer/RenderPasses/LightPass.h"
#include "Renderer/RenderPasses/ShadowPass.h"
#include "Renderer/RHI.h"
#include "Renderer/Command.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Image.h"
#include "Renderer/Buffer.h"
#include "Renderer/RenderPass.h"
#include "Shader/Shader.h"
#include "Shader/Reflection.h"
#include "Systems/RendererSystem.h"
#include "Systems/LightSystem.h"
#include "Systems/CameraSystem.h"

namespace pe
{
    void LightOpaquePass::Init()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();

        m_srmRT = rs->GetRenderTarget("srm"); // Specular Roughness Metallic
        m_normalRT = rs->GetRenderTarget("normal");
        m_albedoRT = rs->GetRenderTarget("albedo");
        m_velocityRT = rs->GetRenderTarget("velocity");
        m_emissiveRT = rs->GetRenderTarget("emissive");
        m_viewportRT = rs->GetRenderTarget("viewport");
        m_ssaoRT = rs->GetRenderTarget("ssao");
        m_depthStencilRT = rs->GetDepthStencilTarget("depthStencil");
        m_transparencyRT = rs->GetRenderTarget("transparency");

        m_attachments.resize(1);
        m_attachments[0] = {};
        m_attachments[0].image = m_viewportRT;
        m_attachments[0].loadOp = AttachmentLoadOp::Clear;
    }

    void LightOpaquePass::UpdatePassInfo()
    {
        const std::vector<Define> definesFrag{
            Define{"SHADOWMAP_CASCADES", std::to_string(Settings::Get<GlobalSettings>().num_cascades)},
            Define{"SHADOWMAP_SIZE", std::to_string((float)Settings::Get<GlobalSettings>().shadow_map_size)},
            Define{"SHADOWMAP_TEXEL_SIZE", std::to_string(1.0f / (float)Settings::Get<GlobalSettings>().shadow_map_size)},
            Define{"MAX_POINT_LIGHTS", std::to_string(MAX_POINT_LIGHTS)},
            Define{"MAX_SPOT_LIGHTS", std::to_string(MAX_SPOT_LIGHTS)}};

        // Opaque light pass
        m_passInfo->name = "lighting_opaque_pipeline";
        m_passInfo->pVertShader = Shader::Create("Shaders/Common/Quad.hlsl", ShaderStage::VertexBit);
        m_passInfo->pFragShader = Shader::Create("Shaders/Gbuffer/LightingPS.hlsl", ShaderStage::FragmentBit, definesFrag);
        m_passInfo->dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        m_passInfo->colorBlendAttachments = {PipelineColorBlendAttachmentState::Default};
        m_passInfo->colorFormats = {m_viewportRT->GetFormat()};
        m_passInfo->depthTestEnable = false;
        m_passInfo->depthWriteEnable = false;
        m_passInfo->stencilTestEnable = false;
        m_passInfo->ReflectDescriptors();
        m_passInfo->UpdateHash();
    }

    void LightOpaquePass::CreateUniforms(CommandBuffer *cmd)
    {
        const std::string path = Path::Assets + "Objects/ibl_brdf_lut.png";
        m_ibl_brdf_lut = Image::LoadRGBA8(cmd, path);

        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
        {
            m_uniform[i] = Buffer::Create(
                RHII.AlignUniform(sizeof(LightPassUBO)),
                BufferUsage::UniformBufferBit,
                AllocationCreate::HostAccessSequentialWriteBit,
                "Gbuffer_uniform_buffer");
            m_uniform[i]->Map();
            m_uniform[i]->Zero();
            m_uniform[i]->Flush();
            m_uniform[i]->Unmap();
        }

        UpdateDescriptorSets();
    }

    void LightOpaquePass::UpdateDescriptorSets()
    {
        ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();
        std::vector<ImageViewApiHandle> views(shadows.m_textures.size());
        for (uint32_t i = 0; i < shadows.m_textures.size(); i++)
            views[i] = shadows.m_textures[i]->GetSRV();

        bool shadowsEnabled = Settings::Get<GlobalSettings>().shadows;
        const SkyBox &skybox = shadowsEnabled ? GetGlobalSystem<RendererSystem>()->GetSkyBoxDay() : GetGlobalSystem<RendererSystem>()->GetSkyBoxNight();

        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
        {
            auto &sets = m_passInfo->GetDescriptors(i);

            auto *DSet = sets[0];
            DSet->SetImageView(0, m_depthStencilRT->GetSRV(), m_depthStencilRT->GetSampler()->ApiHandle());
            DSet->SetImageView(1, m_normalRT->GetSRV(), m_normalRT->GetSampler()->ApiHandle());
            DSet->SetImageView(2, m_albedoRT->GetSRV(), m_albedoRT->GetSampler()->ApiHandle());
            DSet->SetImageView(3, m_srmRT->GetSRV(), m_srmRT->GetSampler()->ApiHandle());
            DSet->SetBuffer(4, GetGlobalSystem<LightSystem>()->GetUniform(i));
            DSet->SetImageView(5, m_ssaoRT->GetSRV(), m_ssaoRT->GetSampler()->ApiHandle());
            DSet->SetImageView(6, m_emissiveRT->GetSRV(), m_emissiveRT->GetSampler()->ApiHandle());
            DSet->SetBuffer(7, m_uniform[i]);
            DSet->SetImageView(8, m_transparencyRT->GetSRV(), m_transparencyRT->GetSampler()->ApiHandle());
            DSet->SetImageView(9, m_ibl_brdf_lut->GetSRV(), m_ibl_brdf_lut->GetSampler()->ApiHandle());
            DSet->Update();

            auto *DSetShadows = sets[1];
            DSetShadows->SetBuffer(0, shadows.m_uniforms[i]);
            DSetShadows->SetImageViews(1, views, {});
            DSetShadows->SetSampler(2, shadows.m_sampler->ApiHandle());
            DSetShadows->Update();

            auto *DSetSkybox = sets[2];
            DSetSkybox->SetImageView(0, skybox.GetCubeMap()->GetSRV(), skybox.GetCubeMap()->GetSampler()->ApiHandle());
            DSetSkybox->Update();
        }
    }

    void LightOpaquePass::Update(Camera *camera)
    {
        const auto &gSettings = Settings::Get<GlobalSettings>();

        m_ubo.invViewProj = camera->GetInvViewProjection();
        m_ubo.ssao = gSettings.ssao;
        m_ubo.ssr = gSettings.ssr;
        m_ubo.tonemapping = gSettings.tonemapping;
        m_ubo.fsr2 = gSettings.fsr2;
        m_ubo.IBL = gSettings.IBL;
        m_ubo.IBL_intensity = gSettings.IBL_intensity;
        m_ubo.volumetric = gSettings.volumetric;
        m_ubo.volumetric_steps = gSettings.volumetric_steps;
        m_ubo.volumetric_dither_strength = gSettings.volumetric_dither_strength;
        m_ubo.lights_intensity = gSettings.lights_intensity;
        m_ubo.lights_range = gSettings.lights_range;
        m_ubo.fog = gSettings.fog;
        m_ubo.fog_thickness = gSettings.fog_thickness;
        m_ubo.fog_max_height = gSettings.fog_max_height;
        m_ubo.fog_ground_thickness = gSettings.fog_ground_thickness;
        m_ubo.shadows = gSettings.shadows;

        MemoryRange mr{};
        mr.data = &m_ubo;
        mr.size = sizeof(m_ubo);
        mr.offset = 0;
        m_uniform[RHII.GetFrameIndex()]->Copy(1, &mr, false);
    }

    void LightOpaquePass::PassBarriers(CommandBuffer *cmd)
    {
        bool shadowsEnabled = Settings::Get<GlobalSettings>().shadows;
        ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();

        std::vector<ImageBarrierInfo> barriers(8 + (shadowsEnabled ? shadows.m_textures.size() : 0));
        for (size_t i = 0; i < barriers.size(); i++)
        {
            barriers[i].layout = ImageLayout::ShaderReadOnly;
            barriers[i].stageFlags = PipelineStage::FragmentShaderBit;
            barriers[i].accessMask = Access::ShaderReadBit;
        }

        barriers[0].image = m_depthStencilRT;
        barriers[1].image = m_normalRT;
        barriers[2].image = m_albedoRT;
        barriers[3].image = m_srmRT;
        barriers[4].image = m_velocityRT;
        barriers[5].image = m_emissiveRT;
        barriers[6].image = m_ssaoRT;
        barriers[7].image = m_transparencyRT;

        if (shadowsEnabled)
        {
            for (uint32_t i = 0; i < shadows.m_textures.size(); i++)
                barriers[i + 8].image = shadows.m_textures[i];
        }

        cmd->ImageBarriers(barriers);
    }

    void LightOpaquePass::Draw(CommandBuffer *cmd)
    {
        bool shadowsEnabled = m_ubo.shadows;
        uint32_t numCascades = Settings::Get<GlobalSettings>().num_cascades;
        ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();

        cmd->SetConstantAt(0, shadowsEnabled ? 1.0f : 0.0f); // Shadow cast
        cmd->SetConstantAt(1, MAX_POINT_LIGHTS);             // num point lights
        cmd->SetConstantAt(2, m_viewportRT->GetWidth_f());   // framebuffer width
        cmd->SetConstantAt(3, m_viewportRT->GetHeight_f());  // framebuffer height
        cmd->SetConstantAt(4, 0u);                           // is transparent pass
        if (shadowsEnabled)
        {
            for (uint32_t i = 0; i < numCascades; i++)
                cmd->SetConstantAt(i + 5, shadows.m_viewZ[i]); // shadowmap cascade distances
        }

        PassBarriers(cmd);

        cmd->BeginPass(1, m_attachments.data(), "LightOpaquePass");
        cmd->BindPipeline(*m_passInfo);
        cmd->SetViewport(0.f, 0.f, m_viewportRT->GetWidth_f(), m_viewportRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_viewportRT->GetWidth(), m_viewportRT->GetHeight());
        cmd->PushConstants();
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
    }

    void LightOpaquePass::Resize(uint32_t width, uint32_t height)
    {
        Init();
        UpdateDescriptorSets();
    }

    void LightOpaquePass::Destroy()
    {
        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
            Buffer::Destroy(m_uniform[i]);
    }

    void LightTransparentPass::Init()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();

        m_srmRT = rs->GetRenderTarget("srm"); // Specular Roughness Metallic
        m_normalRT = rs->GetRenderTarget("normal");
        m_albedoRT = rs->GetRenderTarget("albedo");
        m_velocityRT = rs->GetRenderTarget("velocity");
        m_emissiveRT = rs->GetRenderTarget("emissive");
        m_viewportRT = rs->GetRenderTarget("viewport");
        m_ssaoRT = rs->GetRenderTarget("ssao");
        m_depthStencilRT = rs->GetDepthStencilTarget("depthStencil");
        m_transparencyRT = rs->GetRenderTarget("transparency");

        m_attachments.resize(1);
        m_attachments[0] = {};
        m_attachments[0].image = m_viewportRT;
        m_attachments[0].loadOp = AttachmentLoadOp::Load;
    }

    void LightTransparentPass::UpdatePassInfo()
    {
        ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();

        const std::vector<Define> definesFrag{
            Define{"SHADOWMAP_CASCADES", std::to_string(Settings::Get<GlobalSettings>().num_cascades)},
            Define{"SHADOWMAP_SIZE", std::to_string((float)Settings::Get<GlobalSettings>().shadow_map_size)},
            Define{"SHADOWMAP_TEXEL_SIZE", std::to_string(1.0f / (float)Settings::Get<GlobalSettings>().shadow_map_size)},
            Define{"MAX_POINT_LIGHTS", std::to_string(MAX_POINT_LIGHTS)},
            Define{"MAX_SPOT_LIGHTS", std::to_string(MAX_SPOT_LIGHTS)}};

        m_passInfo->name = "lighting_transparent_pipeline";
        m_passInfo->pVertShader = Shader::Create("Shaders/Common/Quad.hlsl", ShaderStage::VertexBit);
        m_passInfo->pFragShader = Shader::Create("Shaders/Gbuffer/LightingPS.hlsl", ShaderStage::FragmentBit, definesFrag);
        m_passInfo->dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        m_passInfo->colorBlendAttachments = {PipelineColorBlendAttachmentState::Default};
        m_passInfo->blendEnable = true;
        m_passInfo->colorFormats = {m_viewportRT->GetFormat()};
        m_passInfo->depthTestEnable = false;
        m_passInfo->depthWriteEnable = false;
        m_passInfo->ReflectDescriptors();
        m_passInfo->UpdateHash();
    }

    void LightTransparentPass::CreateUniforms(CommandBuffer *cmd)
    {
        const std::string path = Path::Assets + "Objects/ibl_brdf_lut.png";
        m_ibl_brdf_lut = Image::LoadRGBA8(cmd, path);

        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
        {
            m_uniform[i] = Buffer::Create(
                RHII.AlignUniform(sizeof(LightPassUBO)),
                BufferUsage::UniformBufferBit,
                AllocationCreate::HostAccessSequentialWriteBit,
                "Gbuffer_uniform_buffer");
            m_uniform[i]->Map();
            m_uniform[i]->Zero();
            m_uniform[i]->Flush();
            m_uniform[i]->Unmap();
        }

        UpdateDescriptorSets();
    }

    void LightTransparentPass::UpdateDescriptorSets()
    {
        ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();
        std::vector<ImageViewApiHandle> views(shadows.m_textures.size());
        for (uint32_t i = 0; i < shadows.m_textures.size(); i++)
            views[i] = shadows.m_textures[i]->GetSRV();

        bool shadowsEnabled = Settings::Get<GlobalSettings>().shadows;
        const SkyBox &skybox = shadowsEnabled ? GetGlobalSystem<RendererSystem>()->GetSkyBoxDay() : GetGlobalSystem<RendererSystem>()->GetSkyBoxNight();

        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
        {
            auto &sets = m_passInfo->GetDescriptors(i);

            auto *DSet = sets[0];
            DSet->SetImageView(0, m_depthStencilRT->GetSRV(), m_depthStencilRT->GetSampler()->ApiHandle());
            DSet->SetImageView(1, m_normalRT->GetSRV(), m_normalRT->GetSampler()->ApiHandle());
            DSet->SetImageView(2, m_albedoRT->GetSRV(), m_albedoRT->GetSampler()->ApiHandle());
            DSet->SetImageView(3, m_srmRT->GetSRV(), m_srmRT->GetSampler()->ApiHandle());
            DSet->SetBuffer(4, GetGlobalSystem<LightSystem>()->GetUniform(i));
            DSet->SetImageView(5, m_ssaoRT->GetSRV(), m_ssaoRT->GetSampler()->ApiHandle());
            DSet->SetImageView(6, m_emissiveRT->GetSRV(), m_emissiveRT->GetSampler()->ApiHandle());
            DSet->SetBuffer(7, m_uniform[i]);
            DSet->SetImageView(8, m_transparencyRT->GetSRV(), m_transparencyRT->GetSampler()->ApiHandle());
            DSet->SetImageView(9, m_ibl_brdf_lut->GetSRV(), m_ibl_brdf_lut->GetSampler()->ApiHandle());
            DSet->Update();

            auto *DSetShadows = sets[1];
            DSetShadows->SetBuffer(0, shadows.m_uniforms[i]);
            DSetShadows->SetImageViews(1, views, {});
            DSetShadows->SetSampler(2, shadows.m_sampler->ApiHandle());
            DSetShadows->Update();

            auto *DSetSkybox = sets[2];
            DSetSkybox->SetImageView(0, skybox.GetCubeMap()->GetSRV(), skybox.GetCubeMap()->GetSampler()->ApiHandle());
            DSetSkybox->Update();
        }
    }

    void LightTransparentPass::Update(Camera *camera)
    {
        const auto &gSettings = Settings::Get<GlobalSettings>();

        m_ubo.invViewProj = camera->GetInvViewProjection();
        m_ubo.ssao = gSettings.ssao;
        m_ubo.ssr = gSettings.ssr;
        m_ubo.tonemapping = gSettings.tonemapping;
        m_ubo.fsr2 = gSettings.fsr2;
        m_ubo.IBL = gSettings.IBL;
        m_ubo.IBL_intensity = gSettings.IBL_intensity;
        m_ubo.volumetric = gSettings.volumetric;
        m_ubo.volumetric_steps = gSettings.volumetric_steps;
        m_ubo.volumetric_dither_strength = gSettings.volumetric_dither_strength;
        m_ubo.lights_intensity = gSettings.lights_intensity;
        m_ubo.lights_range = gSettings.lights_range;
        m_ubo.fog = gSettings.fog;
        m_ubo.fog_thickness = gSettings.fog_thickness;
        m_ubo.fog_max_height = gSettings.fog_max_height;
        m_ubo.fog_ground_thickness = gSettings.fog_ground_thickness;
        m_ubo.shadows = gSettings.shadows;

        MemoryRange mr{};
        mr.data = &m_ubo;
        mr.size = sizeof(m_ubo);
        mr.offset = 0;
        m_uniform[RHII.GetFrameIndex()]->Copy(1, &mr, false);
    }

    void LightTransparentPass::PassBarriers(CommandBuffer *cmd)
    {
        bool shadowsEnabled = Settings::Get<GlobalSettings>().shadows;
        RendererSystem &rs = *GetGlobalSystem<RendererSystem>();
        const SkyBox &skybox = shadowsEnabled ? rs.GetSkyBoxDay() : rs.GetSkyBoxNight();
        ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();

        std::vector<ImageBarrierInfo> barriers(8 + shadows.m_textures.size());
        for (size_t i = 0; i < barriers.size(); i++)
        {
            barriers[i].layout = ImageLayout::ShaderReadOnly;
            barriers[i].stageFlags = PipelineStage::FragmentShaderBit;
            barriers[i].accessMask = Access::ShaderReadBit;
        }

        barriers[0].image = m_depthStencilRT;
        barriers[1].image = m_normalRT;
        barriers[2].image = m_albedoRT;
        barriers[3].image = m_srmRT;
        barriers[4].image = m_velocityRT;
        barriers[5].image = m_emissiveRT;
        barriers[6].image = m_ssaoRT;
        barriers[7].image = m_transparencyRT;

        for (uint32_t i = 0; i < shadows.m_textures.size(); i++)
        {
            barriers[i + 8].image = shadows.m_textures[i];
        }

        cmd->ImageBarriers(barriers);
    }

    void LightTransparentPass::Draw(CommandBuffer *cmd)
    {
        bool shadowsEnabled = m_ubo.shadows;
        uint32_t shadowmapCascades = Settings::Get<GlobalSettings>().num_cascades;

        RendererSystem &rs = *GetGlobalSystem<RendererSystem>();
        const SkyBox &skybox = shadowsEnabled ? rs.GetSkyBoxDay() : rs.GetSkyBoxNight();
        ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();

        cmd->SetConstantAt(0, shadowsEnabled ? 1.0f : 0.0f); // Shadow cast
        cmd->SetConstantAt(1, MAX_POINT_LIGHTS);             // num point lights
        cmd->SetConstantAt(2, m_viewportRT->GetWidth_f());   // framebuffer width
        cmd->SetConstantAt(3, m_viewportRT->GetHeight_f());  // framebuffer height
        cmd->SetConstantAt(4, 1u);                           // transparent pass
        for (uint32_t i = 0; i < shadowmapCascades; i++)
            cmd->SetConstantAt(i + 5, shadows.m_viewZ[i]); // shadowmap cascade distances

        PassBarriers(cmd);

        cmd->BeginPass(1, m_attachments.data(), "LightTransparentPass");
        cmd->BindPipeline(*m_passInfo);
        cmd->SetViewport(0.f, 0.f, m_viewportRT->GetWidth_f(), m_viewportRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_viewportRT->GetWidth(), m_viewportRT->GetHeight());
        cmd->PushConstants();
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
    }

    void LightTransparentPass::Resize(uint32_t width, uint32_t height)
    {
        Init();
        UpdateDescriptorSets();
    }

    void LightTransparentPass::Destroy()
    {
        for (uint32_t i = 0; i < SWAPCHAIN_IMAGES; i++)
            Buffer::Destroy(m_uniform[i]);
    }
}
