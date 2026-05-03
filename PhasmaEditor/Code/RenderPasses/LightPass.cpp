#include "LightPass.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/RenderPass.h"
#include "API/Shader.h"
#include "API/Vulkan/VulkanImageImpl.h"
#include "Camera/Camera.h"
#include "ShadowPass.h"
#include "Systems/RendererSystem.h"

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
        m_attachments[0].loadOp = PE_LOAD_OP_CLEAR;

        m_uniforms.resize(RHII.GetSwapchainImageCount());
    }

    void LightOpaquePass::UpdatePassInfo()
    {
        const std::vector<Define> definesFrag{
            Define{"SHADOWMAP_CASCADES", std::to_string(Settings::Get<GlobalSettings>().num_cascades)},
            Define{"SHADOWMAP_SIZE", std::to_string((float)Settings::Get<GlobalSettings>().shadow_map_size)},
            Define{"SHADOWMAP_TEXEL_SIZE", std::to_string(1.0f / (float)Settings::Get<GlobalSettings>().shadow_map_size)}};

        // Opaque light pass
        m_passInfo->name = "lighting_opaque_pipeline";
        m_passInfo->pVertShader = Shader::Create({.sourcePath = Path::Assets + "Shaders/Common/Quad.hlsl", .entryPoint = "mainVS", .stage = PE_SHADER_STAGE_VERTEX, .defines = std::vector<Define>{}});
        m_passInfo->pFragShader = Shader::Create({.sourcePath = Path::Assets + "Shaders/Gbuffer/LightingPS.hlsl", .entryPoint = "mainPS", .stage = PE_SHADER_STAGE_FRAGMENT, .defines = definesFrag});
        m_passInfo->dynamicStates = {PE_DYNAMIC_STATE_VIEWPORT, PE_DYNAMIC_STATE_SCISSOR};
        m_passInfo->colorBlendAttachments = {BlendState::Default};
        m_passInfo->colorFormats = {m_viewportRT->GetFormat()};
        m_passInfo->depthTestEnable = false;
        m_passInfo->depthWriteEnable = false;
        m_passInfo->stencilTestEnable = false;
        m_passInfo->Update();
    }

    void LightOpaquePass::CreateUniforms(CommandBuffer *cmd)
    {

        for (auto &uniform : m_uniforms)
        {
            uniform = Buffer::Create({
                .size = RHII.AlignUniform(sizeof(LightPassUBO)),
                .usage = PE_BUFFER_USAGE_UNIFORM_BUFFER,
                .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU,
                .name = "Gbuffer_uniform_buffer",
            });
            uniform->Map();
            uniform->Zero();
            uniform->Flush();
            uniform->Unmap();
        }

        UpdateDescriptorSets();
    }

    void LightOpaquePass::UpdateDescriptorSets()
    {
        ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();
        std::vector<ImageView *> views(shadows.m_textures.size());
        for (uint32_t i = 0; i < shadows.m_textures.size(); i++)
            views[i] = shadows.m_textures[i]->GetSRV();

        const SkyBox &skybox = Settings::Get<GlobalSettings>().day
                                   ? GetGlobalSystem<RendererSystem>()->GetSkyBoxDay()
                                   : GetGlobalSystem<RendererSystem>()->GetSkyBoxNight();

        Scene &scene = GetGlobalSystem<RendererSystem>()->GetScene();
        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
        {
            auto *ibl_brdf_lut = GetGlobalSystem<RendererSystem>()->GetIBL_LUT();
            auto &sets = m_passInfo->GetDescriptors(i);

            auto *DSet = sets[0];
            DSet->SetImageView(0, m_depthStencilRT->GetSRV(), m_depthStencilRT->GetSampler());
            DSet->SetImageView(1, m_normalRT->GetSRV(), m_normalRT->GetSampler());
            DSet->SetImageView(2, m_albedoRT->GetSRV(), m_albedoRT->GetSampler());
            DSet->SetImageView(3, m_srmRT->GetSRV(), m_srmRT->GetSampler());
            DSet->SetBuffer(4, scene.GetLightUniform(i));
            DSet->SetImageView(5, m_ssaoRT->GetSRV(), m_ssaoRT->GetSampler());
            DSet->SetImageView(6, m_emissiveRT->GetSRV(), m_emissiveRT->GetSampler());
            DSet->SetBuffer(7, m_uniforms[i]);
            DSet->SetImageView(8, m_transparencyRT->GetSRV(), m_transparencyRT->GetSampler());
            DSet->SetImageView(9, ibl_brdf_lut->GetSRV(), ibl_brdf_lut->GetSampler());
            DSet->SetBuffer(10, scene.GetLightStorage(i));
            DSet->Update();

            auto *DSetShadows = sets[1];
            DSetShadows->SetBuffer(0, shadows.m_uniforms[i]);
            DSetShadows->SetImageViews(1, views, {});
            DSetShadows->SetSampler(2, shadows.m_sampler);
            DSetShadows->Update();

            auto *DSetSkybox = sets[2];
            DSetSkybox->SetImageView(0, skybox.GetCubeMap()->GetSRV(), skybox.GetCubeMap()->GetSampler());
            DSetSkybox->Update();
        }
    }

    void LightOpaquePass::Update()
    {
        const auto &gSettings = Settings::Get<GlobalSettings>();

        Camera *camera = GetGlobalSystem<RendererSystem>()->GetScene().GetActiveCamera();

        m_ubo.invViewProj = camera->GetInvViewProjection();
        m_ubo.camPos = vec4(camera->GetPosition(), 1.0f);
        const bool isDx12 = RHII.GetApi() == PE_GRAPHICS_API_DX12;
        m_ubo.ssao = !isDx12 && gSettings.ssao;
        m_ubo.ssr = !isDx12 && gSettings.ssr;
        m_ubo.tonemapping = !isDx12 && gSettings.tonemapping;
        m_ubo.fsr2 = !isDx12 && gSettings.taa;
        m_ubo.IBL = gSettings.IBL;
        m_ubo.IBL_intensity = gSettings.IBL_intensity;
        m_ubo.lights_intensity = gSettings.lights_intensity;
        m_ubo.shadows = gSettings.shadows;
        m_ubo.use_Disney_PBR = gSettings.use_Disney_PBR;

        BufferRange range{};
        range.data = &m_ubo;
        range.size = sizeof(m_ubo);
        range.offset = 0;
        m_uniforms[RHII.GetFrameIndex()]->Copy(1, &range, false);
    }

    void LightOpaquePass::DeclareInputs(RGBuilder &builder)
    {
        const bool isDx12 = RHII.GetApi() == PE_GRAPHICS_API_DX12;
        const bool shadowsEnabled = Settings::Get<GlobalSettings>().shadows;
        ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();

        builder.Read(m_depthStencilRT);
        builder.Read(m_normalRT);
        builder.Read(m_albedoRT);
        builder.Read(m_srmRT);
        builder.Read(m_velocityRT);
        builder.Read(m_emissiveRT);
        if (!isDx12)
            builder.Read(m_ssaoRT);
        builder.Read(m_transparencyRT);

        if (shadowsEnabled)
        {
            for (auto *tex : shadows.m_textures)
                builder.Read(tex);
        }
    }

    void LightOpaquePass::ExecutePass(CommandBuffer *cmd)
    {
        uint32_t shadowmapCascades = Settings::Get<GlobalSettings>().num_cascades;
        ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();

        Scene &scene = GetGlobalSystem<RendererSystem>()->GetScene();
        cmd->SetConstantAt(0, (uint32_t)scene.GetPointLights().size()); // num point lights
        cmd->SetConstantAt(1, (uint32_t)scene.GetSpotLights().size());  // num spot lights
        cmd->SetConstantAt(2, (uint32_t)scene.GetAreaLights().size());  // num area lights
        cmd->SetConstantAt(3, 0u);                                      // padding
        cmd->SetConstantAt(4, m_viewportRT->GetWidth_f());              // framebuffer width
        cmd->SetConstantAt(5, m_viewportRT->GetHeight_f());             // framebuffer height
        cmd->SetConstantAt(6, 0u);                                      // is transparent pass
        for (uint32_t i = 0; i < shadowmapCascades; i++)
            cmd->SetConstantAt(i + 7, shadows.m_viewZ[i]); // shadowmap cascade distances

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

        for (auto &uniform : m_uniforms)
            Buffer::Destroy(uniform);
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
        m_attachments[0].loadOp = PE_LOAD_OP_LOAD;

        m_uniforms.resize(RHII.GetSwapchainImageCount());
    }

    void LightTransparentPass::UpdatePassInfo()
    {
        const std::vector<Define> definesFrag{
            Define{"SHADOWMAP_CASCADES", std::to_string(Settings::Get<GlobalSettings>().num_cascades)},
            Define{"SHADOWMAP_SIZE", std::to_string((float)Settings::Get<GlobalSettings>().shadow_map_size)},
            Define{"SHADOWMAP_TEXEL_SIZE", std::to_string(1.0f / (float)Settings::Get<GlobalSettings>().shadow_map_size)}};

        m_passInfo->name = "lighting_transparent_pipeline";
        m_passInfo->pVertShader = Shader::Create({.sourcePath = Path::Assets + "Shaders/Common/Quad.hlsl", .entryPoint = "mainVS", .stage = PE_SHADER_STAGE_VERTEX, .defines = std::vector<Define>{}});
        m_passInfo->pFragShader = Shader::Create({.sourcePath = Path::Assets + "Shaders/Gbuffer/LightingPS.hlsl", .entryPoint = "mainPS", .stage = PE_SHADER_STAGE_FRAGMENT, .defines = definesFrag});
        m_passInfo->dynamicStates = {PE_DYNAMIC_STATE_VIEWPORT, PE_DYNAMIC_STATE_SCISSOR};
        m_passInfo->colorBlendAttachments = {BlendState::Default};
        m_passInfo->blendEnable = true;
        m_passInfo->colorFormats = {m_viewportRT->GetFormat()};
        m_passInfo->depthTestEnable = false;
        m_passInfo->depthWriteEnable = false;
        m_passInfo->Update();
    }

    void LightTransparentPass::CreateUniforms(CommandBuffer *cmd)
    {
        for (auto &uniform : m_uniforms)
        {
            uniform = Buffer::Create({
                .size = RHII.AlignUniform(sizeof(LightPassUBO)),
                .usage = PE_BUFFER_USAGE_UNIFORM_BUFFER,
                .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU,
                .name = "Gbuffer_uniform_buffer",
            });
            uniform->Map();
            uniform->Zero();
            uniform->Flush();
            uniform->Unmap();
        }

        UpdateDescriptorSets();
    }

    void LightTransparentPass::UpdateDescriptorSets()
    {
        ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();
        std::vector<ImageView *> views(shadows.m_textures.size());
        for (uint32_t i = 0; i < shadows.m_textures.size(); i++)
            views[i] = shadows.m_textures[i]->GetSRV();

        const SkyBox &skybox = Settings::Get<GlobalSettings>().day
                                   ? GetGlobalSystem<RendererSystem>()->GetSkyBoxDay()
                                   : GetGlobalSystem<RendererSystem>()->GetSkyBoxNight();

        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
        {
            auto &sets = m_passInfo->GetDescriptors(i);

            auto *DSet = sets[0];
            DSet->SetImageView(0, m_depthStencilRT->GetSRV(), m_depthStencilRT->GetSampler());
            DSet->SetImageView(1, m_normalRT->GetSRV(), m_normalRT->GetSampler());
            DSet->SetImageView(2, m_albedoRT->GetSRV(), m_albedoRT->GetSampler());
            DSet->SetImageView(3, m_srmRT->GetSRV(), m_srmRT->GetSampler());
            DSet->SetBuffer(4, GetGlobalSystem<RendererSystem>()->GetScene().GetLightUniform(i));
            DSet->SetImageView(5, m_ssaoRT->GetSRV(), m_ssaoRT->GetSampler());
            DSet->SetImageView(6, m_emissiveRT->GetSRV(), m_emissiveRT->GetSampler());
            DSet->SetBuffer(7, m_uniforms[i]);
            DSet->SetImageView(8, m_transparencyRT->GetSRV(), m_transparencyRT->GetSampler());
            auto *ibl_brdf_lut = GetGlobalSystem<RendererSystem>()->GetIBL_LUT();
            DSet->SetImageView(9, ibl_brdf_lut->GetSRV(), ibl_brdf_lut->GetSampler());
            DSet->SetBuffer(10, GetGlobalSystem<RendererSystem>()->GetScene().GetLightStorage(i));
            DSet->Update();

            auto *DSetShadows = sets[1];
            DSetShadows->SetBuffer(0, shadows.m_uniforms[i]);
            DSetShadows->SetImageViews(1, views);
            DSetShadows->SetSampler(2, shadows.m_sampler);
            DSetShadows->Update();

            auto *DSetSkybox = sets[2];
            DSetSkybox->SetImageView(0, skybox.GetCubeMap()->GetSRV(), skybox.GetCubeMap()->GetSampler());
            DSetSkybox->Update();
        }
    }

    void LightTransparentPass::Update()
    {
        const auto &gSettings = Settings::Get<GlobalSettings>();

        Camera *camera = GetGlobalSystem<RendererSystem>()->GetScene().GetActiveCamera();

        m_ubo.invViewProj = camera->GetInvViewProjection();
        m_ubo.camPos = vec4(camera->GetPosition(), 1.0f);
        const bool isDx12 = RHII.GetApi() == PE_GRAPHICS_API_DX12;
        m_ubo.ssao = !isDx12 && gSettings.ssao;
        m_ubo.ssr = !isDx12 && gSettings.ssr;
        m_ubo.tonemapping = !isDx12 && gSettings.tonemapping;
        m_ubo.fsr2 = !isDx12 && gSettings.taa;
        m_ubo.IBL = gSettings.IBL;
        m_ubo.IBL_intensity = gSettings.IBL_intensity;
        m_ubo.lights_intensity = gSettings.lights_intensity;
        m_ubo.shadows = gSettings.shadows;
        m_ubo.use_Disney_PBR = gSettings.use_Disney_PBR;

        BufferRange range{};
        range.data = &m_ubo;
        range.size = sizeof(m_ubo);
        range.offset = 0;
        m_uniforms[RHII.GetFrameIndex()]->Copy(1, &range, false);
    }

    void LightTransparentPass::DeclareInputs(RGBuilder &builder)
    {
        const bool isDx12 = RHII.GetApi() == PE_GRAPHICS_API_DX12;
        const bool shadowsEnabled = Settings::Get<GlobalSettings>().shadows;
        ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();

        builder.Read(m_depthStencilRT);
        builder.Read(m_normalRT);
        builder.Read(m_albedoRT);
        builder.Read(m_srmRT);
        builder.Read(m_velocityRT);
        builder.Read(m_emissiveRT);
        if (!isDx12)
            builder.Read(m_ssaoRT);
        builder.Read(m_transparencyRT);

        if (shadowsEnabled)
        {
            for (auto *tex : shadows.m_textures)
                builder.Read(tex);
        }
    }

    void LightTransparentPass::ExecutePass(CommandBuffer *cmd)
    {
        uint32_t shadowmapCascades = Settings::Get<GlobalSettings>().num_cascades;
        ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();

        Scene &scene = GetGlobalSystem<RendererSystem>()->GetScene();
        cmd->SetConstantAt(0, (uint32_t)scene.GetPointLights().size()); // num point lights
        cmd->SetConstantAt(1, (uint32_t)scene.GetSpotLights().size());  // num spot lights
        cmd->SetConstantAt(2, (uint32_t)scene.GetAreaLights().size());  // num area lights
        cmd->SetConstantAt(3, 0u);                                      // padding
        cmd->SetConstantAt(4, m_viewportRT->GetWidth_f());              // framebuffer width
        cmd->SetConstantAt(5, m_viewportRT->GetHeight_f());             // framebuffer height
        cmd->SetConstantAt(6, 1u);                                      // transparent pass
        for (uint32_t i = 0; i < shadowmapCascades; i++)
            cmd->SetConstantAt(i + 7, shadows.m_viewZ[i]); // shadowmap cascade distances

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
        for (auto &uniform : m_uniforms)
            Buffer::Destroy(uniform);
    }
} // namespace pe
