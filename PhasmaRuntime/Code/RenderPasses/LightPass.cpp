#include "LightPass.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/RenderGraph.h"
#include "API/RenderPass.h"
#include "API/Sampler.h"
#include "API/Shader.h"
#include "Camera/Camera.h"
#include "Render/SceneRendererHost.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "ShadowPass.h"
#include "Skybox/Skybox.h"

#include <algorithm>

namespace pe
{
    namespace
    {
        void DestroyLightShadowFallbackResources(std::vector<Buffer *> &uniforms,
                                                 Image *&texture,
                                                 Sampler *&sampler,
                                                 std::vector<ImageView *> &views)
        {
            for (auto *&uniform : uniforms)
                Buffer::Destroy(uniform);
            uniforms.clear();

            Image::Destroy(texture);
            Sampler::Destroy(sampler);
            views.clear();
        }

        void EnsureLightShadowFallbackResources(std::vector<Buffer *> &uniforms,
                                                Image *&texture,
                                                Sampler *&sampler,
                                                std::vector<ImageView *> &views,
                                                const std::string &namePrefix)
        {
            const uint32_t cascadeCount = std::max(1u, Settings::Get<GlobalSettings>().num_cascades);
            const uint32_t frameCount = RHII.GetSwapchainImageCount();

            if (!texture)
            {
                ImageDesc desc{};
                desc.format = RHII.GetDepthFormat();
                desc.width = 1;
                desc.height = 1;
                desc.usage = PE_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT | PE_IMAGE_USAGE_SAMPLED | PE_IMAGE_USAGE_TRANSFER_DST;
                desc.clearColor = vec4(Color::Depth, Color::Stencil, 0.0f, 1.0f);
                desc.name = namePrefix + "_ShadowFallback";
                texture = Image::Create(desc);
                texture->SetClearColor(desc.clearColor);
                texture->CreateSRV(PE_IMAGE_VIEW_TYPE_2D);
            }

            if (!sampler)
            {
                SamplerDesc samplerInfo = Sampler::CreateInfoInit();
                samplerInfo.addressModeU = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                samplerInfo.addressModeV = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                samplerInfo.addressModeW = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                samplerInfo.maxAnisotropy = 1.f;
                samplerInfo.borderColor = PE_SAMPLER_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
                samplerInfo.compareEnable = true;
                samplerInfo.compareOp = PE_COMPARE_OP_GREATER_OR_EQUAL;
                sampler = Sampler::Create(samplerInfo, namePrefix + "_ShadowFallbackSampler");
            }

            if (uniforms.size() != frameCount)
            {
                for (auto *&uniform : uniforms)
                    Buffer::Destroy(uniform);
                uniforms.resize(frameCount, nullptr);

                for (auto *&uniform : uniforms)
                {
                    uniform = Buffer::Create({
                        .size = RHII.AlignUniform(cascadeCount * sizeof(mat4)),
                        .usage = PE_BUFFER_USAGE_UNIFORM_BUFFER,
                        .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU,
                        .name = namePrefix + "_ShadowFallbackUniform",
                    });
                    uniform->Map();
                    uniform->Zero();
                    uniform->Flush();
                }
            }

            views.assign(cascadeCount, texture->GetSRV());
        }

        bool HasLiveShadowResources(const ShadowPass &shadows)
        {
            return shadows.HasLiveResources();
        }

        std::vector<ImageView *> GetLiveShadowViews(const ShadowPass &shadows)
        {
            return shadows.GetTextureViews();
        }
    } // namespace

    void LightOpaquePass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();

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
        EnsureLightShadowFallbackResources(m_shadowFallbackUniforms,
                                           m_shadowFallbackTexture,
                                           m_shadowFallbackSampler,
                                           m_shadowFallbackViews,
                                           "LightOpaque");
    }

    void LightOpaquePass::UpdatePassInfo()
    {
        const std::vector<Define> definesFrag{
            Define{"SHADOWMAP_CASCADES", std::to_string(Settings::Get<GlobalSettings>().num_cascades)},
            Define{"SHADOWMAP_SIZE", std::to_string((float)Settings::Get<GlobalSettings>().shadow_map_size)},
            Define{"SHADOWMAP_TEXEL_SIZE", std::to_string(1.0f / (float)Settings::Get<GlobalSettings>().shadow_map_size)}};

        // Opaque light pass
        m_passInfo->name = "lighting_opaque_pipeline";
        m_passInfo->pVertShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Common/Quad.hlsl", .entryPoint = "mainVS", .stage = PE_SHADER_STAGE_VERTEX, .defines = std::vector<Define>{}});
        m_passInfo->pFragShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Gbuffer/LightingPS.hlsl", .entryPoint = "mainPS", .stage = PE_SHADER_STAGE_FRAGMENT, .defines = definesFrag});
        m_passInfo->dynamicStates = {PE_DYNAMIC_STATE_VIEWPORT, PE_DYNAMIC_STATE_SCISSOR};
        m_passInfo->cullMode = PE_CULL_MODE_NONE;
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
        const bool useShadowResources = Settings::Get<GlobalSettings>().shadows && HasLiveShadowResources(shadows);
        EnsureLightShadowFallbackResources(m_shadowFallbackUniforms,
                                           m_shadowFallbackTexture,
                                           m_shadowFallbackSampler,
                                           m_shadowFallbackViews,
                                           "LightOpaque");
        const std::vector<ImageView *> shadowViews = useShadowResources ? GetLiveShadowViews(shadows) : m_shadowFallbackViews;
        Sampler *shadowSampler = useShadowResources ? shadows.GetSampler() : m_shadowFallbackSampler;

        SceneRendererHost &renderer = RequireActiveSceneRendererHost();
        const SkyBox &skybox = renderer.GetSkyBox();

        Scene &scene = *GetActiveScene();
        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
        {
            auto *ibl_brdf_lut = renderer.GetIBL_LUT();
            auto &sets = m_passInfo->GetDescriptors(i);

            auto *DSet = sets[0];
            Image *ssaoRT = m_ssaoRT ? m_ssaoRT : m_albedoRT;
            DSet->SetImageView(0, m_depthStencilRT->GetSRV(), m_depthStencilRT->GetSampler());
            DSet->SetImageView(1, m_normalRT->GetSRV(), m_normalRT->GetSampler());
            DSet->SetImageView(2, m_albedoRT->GetSRV(), m_albedoRT->GetSampler());
            DSet->SetImageView(3, m_srmRT->GetSRV(), m_srmRT->GetSampler());
            DSet->SetBuffer(4, scene.GetLightUniform(i));
            DSet->SetImageView(5, ssaoRT->GetSRV(), ssaoRT->GetSampler());
            DSet->SetImageView(6, m_emissiveRT->GetSRV(), m_emissiveRT->GetSampler());
            DSet->SetBuffer(7, m_uniforms[i]);
            DSet->SetImageView(8, m_transparencyRT->GetSRV(), m_transparencyRT->GetSampler());
            DSet->SetImageView(9, ibl_brdf_lut->GetSRV(), ibl_brdf_lut->GetSampler());
            DSet->SetBuffer(10, scene.GetLightStorage(i));
            DSet->Update();

            auto *DSetShadows = sets[1];
            Buffer *shadowUniform = useShadowResources ? shadows.GetUniform(i) : m_shadowFallbackUniforms[i];
            DSetShadows->SetBuffer(0, shadowUniform);
            DSetShadows->SetImageViews(1, shadowViews, {});
            DSetShadows->SetSampler(2, shadowSampler);
            DSetShadows->Update();

            auto *DSetSkybox = sets[2];
            DSetSkybox->SetImageView(0, skybox.GetCubeMap()->GetSRV(), skybox.GetCubeMap()->GetSampler());
            DSetSkybox->Update();
        }

        m_boundShadowsAvailable = useShadowResources;
    }

    void LightOpaquePass::Update()
    {
        const auto &gSettings = Settings::Get<GlobalSettings>();
        SceneRendererHost &renderer = RequireActiveSceneRendererHost();
        ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();
        const bool shadowsAvailable = gSettings.shadows && HasLiveShadowResources(shadows);
        if (Image *ssaoRT = renderer.GetRenderTarget("ssao");
            ssaoRT != m_ssaoRT || shadowsAvailable != m_boundShadowsAvailable)
        {
            m_ssaoRT = ssaoRT;
            UpdateDescriptorSets();
        }

        Camera *camera = GetActiveScene()->GetActiveCamera();

        m_ubo.invViewProj = camera->GetInvViewProjection();
        m_ubo.camPos = vec4(camera->GetPosition(), 1.0f);
        m_ubo.camForward = vec4(camera->GetFront(), 0.0f);
#if defined(PE_ANDROID)
        m_ubo.ssao = 0u; // FidelityFX CACAO produces incorrect AO on Mali GPUs; disable SSAO on Android.
#else
        m_ubo.ssao = gSettings.ssao && m_ssaoRT ? 1u : 0u;
#endif
        m_ubo.ssr = gSettings.ssr;
        m_ubo.IBL = gSettings.IBL;
        m_ubo.IBL_intensity = gSettings.IBL_intensity;
        m_ubo.lights_intensity = gSettings.lights_intensity;
        m_ubo.shadows = shadowsAvailable ? 1u : 0u;
        m_ubo.use_Disney_PBR = gSettings.use_Disney_PBR;
        m_ubo.orthographicCamera = camera->IsOrthographic() ? 1u : 0u;
        m_ubo.skyboxTanHalfFovY = tan(camera->Fovy() * 0.5f);
        m_ubo.physical_point_falloff = gSettings.physical_point_falloff ? 1.0f : 0.0f;

        BufferRange range{};
        range.data = &m_ubo;
        range.size = sizeof(m_ubo);
        range.offset = 0;
        m_uniforms[RHII.GetFrameIndex()]->Copy(1, &range, false);
    }

    void LightOpaquePass::DeclareInputs(RGBuilder &builder)
    {
        ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();
        const bool shadowsEnabled = Settings::Get<GlobalSettings>().shadows && HasLiveShadowResources(shadows);

        builder.Read(m_depthStencilRT);
        builder.Read(m_normalRT);
        builder.Read(m_albedoRT);
        builder.Read(m_srmRT);
        builder.Read(m_velocityRT);
        builder.Read(m_emissiveRT);
        if (Settings::Get<GlobalSettings>().ssao && m_ssaoRT)
            builder.Read(m_ssaoRT);
        builder.Read(m_transparencyRT);

        if (shadowsEnabled)
        {
            for (auto *tex : shadows.GetTextures())
                builder.Read(tex);
        }
    }

    void LightOpaquePass::ExecutePass(CommandBuffer *cmd)
    {
        uint32_t shadowmapCascades = Settings::Get<GlobalSettings>().num_cascades;
        const auto &gSettings = Settings::Get<GlobalSettings>();
        ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();
        const bool shadowsAvailable = gSettings.shadows && HasLiveShadowResources(shadows);

        Scene &scene = *GetActiveScene();
        cmd->SetConstantAt(0, (uint32_t)scene.GetPointLights().size()); // num point lights
        cmd->SetConstantAt(1, (uint32_t)scene.GetSpotLights().size());  // num spot lights
        cmd->SetConstantAt(2, (uint32_t)scene.GetAreaLights().size());  // num area lights
        cmd->SetConstantAt(3, 0u);                                      // padding
        cmd->SetConstantAt(4, m_viewportRT->GetWidth_f());              // framebuffer width
        cmd->SetConstantAt(5, m_viewportRT->GetHeight_f());             // framebuffer height
        cmd->SetConstantAt(6, 0u);                                      // is transparent pass
        for (uint32_t i = 0; i < shadowmapCascades; i++)
            cmd->SetConstantAt(i + 8,
                               shadowsAvailable ? shadows.GetCascadeViewZ(i) : 0.0f); // shadowmap cascade distances
        for (uint32_t i = 0; i < shadowmapCascades; i++)
            cmd->SetConstantAt(i + 12,
                               shadowsAvailable ? shadows.GetCascadeTexelSizeWorld(i) : 0.0f); // cascade world-space texel sizes
        cmd->SetConstantAt(16, gSettings.shadow_distance);
        cmd->SetConstantAt(17, gSettings.shadow_distance * std::clamp(gSettings.shadow_fade_fraction, 0.0f, 1.0f));
        cmd->SetConstantAt(18, gSettings.shadow_normal_bias);
        cmd->SetConstantAt(19, gSettings.shadow_filter_radius);
        cmd->SetConstantAt(20, static_cast<uint32_t>(std::clamp(gSettings.shadow_debug_mode, 0, 2)));

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
        m_uniforms.clear();
        DestroyLightShadowFallbackResources(m_shadowFallbackUniforms,
                                            m_shadowFallbackTexture,
                                            m_shadowFallbackSampler,
                                            m_shadowFallbackViews);
    }

    void LightTransparentPass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();

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
        EnsureLightShadowFallbackResources(m_shadowFallbackUniforms,
                                           m_shadowFallbackTexture,
                                           m_shadowFallbackSampler,
                                           m_shadowFallbackViews,
                                           "LightTransparent");
    }

    void LightTransparentPass::UpdatePassInfo()
    {
        const std::vector<Define> definesFrag{
            Define{"SHADOWMAP_CASCADES", std::to_string(Settings::Get<GlobalSettings>().num_cascades)},
            Define{"SHADOWMAP_SIZE", std::to_string((float)Settings::Get<GlobalSettings>().shadow_map_size)},
            Define{"SHADOWMAP_TEXEL_SIZE", std::to_string(1.0f / (float)Settings::Get<GlobalSettings>().shadow_map_size)}};

        m_passInfo->name = "lighting_transparent_pipeline";
        m_passInfo->pVertShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Common/Quad.hlsl", .entryPoint = "mainVS", .stage = PE_SHADER_STAGE_VERTEX, .defines = std::vector<Define>{}});
        m_passInfo->pFragShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Gbuffer/LightingPS.hlsl", .entryPoint = "mainPS", .stage = PE_SHADER_STAGE_FRAGMENT, .defines = definesFrag});
        m_passInfo->dynamicStates = {PE_DYNAMIC_STATE_VIEWPORT, PE_DYNAMIC_STATE_SCISSOR};
        m_passInfo->cullMode = PE_CULL_MODE_NONE;
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
        const bool useShadowResources = Settings::Get<GlobalSettings>().shadows && HasLiveShadowResources(shadows);
        EnsureLightShadowFallbackResources(m_shadowFallbackUniforms,
                                           m_shadowFallbackTexture,
                                           m_shadowFallbackSampler,
                                           m_shadowFallbackViews,
                                           "LightTransparent");
        const std::vector<ImageView *> shadowViews = useShadowResources ? GetLiveShadowViews(shadows) : m_shadowFallbackViews;
        Sampler *shadowSampler = useShadowResources ? shadows.GetSampler() : m_shadowFallbackSampler;

        SceneRendererHost &renderer = RequireActiveSceneRendererHost();
        const SkyBox &skybox = renderer.GetSkyBox();

        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
        {
            auto &sets = m_passInfo->GetDescriptors(i);

            auto *DSet = sets[0];
            Image *ssaoRT = m_ssaoRT ? m_ssaoRT : m_albedoRT;
            DSet->SetImageView(0, m_depthStencilRT->GetSRV(), m_depthStencilRT->GetSampler());
            DSet->SetImageView(1, m_normalRT->GetSRV(), m_normalRT->GetSampler());
            DSet->SetImageView(2, m_albedoRT->GetSRV(), m_albedoRT->GetSampler());
            DSet->SetImageView(3, m_srmRT->GetSRV(), m_srmRT->GetSampler());
            DSet->SetBuffer(4, GetActiveScene()->GetLightUniform(i));
            DSet->SetImageView(5, ssaoRT->GetSRV(), ssaoRT->GetSampler());
            DSet->SetImageView(6, m_emissiveRT->GetSRV(), m_emissiveRT->GetSampler());
            DSet->SetBuffer(7, m_uniforms[i]);
            DSet->SetImageView(8, m_transparencyRT->GetSRV(), m_transparencyRT->GetSampler());
            auto *ibl_brdf_lut = renderer.GetIBL_LUT();
            DSet->SetImageView(9, ibl_brdf_lut->GetSRV(), ibl_brdf_lut->GetSampler());
            DSet->SetBuffer(10, GetActiveScene()->GetLightStorage(i));
            DSet->Update();

            auto *DSetShadows = sets[1];
            Buffer *shadowUniform = useShadowResources ? shadows.GetUniform(i) : m_shadowFallbackUniforms[i];
            DSetShadows->SetBuffer(0, shadowUniform);
            DSetShadows->SetImageViews(1, shadowViews);
            DSetShadows->SetSampler(2, shadowSampler);
            DSetShadows->Update();

            auto *DSetSkybox = sets[2];
            DSetSkybox->SetImageView(0, skybox.GetCubeMap()->GetSRV(), skybox.GetCubeMap()->GetSampler());
            DSetSkybox->Update();
        }

        m_boundShadowsAvailable = useShadowResources;
    }

    void LightTransparentPass::Update()
    {
        const auto &gSettings = Settings::Get<GlobalSettings>();
        SceneRendererHost &renderer = RequireActiveSceneRendererHost();
        ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();
        const bool shadowsAvailable = gSettings.shadows && HasLiveShadowResources(shadows);
        if (Image *ssaoRT = renderer.GetRenderTarget("ssao");
            ssaoRT != m_ssaoRT || shadowsAvailable != m_boundShadowsAvailable)
        {
            m_ssaoRT = ssaoRT;
            UpdateDescriptorSets();
        }

        Camera *camera = GetActiveScene()->GetActiveCamera();

        m_ubo.invViewProj = camera->GetInvViewProjection();
        m_ubo.camPos = vec4(camera->GetPosition(), 1.0f);
        m_ubo.camForward = vec4(camera->GetFront(), 0.0f);
#if defined(PE_ANDROID)
        m_ubo.ssao = 0u; // FidelityFX CACAO produces incorrect AO on Mali GPUs; disable SSAO on Android.
#else
        m_ubo.ssao = gSettings.ssao && m_ssaoRT ? 1u : 0u;
#endif
        m_ubo.ssr = gSettings.ssr;
        m_ubo.IBL = gSettings.IBL;
        m_ubo.IBL_intensity = gSettings.IBL_intensity;
        m_ubo.lights_intensity = gSettings.lights_intensity;
        m_ubo.shadows = shadowsAvailable ? 1u : 0u;
        m_ubo.use_Disney_PBR = gSettings.use_Disney_PBR;
        m_ubo.orthographicCamera = camera->IsOrthographic() ? 1u : 0u;
        m_ubo.skyboxTanHalfFovY = tan(camera->Fovy() * 0.5f);
        m_ubo.physical_point_falloff = gSettings.physical_point_falloff ? 1.0f : 0.0f;

        BufferRange range{};
        range.data = &m_ubo;
        range.size = sizeof(m_ubo);
        range.offset = 0;
        m_uniforms[RHII.GetFrameIndex()]->Copy(1, &range, false);
    }

    void LightTransparentPass::DeclareInputs(RGBuilder &builder)
    {
        ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();
        const bool shadowsEnabled = Settings::Get<GlobalSettings>().shadows && HasLiveShadowResources(shadows);

        builder.Read(m_depthStencilRT);
        builder.Read(m_normalRT);
        builder.Read(m_albedoRT);
        builder.Read(m_srmRT);
        builder.Read(m_velocityRT);
        builder.Read(m_emissiveRT);
        if (Settings::Get<GlobalSettings>().ssao && m_ssaoRT)
            builder.Read(m_ssaoRT);
        builder.Read(m_transparencyRT);

        if (shadowsEnabled)
        {
            for (auto *tex : shadows.GetTextures())
                builder.Read(tex);
        }
    }

    void LightTransparentPass::ExecutePass(CommandBuffer *cmd)
    {
        uint32_t shadowmapCascades = Settings::Get<GlobalSettings>().num_cascades;
        const auto &gSettings = Settings::Get<GlobalSettings>();
        ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();
        const bool shadowsAvailable = gSettings.shadows && HasLiveShadowResources(shadows);

        Scene &scene = *GetActiveScene();
        cmd->SetConstantAt(0, (uint32_t)scene.GetPointLights().size()); // num point lights
        cmd->SetConstantAt(1, (uint32_t)scene.GetSpotLights().size());  // num spot lights
        cmd->SetConstantAt(2, (uint32_t)scene.GetAreaLights().size());  // num area lights
        cmd->SetConstantAt(3, 0u);                                      // padding
        cmd->SetConstantAt(4, m_viewportRT->GetWidth_f());              // framebuffer width
        cmd->SetConstantAt(5, m_viewportRT->GetHeight_f());             // framebuffer height
        cmd->SetConstantAt(6, 1u);                                      // transparent pass
        for (uint32_t i = 0; i < shadowmapCascades; i++)
            cmd->SetConstantAt(i + 8,
                               shadowsAvailable ? shadows.GetCascadeViewZ(i) : 0.0f); // shadowmap cascade distances
        for (uint32_t i = 0; i < shadowmapCascades; i++)
            cmd->SetConstantAt(i + 12,
                               shadowsAvailable ? shadows.GetCascadeTexelSizeWorld(i) : 0.0f); // cascade world-space texel sizes
        cmd->SetConstantAt(16, gSettings.shadow_distance);
        cmd->SetConstantAt(17, gSettings.shadow_distance * std::clamp(gSettings.shadow_fade_fraction, 0.0f, 1.0f));
        cmd->SetConstantAt(18, gSettings.shadow_normal_bias);
        cmd->SetConstantAt(19, gSettings.shadow_filter_radius);
        cmd->SetConstantAt(20, static_cast<uint32_t>(std::clamp(gSettings.shadow_debug_mode, 0, 2)));

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
        m_uniforms.clear();
        DestroyLightShadowFallbackResources(m_shadowFallbackUniforms,
                                            m_shadowFallbackTexture,
                                            m_shadowFallbackSampler,
                                            m_shadowFallbackViews);
    }
} // namespace pe
