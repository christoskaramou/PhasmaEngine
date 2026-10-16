#include "LightPass.h"
#include "GlobalIlluminationPass.h"
#include "ReflectionPass.h"
#include "ScreenProbePass.h"
#include "API/Buffer.h"
#include "Render/SceneRenderTargets.h"
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
#include "ForwardPlusLightCullingPass.h"
#include "Render/SceneRendererHost.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Scene/OpenPBRMaterial.h"
#include "ShadowPass.h"
#include "Skybox/Skybox.h"

namespace pe
{
    namespace
    {
        bool ProbeLightingActive(uint32_t frame)
        {
            auto *gi = GetGlobalComponent<GlobalIlluminationPass>();
            auto *scene = GetActiveScene();
            return Settings::Get<SceneSettings>().global_illumination && gi && scene && scene->GetTLAS() && gi->GetProbeUniform(frame);
        }

        // The screen probe irradiance the opaque lighting pass reads this frame (ScreenProbePass runs whenever GI
        // does in raster), or null.
        Image *ActiveScreenIrradiance(uint32_t frame)
        {
            auto *screenProbes = GetGlobalComponent<ScreenProbePass>();
            return ProbeLightingActive(frame) && screenProbes ? screenProbes->GetIrradiance() : nullptr;
        }

        // The reflections the opaque lighting pass reads this frame (ReflectionPass runs with the screen probes), or null.
        Image *ActiveReflections(uint32_t frame)
        {
            auto *reflections = GetGlobalComponent<ReflectionPass>();
            return ActiveScreenIrradiance(frame) && reflections ? reflections->GetSpecular() : nullptr;
        }

        void BindProbeLighting(PassInfo &pass, Buffer *fallbackUniform, uint32_t frame, bool force = false, Image *screen = nullptr,
                               Image *reflections = nullptr)
        {
            const auto &sets = pass.GetDescriptors(frame);
            if (sets.size() < 4)
                return;
            auto *gi = GetGlobalComponent<GlobalIlluminationPass>();
            const bool active = ProbeLightingActive(frame);
            auto *fallback = RequireActiveSceneRendererHost().GetIBL_LUT();
            auto *set = sets[3];
            auto *uniform = active ? gi->GetProbeUniform(frame) : fallbackUniform;
            ImageView *screenView = screen ? screen->GetSRV() : fallback->GetSRV();
            ImageView *reflectionView = reflections ? reflections->GetSRV() : fallback->GetSRV();
            const auto &bound = set->GetBoundResources();
            auto isBound = [&bound](uint32_t binding, auto matches)
            { return std::any_of(bound.begin(), bound.end(), [&](const auto &info)
                                 { return info.binding == binding && matches(info); }); };
            if (!force && isBound(0, [uniform](const auto &info)
                                  { return !info.buffers.empty() && info.buffers[0] == uniform; }) &&
                (!set->HasBinding(4) || isBound(4, [screenView](const auto &info)
                                                { return !info.views.empty() && info.views[0] == screenView; })) &&
                (!set->HasBinding(5) || isBound(5, [reflectionView](const auto &info)
                                                { return !info.views.empty() && info.views[0] == reflectionView; })))
                return;
            set->SetBuffer(0, uniform,
                           active ? 0 : RHII.AlignUniform(sizeof(LightPassUBO)), active ? 0 : GlobalIlluminationPass::kUniformBytes);
            set->SetImageView(1, active ? gi->GetIrradianceHistory()->GetSRV() : fallback->GetSRV());
            set->SetImageView(2, active ? gi->GetDistanceHistory()->GetSRV() : fallback->GetSRV());
            set->SetSampler(3, fallback->GetSampler());
            if (set->HasBinding(4))
                set->SetImageView(4, screenView);
            if (set->HasBinding(5))
                set->SetImageView(5, reflectionView);
            set->Update();
        }

        void ReadProbeLighting(RGBuilder &builder)
        {
            auto *gi = GetGlobalComponent<GlobalIlluminationPass>();
            auto *scene = GetActiveScene();
            if (Settings::Get<SceneSettings>().global_illumination && gi && scene && scene->GetTLAS() && gi->GetIrradianceHistory())
            {
                builder.Read(gi->GetIrradianceHistory());
                builder.Read(gi->GetDistanceHistory());
            }
        }

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
            const uint32_t cascadeCount = std::max(1u, Settings::Get<SceneSettings>().num_cascades);
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

        Buffer *CreateZeroedStorageBuffer(size_t size, const std::string &name)
        {
            Buffer *buffer = Buffer::Create({
                .size = size,
                .usage = PE_BUFFER_USAGE_STORAGE_BUFFER,
                .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU,
                .name = name,
            });
            buffer->Map();
            buffer->Zero();
            buffer->Flush();
            buffer->Unmap();
            return buffer;
        }

        void DestroyForwardPlusFallbackResources(std::vector<Buffer *> &tileData,
                                                 std::vector<Buffer *> &lightIndices)
        {
            for (auto *&buffer : tileData)
                Buffer::Destroy(buffer);
            tileData.clear();

            for (auto *&buffer : lightIndices)
                Buffer::Destroy(buffer);
            lightIndices.clear();
        }

        void EnsureForwardPlusFallbackResources(std::vector<Buffer *> &tileData,
                                                std::vector<Buffer *> &lightIndices,
                                                const std::string &namePrefix)
        {
            const uint32_t frameCount = RHII.GetSwapchainImageCount();
            if (tileData.size() == frameCount && lightIndices.size() == frameCount &&
                std::all_of(tileData.begin(), tileData.end(), [](Buffer *buffer)
                            { return buffer != nullptr; }) &&
                std::all_of(lightIndices.begin(), lightIndices.end(), [](Buffer *buffer)
                            { return buffer != nullptr; }))
                return;

            DestroyForwardPlusFallbackResources(tileData, lightIndices);
            tileData.resize(frameCount);
            lightIndices.resize(frameCount);
            for (uint32_t i = 0; i < frameCount; ++i)
            {
                tileData[i] = CreateZeroedStorageBuffer(sizeof(uvec4),
                                                        namePrefix + "_ForwardPlusFallbackTileData_" +
                                                            std::to_string(i));
                lightIndices[i] = CreateZeroedStorageBuffer(sizeof(uint32_t),
                                                            namePrefix + "_ForwardPlusFallbackLightIndices_" +
                                                                std::to_string(i));
            }
        }

        // The Shadow pass's own condition: with the scene settings inactive it renders no shadow maps.
        bool ShadowsActive(const ShadowPass &shadows)
        {
            return Settings::Get<SceneSettings>().shadows && SceneSettingsActive() && shadows.HasLiveResources();
        }

        std::vector<ImageView *> GetLiveShadowViews(const ShadowPass &shadows)
        {
            return shadows.GetTextureViews();
        }

        bool SetForwardPlusDescriptors(Descriptor *set,
                                       uint32_t frame,
                                       const std::vector<Buffer *> &fallbackTileData,
                                       const std::vector<Buffer *> &fallbackLightIndices)
        {
            Buffer *tileData = frame < fallbackTileData.size() ? fallbackTileData[frame] : nullptr;
            Buffer *pointIndices = frame < fallbackLightIndices.size() ? fallbackLightIndices[frame] : nullptr;
            Buffer *spotIndices = pointIndices;
            bool boundLiveForwardPlusResources = false;

            if (Settings::Get<SceneSettings>().forward_plus)
            {
                if (ForwardPlusLightCullingPass *forwardPlus = GetGlobalComponent<ForwardPlusLightCullingPass>())
                {
                    Buffer *liveTileData = forwardPlus->GetTileLightData(frame);
                    Buffer *livePointIndices = forwardPlus->GetPointLightIndices(frame);
                    Buffer *liveSpotIndices = forwardPlus->GetSpotLightIndices(frame);
                    if (liveTileData && livePointIndices && liveSpotIndices)
                    {
                        tileData = liveTileData;
                        pointIndices = livePointIndices;
                        spotIndices = liveSpotIndices;
                        boundLiveForwardPlusResources = true;
                    }
                }
            }

            if (!set->HasBinding(11) || !set->HasBinding(12) || !set->HasBinding(13))
                return boundLiveForwardPlusResources;

            set->SetBuffer(11, tileData);
            set->SetBuffer(12, pointIndices);
            set->SetBuffer(13, spotIndices);
            return boundLiveForwardPlusResources;
        }

        void ReadForwardPlusTiles(RGBuilder &builder)
        {
            if (!Settings::Get<SceneSettings>().forward_plus)
                return;
            if (ForwardPlusLightCullingPass *forwardPlus = GetGlobalComponent<ForwardPlusLightCullingPass>())
                builder.ReadBuffer(forwardPlus->GetTileLightData(RHII.GetFrameIndex()));
        }

        void AddForwardPlusReadBarriers(CommandBuffer *cmd, uint32_t frame)
        {
            if (!Settings::Get<SceneSettings>().forward_plus)
                return;

            ForwardPlusLightCullingPass *forwardPlus = GetGlobalComponent<ForwardPlusLightCullingPass>();
            if (!forwardPlus)
                return;

            std::vector<BufferBarrierInfo> barriers;
            barriers.reserve(3);
            auto addBarrier = [&](Buffer *buffer)
            {
                if (!buffer)
                    return;

                BufferBarrierInfo barrier{};
                barrier.buffer = buffer;
                barrier.stageMask = PE_STAGE_FRAGMENT_SHADER;
                barrier.accessMask = PE_ACCESS_SHADER_STORAGE_READ;
                barrier.size = PE_WHOLE_SIZE;
                barriers.push_back(barrier);
            };

            addBarrier(forwardPlus->GetTileLightData(frame));
            addBarrier(forwardPlus->GetPointLightIndices(frame));
            addBarrier(forwardPlus->GetSpotLightIndices(frame));

            if (!barriers.empty())
                cmd->BufferBarriers(barriers);
        }

        // LightingPS's compile-time configuration, shared by the opaque lighting pass and material pass 2.
        std::vector<Define> LightingDefines()
        {
            return {Define{"SHADOWMAP_CASCADES", std::to_string(Settings::Get<SceneSettings>().num_cascades)},
                    Define{"SHADOWMAP_SIZE", std::to_string((float)Settings::Get<SceneSettings>().shadow_map_size)},
                    Define{"SHADOWMAP_TEXEL_SIZE", std::to_string(1.0f / (float)Settings::Get<SceneSettings>().shadow_map_size)},
                    Define{"FORWARD_PLUS_TILE_SIZE", std::to_string(ForwardPlusLightCullingPass::TileSize)},
                    Define{"FORWARD_PLUS_MAX_LIGHTS_PER_TILE", std::to_string(ForwardPlusLightCullingPass::MaxLightsPerTile)}};
        }

        // PushConstants_Lighting by dword index (Structures.hlsl), shared by the opaque lighting pass and pass 2.
        void SetLightingPushConstants(CommandBuffer *cmd, Image *viewport, uint32_t passType)
        {
            const auto &gSettings = Settings::Get<SceneSettings>();
            const uint32_t shadowmapCascades = gSettings.num_cascades;
            ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();
            const bool shadowsAvailable = ShadowsActive(shadows);
            Scene &scene = *GetActiveScene();

            cmd->SetConstantAt(0, (uint32_t)scene.GetPointLights().size()); // num point lights
            cmd->SetConstantAt(1, (uint32_t)scene.GetSpotLights().size());  // num spot lights
            cmd->SetConstantAt(2, (uint32_t)scene.GetAreaLights().size());  // num area lights
            cmd->SetConstantAt(3, 0u);                                      // padding
            cmd->SetConstantAt(4, viewport->GetWidth_f());                  // framebuffer width
            cmd->SetConstantAt(5, viewport->GetHeight_f());                 // framebuffer height
            cmd->SetConstantAt(6, passType);                                // is transparent pass

            for (uint32_t i = 0; i < shadowmapCascades; i++)
            {
                // shadowmap cascade distances
                cmd->SetConstantAt(i + 8, shadowsAvailable ? shadows.GetCascadeViewZ(i) : 0.0f);
            }

            for (uint32_t i = 0; i < shadowmapCascades; i++)
            {
                // cascade world-space texel sizes
                cmd->SetConstantAt(i + 12, shadowsAvailable ? shadows.GetCascadeTexelSizeWorld(i) : 0.0f);
            }

            cmd->SetConstantAt(16, gSettings.shadow_distance);
            cmd->SetConstantAt(17, gSettings.shadow_distance * std::clamp(gSettings.shadow_fade_fraction, 0.0f, 1.0f));
            cmd->SetConstantAt(18, gSettings.shadow_normal_bias);
            cmd->SetConstantAt(19, gSettings.shadow_filter_radius);
            cmd->SetConstantAt(20, static_cast<uint32_t>(std::clamp(gSettings.shadow_debug_mode, 0, 2)));
        }

        std::string DefinesKey(const std::vector<Define> &defines)
        {
            std::string key;
            for (const Define &define : defines)
                key += define.name + "=" + define.value + ";";

            return key;
        }
    } // namespace

    void LightOpaquePass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();

        m_srmRT = rs->GetRenderTarget("srm"); // Specular Roughness Metallic
        m_normalRT = rs->GetRenderTarget("normal");
        m_albedoRT = rs->GetRenderTarget("albedo");
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
        EnsureForwardPlusFallbackResources(m_forwardPlusFallbackTileData,
                                           m_forwardPlusFallbackLightIndices,
                                           "LightOpaque");
    }

    void LightOpaquePass::UpdatePassInfo()
    {
        const std::vector<Define> definesFrag = LightingDefines();

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
                .size = RHII.AlignUniform(sizeof(LightPassUBO)) + RHII.AlignUniform(GlobalIlluminationPass::kUniformBytes),
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
        const bool useShadowResources = ShadowsActive(shadows);
        EnsureLightShadowFallbackResources(m_shadowFallbackUniforms,
                                           m_shadowFallbackTexture,
                                           m_shadowFallbackSampler,
                                           m_shadowFallbackViews,
                                           "LightOpaque");
        EnsureForwardPlusFallbackResources(m_forwardPlusFallbackTileData,
                                           m_forwardPlusFallbackLightIndices,
                                           "LightOpaque");
        const std::vector<ImageView *> shadowViews = useShadowResources ? GetLiveShadowViews(shadows) : m_shadowFallbackViews;
        Sampler *shadowSampler = useShadowResources ? shadows.GetSampler() : m_shadowFallbackSampler;

        SceneRendererHost &renderer = RequireActiveSceneRendererHost();
        const SkyBox &skybox = renderer.GetSkyBox();

        Scene &scene = *GetActiveScene();
        const bool forwardPlusEnabled = Settings::Get<SceneSettings>().forward_plus;
        bool boundForwardPlusResources = true;
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
            m_boundLightStorage.resize(RHII.GetSwapchainImageCount());
            m_boundLightStorage[i] = scene.GetLightStorage(i);
            const bool frameBoundForwardPlusResources =
                SetForwardPlusDescriptors(DSet, i, m_forwardPlusFallbackTileData, m_forwardPlusFallbackLightIndices);
            boundForwardPlusResources = boundForwardPlusResources && frameBoundForwardPlusResources;
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
            BindProbeLighting(*m_passInfo, m_uniforms[i], i, true, ActiveScreenIrradiance(i), ActiveReflections(i));
        }

        m_boundShadowsAvailable = useShadowResources;
        m_boundForwardPlusEnabled = forwardPlusEnabled && boundForwardPlusResources;
    }

    void LightOpaquePass::Update()
    {
        const auto &gSettings = Settings::Get<SceneSettings>();
        const auto &pp = ActivePostProcessProfile();
        SceneRendererHost &renderer = RequireActiveSceneRendererHost();
        ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();
        const bool shadowsAvailable = ShadowsActive(shadows);
        const bool forwardPlusEnabled = gSettings.forward_plus;
        const uint32_t frame = RHII.GetFrameIndex();
        const bool lightStorageMoved =
            frame >= m_boundLightStorage.size() || m_boundLightStorage[frame] != GetActiveScene()->GetLightStorage(frame);
        if (Image *ssaoRT = renderer.GetRenderTarget("ssao");
            ssaoRT != m_ssaoRT || shadowsAvailable != m_boundShadowsAvailable ||
            forwardPlusEnabled != m_boundForwardPlusEnabled || lightStorageMoved)
        {
            m_ssaoRT = ssaoRT;
            UpdateDescriptorSets();
        }

        Camera *camera = GetActiveScene()->GetActiveCamera();

        m_ubo.invViewProj = camera->GetInvViewProjection();
        m_ubo.camPos = vec4(camera->GetPosition(), 1.0f);
        m_ubo.camForward = vec4(camera->GetFront(), 0.0f);
#if defined(PE_ANDROID)
        m_ubo.ssao = 0u; // SSAO stays disabled on Android until the native pass is validated there.
#else
        m_ubo.ssao = pp.ssao && m_ssaoRT ? 1u : 0u;
#endif
        m_ubo.ssr = pp.ssr;
        m_ubo.IBL = pp.IBL;
        // Volume blend: scale IBL by the per-effect factor (0 -> no IBL contribution) so it fades.
        m_ubo.IBL_intensity = pp.IBL_intensity * std::clamp(ActivePostProcessBlend().IBL, 0.0f, 1.0f);
        m_ubo.shadows = shadowsAvailable ? 1u : 0u;
        m_ubo.use_Disney_PBR = gSettings.use_Disney_PBR;
        m_ubo.orthographicCamera = camera->IsOrthographic() ? 1u : 0u;
        m_ubo.skyboxTanHalfFovY = tan(camera->Fovy() * 0.5f);
        m_ubo.physical_point_falloff = gSettings.physical_point_falloff ? 1.0f : 0.0f;
        m_ubo.forward_plus = forwardPlusEnabled ? 1u : 0u;
        m_ubo.fog_density = gSettings.fog ? gSettings.fog_density : 0.0f;
        m_ubo.fog_start = gSettings.fog_start;
        m_ubo.linear_color = SceneUsesLinearColor() ? 1u : 0u;
        Image *screenIrradiance = ActiveScreenIrradiance(RHII.GetFrameIndex());
        m_ubo.screen_probes = screenIrradiance ? 1u : 0u;
        Image *reflections = ActiveReflections(RHII.GetFrameIndex());
        m_ubo.reflections = reflections ? 1u : 0u;

        BufferRange range{};
        range.data = &m_ubo;
        range.size = sizeof(m_ubo);
        range.offset = 0;
        m_uniforms[RHII.GetFrameIndex()]->Copy(1, &range, false);
        BindProbeLighting(*m_passInfo, m_uniforms[RHII.GetFrameIndex()], RHII.GetFrameIndex(), false, screenIrradiance, reflections);
    }

    void LightOpaquePass::DeclareInputs(RGBuilder &builder)
    {
        ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();
        const bool shadowsEnabled = ShadowsActive(shadows);

        builder.Read(m_depthStencilRT);
        builder.Read(m_normalRT);
        builder.Read(m_albedoRT);
        builder.Read(m_srmRT);
        builder.Read(m_emissiveRT);
        if (ActivePostProcessProfile().ssao && m_ssaoRT)
            builder.Read(m_ssaoRT);
        builder.Read(m_transparencyRT);
        ReadProbeLighting(builder);
        if (Image *screenIrradiance = ActiveScreenIrradiance(RHII.GetFrameIndex()))
            builder.Read(screenIrradiance);
        if (Image *reflections = ActiveReflections(RHII.GetFrameIndex()))
            builder.Read(reflections);
        ReadForwardPlusTiles(builder);

        if (shadowsEnabled)
        {
            for (auto *tex : shadows.GetTextures())
                builder.Read(tex);
        }
    }

    void LightOpaquePass::ExecutePass(CommandBuffer *cmd)
    {
        const uint32_t frame = RHII.GetFrameIndex();
        AddForwardPlusReadBarriers(cmd, frame);
        SetLightingPushConstants(cmd, m_viewportRT, 0u);

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
        DestroyForwardPlusFallbackResources(m_forwardPlusFallbackTileData, m_forwardPlusFallbackLightIndices);
    }

    void LightTransparentPass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();

        m_srmRT = rs->GetRenderTarget("srm"); // Specular Roughness Metallic
        m_normalRT = rs->GetRenderTarget("normal");
        m_albedoRT = rs->GetRenderTarget("albedo");
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
        EnsureForwardPlusFallbackResources(m_forwardPlusFallbackTileData,
                                           m_forwardPlusFallbackLightIndices,
                                           "LightTransparent");
    }

    void LightTransparentPass::UpdatePassInfo()
    {
        const std::vector<Define> definesFrag{
            Define{"SHADOWMAP_CASCADES", std::to_string(Settings::Get<SceneSettings>().num_cascades)},
            Define{"SHADOWMAP_SIZE", std::to_string((float)Settings::Get<SceneSettings>().shadow_map_size)},
            Define{"SHADOWMAP_TEXEL_SIZE", std::to_string(1.0f / (float)Settings::Get<SceneSettings>().shadow_map_size)},
            Define{"FORWARD_PLUS_TILE_SIZE", std::to_string(ForwardPlusLightCullingPass::TileSize)},
            Define{"FORWARD_PLUS_MAX_LIGHTS_PER_TILE", std::to_string(ForwardPlusLightCullingPass::MaxLightsPerTile)}};

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
                .size = RHII.AlignUniform(sizeof(LightPassUBO)) + RHII.AlignUniform(GlobalIlluminationPass::kUniformBytes),
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
        const bool useShadowResources = ShadowsActive(shadows);
        EnsureLightShadowFallbackResources(m_shadowFallbackUniforms,
                                           m_shadowFallbackTexture,
                                           m_shadowFallbackSampler,
                                           m_shadowFallbackViews,
                                           "LightTransparent");
        EnsureForwardPlusFallbackResources(m_forwardPlusFallbackTileData,
                                           m_forwardPlusFallbackLightIndices,
                                           "LightTransparent");
        const std::vector<ImageView *> shadowViews = useShadowResources ? GetLiveShadowViews(shadows) : m_shadowFallbackViews;
        Sampler *shadowSampler = useShadowResources ? shadows.GetSampler() : m_shadowFallbackSampler;

        SceneRendererHost &renderer = RequireActiveSceneRendererHost();
        const SkyBox &skybox = renderer.GetSkyBox();

        const bool forwardPlusEnabled = Settings::Get<SceneSettings>().forward_plus;
        bool boundForwardPlusResources = true;
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
            m_boundLightStorage.resize(RHII.GetSwapchainImageCount());
            m_boundLightStorage[i] = GetActiveScene()->GetLightStorage(i);
            const bool frameBoundForwardPlusResources =
                SetForwardPlusDescriptors(DSet, i, m_forwardPlusFallbackTileData, m_forwardPlusFallbackLightIndices);
            boundForwardPlusResources = boundForwardPlusResources && frameBoundForwardPlusResources;
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
            BindProbeLighting(*m_passInfo, m_uniforms[i], i, true);
        }

        m_boundShadowsAvailable = useShadowResources;
        m_boundForwardPlusEnabled = forwardPlusEnabled && boundForwardPlusResources;
    }

    void LightTransparentPass::Update()
    {
        const auto &gSettings = Settings::Get<SceneSettings>();
        const auto &pp = ActivePostProcessProfile();
        SceneRendererHost &renderer = RequireActiveSceneRendererHost();
        ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();
        const bool shadowsAvailable = ShadowsActive(shadows);
        const bool forwardPlusEnabled = gSettings.forward_plus;
        const uint32_t frame = RHII.GetFrameIndex();
        const bool lightStorageMoved =
            frame >= m_boundLightStorage.size() || m_boundLightStorage[frame] != GetActiveScene()->GetLightStorage(frame);
        if (Image *ssaoRT = renderer.GetRenderTarget("ssao");
            ssaoRT != m_ssaoRT || shadowsAvailable != m_boundShadowsAvailable ||
            forwardPlusEnabled != m_boundForwardPlusEnabled || lightStorageMoved)
        {
            m_ssaoRT = ssaoRT;
            UpdateDescriptorSets();
        }

        Camera *camera = GetActiveScene()->GetActiveCamera();

        m_ubo.invViewProj = camera->GetInvViewProjection();
        m_ubo.camPos = vec4(camera->GetPosition(), 1.0f);
        m_ubo.camForward = vec4(camera->GetFront(), 0.0f);
#if defined(PE_ANDROID)
        m_ubo.ssao = 0u; // SSAO stays disabled on Android until the native pass is validated there.
#else
        m_ubo.ssao = pp.ssao && m_ssaoRT ? 1u : 0u;
#endif
        m_ubo.ssr = pp.ssr;
        m_ubo.IBL = pp.IBL;
        // Volume blend: scale IBL by the per-effect factor (0 -> no IBL contribution) so it fades.
        m_ubo.IBL_intensity = pp.IBL_intensity * std::clamp(ActivePostProcessBlend().IBL, 0.0f, 1.0f);
        m_ubo.shadows = shadowsAvailable ? 1u : 0u;
        m_ubo.use_Disney_PBR = gSettings.use_Disney_PBR;
        m_ubo.orthographicCamera = camera->IsOrthographic() ? 1u : 0u;
        m_ubo.skyboxTanHalfFovY = tan(camera->Fovy() * 0.5f);
        m_ubo.physical_point_falloff = gSettings.physical_point_falloff ? 1.0f : 0.0f;
        m_ubo.forward_plus = forwardPlusEnabled ? 1u : 0u;
        m_ubo.fog_density = gSettings.fog ? gSettings.fog_density : 0.0f;
        m_ubo.fog_start = gSettings.fog_start;
        m_ubo.linear_color = SceneUsesLinearColor() ? 1u : 0u;

        BufferRange range{};
        range.data = &m_ubo;
        range.size = sizeof(m_ubo);
        range.offset = 0;
        m_uniforms[RHII.GetFrameIndex()]->Copy(1, &range, false);
        BindProbeLighting(*m_passInfo, m_uniforms[RHII.GetFrameIndex()], RHII.GetFrameIndex());
    }

    void LightTransparentPass::DeclareInputs(RGBuilder &builder)
    {
        ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();
        const bool shadowsEnabled = ShadowsActive(shadows);

        builder.Read(m_depthStencilRT);
        builder.Read(m_normalRT);
        builder.Read(m_albedoRT);
        builder.Read(m_srmRT);
        builder.Read(m_emissiveRT);
        if (ActivePostProcessProfile().ssao && m_ssaoRT)
            builder.Read(m_ssaoRT);
        builder.Read(m_transparencyRT);
        ReadProbeLighting(builder);
        ReadForwardPlusTiles(builder);

        if (shadowsEnabled)
        {
            for (auto *tex : shadows.GetTextures())
                builder.Read(tex);
        }
    }

    void LightTransparentPass::ExecutePass(CommandBuffer *cmd)
    {
        uint32_t shadowmapCascades = Settings::Get<SceneSettings>().num_cascades;
        const auto &gSettings = Settings::Get<SceneSettings>();
        ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();
        const bool shadowsAvailable = ShadowsActive(shadows);

        Scene &scene = *GetActiveScene();
        const uint32_t frame = RHII.GetFrameIndex();
        AddForwardPlusReadBarriers(cmd, frame);

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
        DestroyForwardPlusFallbackResources(m_forwardPlusFallbackTileData, m_forwardPlusFallbackLightIndices);
    }

    namespace
    {
        void BindMaterialImage(Descriptor *set, uint32_t binding, Image *image)
        {
            if (image && set->HasBinding(binding))
                set->SetImageView(binding, image->GetSRV(), image->GetSampler());
        }

        void BindMaterialBuffer(Descriptor *set, uint32_t binding, Buffer *buffer)
        {
            if (buffer && set->HasBinding(binding))
                set->SetBuffer(binding, buffer);
        }
    } // namespace

    void MaterialShadePass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();
        m_viewportRT = rs->GetRenderTarget("viewport");
        m_visibility = rs->GetRenderTarget("visibility");
        m_albedoRT = rs->GetRenderTarget("albedo");
        m_boundFrames.assign(RHII.GetSwapchainImageCount(), false);
    }

    std::vector<Define> MaterialShadePass::Defines() const
    {
        std::vector<Define> defines = LightingDefines();
        defines.push_back(Define{"PE_OPENPBR_SHADE", "1"});
        if (m_viewportRT && m_viewportRT->GetFormat() == PE_FORMAT_R8G8B8A8_UNORM)
            defines.push_back(Define{"PE_VIEWPORT_RGBA8", "1"});

        return defines;
    }

    void MaterialShadePass::BuildPipeline(PassInfo &info, uint32_t module)
    {
        const std::vector<Define> defines = Defines();
        info.name = "MaterialShade_pipeline_" + std::to_string(module);
        info.pCompShader = Shader::Create({.sourcePath = GetOpenPBRModule(module).shadeSource,
                                           .entryPoint = "mainCS",
                                           .stage = PE_SHADER_STAGE_COMPUTE,
                                           .defines = defines});
        info.Update();
    }

    void MaterialShadePass::UpdatePassInfo()
    {
        m_builtDefines = DefinesKey(Defines());
        for (const auto &[module, info] : m_modulePassInfos)
            BuildPipeline(*info, module);
    }

    void MaterialShadePass::SyncModules(const std::vector<uint32_t> &modules)
    {
        for (auto it = m_modulePassInfos.begin(); it != m_modulePassInfos.end();)
        {
            if (std::find(modules.begin(), modules.end(), it->first) != modules.end())
            {
                ++it;
                continue;
            }

            RHII.AddToDeletionQueue([retired = it->second] {});
            it = m_modulePassInfos.erase(it);
        }

        for (uint32_t module : modules)
        {
            if (m_modulePassInfos.contains(module))
                continue;

            auto info = std::make_shared<PassInfo>();
            BuildPipeline(*info, module);
            m_modulePassInfos.emplace(module, std::move(info));
        }
    }

    std::vector<PassInfo *> MaterialShadePass::GetPassInfos() noexcept
    {
        std::vector<PassInfo *> infos;
        for (const auto &[module, info] : m_modulePassInfos)
            infos.push_back(info.get());

        return infos;
    }

    void MaterialShadePass::BindSceneDescriptors(PassInfo &info, uint32_t frame)
    {
        SceneRendererHost &renderer = RequireActiveSceneRendererHost();
        Scene *scene = GetActiveScene();
        auto *opaque = GetGlobalComponent<LightOpaquePass>();
        Image *ssao = renderer.GetRenderTarget("ssao");
        Descriptor *set = info.GetDescriptors(frame)[0];

        BindMaterialBuffer(set, 4, scene->GetLightUniform(frame));
        BindMaterialImage(set, 5, ssao ? ssao : m_albedoRT);
        BindMaterialBuffer(set, 7, opaque->m_uniforms[frame]); // PassUBO: the opaque pass's per-frame LightPassUBO
        BindMaterialImage(set, 9, renderer.GetIBL_LUT());
        BindMaterialBuffer(set, 10, scene->GetLightStorage(frame));

        if (set->HasBinding(14))
            set->SetImageView(14, m_visibility->GetSRV());

        BindMaterialBuffer(set, 15, scene->GetBuffer());
        BindMaterialBuffer(set, 16, scene->GetUniforms(frame));
        BindMaterialBuffer(set, 17, scene->GetMeshConstants());
        BindMaterialBuffer(set, 18, scene->GetMaterialByteBuffer());
        if (set->HasBinding(19))
            set->SetImageView(19, m_viewportRT->GetUAV(0));

        set->Update();
    }

    void MaterialShadePass::BindEnvironmentDescriptors(PassInfo &info, uint32_t frame)
    {
        SceneRendererHost &renderer = RequireActiveSceneRendererHost();
        ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();
        const bool useShadows = ShadowsActive(shadows);
        const auto &sets = info.GetDescriptors(frame);
        if (sets.size() > 1)
        {
            Descriptor *set = sets[1];
            BindMaterialBuffer(set, 0, useShadows ? shadows.GetUniform(frame) : m_shadowFallbackUniforms[frame]);
            if (set->HasBinding(1))
                set->SetImageViews(1, useShadows ? GetLiveShadowViews(shadows) : m_shadowFallbackViews, {});

            if (set->HasBinding(2))
                set->SetSampler(2, useShadows ? shadows.GetSampler() : m_shadowFallbackSampler);

            set->Update();
        }

        if (sets.size() > 2)
        {
            Descriptor *set = sets[2];
            Image *radiance = renderer.GetSkyBox().GetOpenPBRRadiance();
            BindMaterialImage(set, 0, renderer.GetSkyBox().GetCubeMap());
            if (set->HasBinding(2))
                set->SetSamplers(2, GetActiveScene()->GetOpenPBRSamplers());

            if (set->HasBinding(3))
                set->SetImageView(3, radiance->GetSRV());

            if (set->HasBinding(4))
                set->SetImageView(4, renderer.GetSkyBox().GetOpenPBRIrradiance()->GetSRV());

            if (set->HasBinding(1))
                set->SetSampler(1, radiance->GetSampler());

            if (set->HasBinding(6))
                set->SetImageViews(6, GetActiveScene()->GetImageViews(), {});

            set->Update();
        }

        auto *opaque = GetGlobalComponent<LightOpaquePass>();
        BindProbeLighting(info,
                          opaque->m_uniforms[frame],
                          frame,
                          true,
                          ActiveScreenIrradiance(frame),
                          ActiveReflections(frame));
    }

    void MaterialShadePass::BindFrame(uint32_t frame)
    {
        if (frame >= m_boundFrames.size())
            return;

        m_boundFrames[frame] = false;
        EnsureLightShadowFallbackResources(m_shadowFallbackUniforms,
                                           m_shadowFallbackTexture,
                                           m_shadowFallbackSampler,
                                           m_shadowFallbackViews,
                                           "MaterialShade");

        auto *opaque = GetGlobalComponent<LightOpaquePass>();
        Scene *scene = GetActiveScene();
        const bool frameReady = opaque &&
                                frame < opaque->m_uniforms.size() &&
                                frame < m_shadowFallbackUniforms.size();
        const bool uniformsReady = frameReady && opaque->m_uniforms[frame];
        const bool sceneReady = scene && scene->GetBuffer() && scene->GetMaterialByteBuffer();
        const bool targetsReady = m_visibility && m_viewportRT;
        if (!uniformsReady || !sceneReady || !targetsReady)
            return;

        const auto &sky = RequireActiveSceneRendererHost().GetSkyBox();
        if (!sky.GetOpenPBRRadiance() || !sky.GetOpenPBRIrradiance())
            return;

        for (const auto &[module, info] : m_modulePassInfos)
        {
            BindSceneDescriptors(*info, frame);
            BindEnvironmentDescriptors(*info, frame);
        }

        m_boundFrames[frame] = !m_modulePassInfos.empty();
    }

    void MaterialShadePass::UpdateDescriptorSets()
    {
        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); ++i)
            BindFrame(i);
    }

    void MaterialShadePass::Update()
    {
        // Every frame, as the light storage, shadow and GI resources it shares with LightOpaque can be replaced.
        BindFrame(RHII.GetFrameIndex());
    }

    void MaterialShadePass::DeclareInputs(RGBuilder &builder)
    {
        if (!m_visibility || !m_viewportRT)
            return;

        builder.ReadCompute(m_visibility);

        Image *ssao = RequireActiveSceneRendererHost().GetRenderTarget("ssao");
        if (ssao && ActivePostProcessProfile().ssao)
            builder.ReadCompute(ssao);

        ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();
        if (ShadowsActive(shadows))
        {
            for (auto *tex : shadows.GetTextures())
                builder.ReadCompute(tex);
        }

        auto *gi = GetGlobalComponent<GlobalIlluminationPass>();
        Scene *scene = GetActiveScene();
        if (Settings::Get<SceneSettings>().global_illumination &&
            gi &&
            scene &&
            scene->GetTLAS() &&
            gi->GetIrradianceHistory())
        {
            builder.ReadCompute(gi->GetIrradianceHistory());
            builder.ReadCompute(gi->GetDistanceHistory());
        }

        if (Image *screen = ActiveScreenIrradiance(RHII.GetFrameIndex()))
            builder.ReadCompute(screen);
        if (Image *reflections = ActiveReflections(RHII.GetFrameIndex()))
            builder.ReadCompute(reflections);

        builder.WriteCompute(m_viewportRT); // keeps LightOpaque's result: only MaterialX pixels are written
    }

    void MaterialShadePass::DeclareOutputs(RGBuilder &builder)
    {
        if (m_viewportRT && m_visibility)
            builder.OutputCustom(m_viewportRT, PE_IMAGE_LAYOUT_GENERAL, PE_STAGE_COMPUTE_SHADER, PE_ACCESS_SHADER_WRITE);
    }

    void MaterialShadePass::ExecutePass(CommandBuffer *cmd)
    {
        Scene *scene = GetActiveScene();
        const uint32_t frame = RHII.GetFrameIndex();
        if (!scene || !m_visibility || !m_viewportRT || frame >= m_boundFrames.size() || !m_boundFrames[frame])
            return;

        SetLightingPushConstants(cmd, m_viewportRT, 0u);
        cmd->SetConstantAt(21, static_cast<uint32_t>(scene->GetVerticesOffset())); // vertices_offset (PE_OPENPBR_SHADE)
        const int envSamples = std::clamp(Settings::Get<SceneSettings>().openpbr_env_samples, 1, 512);
        cmd->SetConstantAt(22, static_cast<uint32_t>(envSamples));

        cmd->BeginDebugRegion("MaterialShadePass");
        for (const auto &[module, info] : m_modulePassInfos)
        {
            cmd->SetConstantAt(23, module); // module_index (PE_OPENPBR_SHADE)
            cmd->BindPipeline(*info);
            cmd->PushConstants();
            cmd->Dispatch((m_visibility->GetWidth() * 2u + 7u) / 8u, (m_visibility->GetHeight() * 2u + 7u) / 8u, 1);
        }

        cmd->EndDebugRegion();
    }

    void MaterialShadePass::Resize(uint32_t width, uint32_t height)
    {
        (void)width;
        (void)height;
        Init();

        // HDR (viewport format) and the shadow settings change the shader: rebuild (only graphics passes are rebuilt for us).
        if (DefinesKey(Defines()) != m_builtDefines)
        {
            for (const auto &[module, info] : m_modulePassInfos)
                info->DestroyShaders();

            UpdatePassInfo();
        }

        UpdateDescriptorSets();
    }

    void MaterialShadePass::Destroy()
    {
        DestroyLightShadowFallbackResources(m_shadowFallbackUniforms,
                                            m_shadowFallbackTexture,
                                            m_shadowFallbackSampler,
                                            m_shadowFallbackViews);

        m_modulePassInfos.clear();

        m_viewportRT = m_visibility = m_albedoRT = nullptr;
        m_boundFrames.clear();
    }
} // namespace pe
