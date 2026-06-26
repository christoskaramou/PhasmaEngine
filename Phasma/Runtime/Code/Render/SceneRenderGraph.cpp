#include "Render/SceneRenderGraph.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "RenderPasses/AabbsPass.h"
#include "RenderPasses/BloomPass.h"
#include "RenderPasses/CullPhase1Pass.h"
#include "RenderPasses/CullingPass.h"
#include "RenderPasses/ColorGradingPass.h"
#include "RenderPasses/DOFPass.h"
#include "RenderPasses/DepthLatePass.h"
#include "RenderPasses/DepthPass.h"
#include "RenderPasses/DepthPyramidPass.h"
#include "RenderPasses/FXAAPass.h"
#include "RenderPasses/ForwardPlusLightCullingPass.h"
#include "RenderPasses/GbufferPass.h"
#include "RenderPasses/GridPass.h"
#include "RenderPasses/LightPass.h"
#include "RenderPasses/LinesPass.h"
#include "RenderPasses/MotionBlurPass.h"
#include "RenderPasses/OcclusionCullingPass.h"
#include "RenderPasses/ParticleComputePass.h"
#include "RenderPasses/ParticlePass.h"
#include "RenderPasses/RayTracingPass.h"
#include "RenderPasses/SelectionOutlinePass.h"
#include "RenderPasses/SSAOPass.h"
#include "RenderPasses/SSRPass.h"
#include "RenderPasses/ShadowPass.h"
#include "RenderPasses/SharpenPass.h"
#include "RenderPasses/TAAPass.h"
#include "RenderPasses/TonemapPass.h"
#include "RenderPasses/UpsamplePass.h"
#include "Scene/Scene.h"

namespace pe
{
    namespace
    {
        struct SceneRenderGraphPassDesc
        {
            SceneRenderGraphPassId id;
            uint32_t order;
            const char *name;
            IRenderPassComponent *SceneRenderGraphPassComponents::*component;
        };

        constexpr SceneRenderGraphPassDesc kSceneRenderGraphPasses[] = {
            {SceneRenderGraphPassId::Culling, 50, "Culling", &SceneRenderGraphPassComponents::culling},
            {SceneRenderGraphPassId::Shadow, 100, "Shadow", &SceneRenderGraphPassComponents::shadow},
            {SceneRenderGraphPassId::CullPhase1, 180, "CullPhase1", &SceneRenderGraphPassComponents::cullPhase1},
            {SceneRenderGraphPassId::Depth, 200, "Depth", &SceneRenderGraphPassComponents::depth},
            {SceneRenderGraphPassId::DepthPyramid, 250, "DepthPyramid", &SceneRenderGraphPassComponents::depthPyramid},
            {SceneRenderGraphPassId::OcclusionCulling, 260, "CullPhase2", &SceneRenderGraphPassComponents::occlusionCulling},
            {SceneRenderGraphPassId::DepthLate, 270, "DepthLate", &SceneRenderGraphPassComponents::depthLate},
            {SceneRenderGraphPassId::GBufferOpaque, 300, "GBufferOpaque", &SceneRenderGraphPassComponents::gbufferOpaque},
            {SceneRenderGraphPassId::SSAO, 400, "SSAO", &SceneRenderGraphPassComponents::ssao},
            {SceneRenderGraphPassId::ForwardPlusLightCulling, 450, "ForwardPlusLightCulling",
             &SceneRenderGraphPassComponents::forwardPlusLightCulling},
            {SceneRenderGraphPassId::LightOpaque, 500, "LightOpaque", &SceneRenderGraphPassComponents::lightOpaque},
            {SceneRenderGraphPassId::GBufferTransparent, 600, "GBufferTransparent",
             &SceneRenderGraphPassComponents::gbufferTransparent},
            {SceneRenderGraphPassId::LightTransparent, 700, "LightTransparent",
             &SceneRenderGraphPassComponents::lightTransparent},
            {SceneRenderGraphPassId::Lines, 720, "Lines", &SceneRenderGraphPassComponents::lines},
            {SceneRenderGraphPassId::RayTracing, 800, "RayTracing", &SceneRenderGraphPassComponents::rayTracing},
            {SceneRenderGraphPassId::ParticleCompute, 900, "ParticleCompute",
             &SceneRenderGraphPassComponents::particleCompute},
            {SceneRenderGraphPassId::SSR, 1000, "SSR", &SceneRenderGraphPassComponents::ssr},
            {SceneRenderGraphPassId::FXAA, 1100, "FXAA", &SceneRenderGraphPassComponents::fxaa},
            {SceneRenderGraphPassId::Aabbs, 1200, "Aabbs", &SceneRenderGraphPassComponents::aabbs},
            {SceneRenderGraphPassId::TAA, 1300, "TAA", &SceneRenderGraphPassComponents::taa},
            {SceneRenderGraphPassId::Sharpen, 1400, "Sharpen", &SceneRenderGraphPassComponents::sharpen},
            {SceneRenderGraphPassId::Upsample, 1500, "Upsample", &SceneRenderGraphPassComponents::upsample},
            {SceneRenderGraphPassId::Tonemap, 1600, "Tonemap", &SceneRenderGraphPassComponents::tonemap},
            {SceneRenderGraphPassId::BloomBF, 1700, "BloomBF", &SceneRenderGraphPassComponents::bloomBrightFilter},
            {SceneRenderGraphPassId::BloomH, 1800, "BloomH",
             &SceneRenderGraphPassComponents::bloomGaussianBlurHorizontal},
            {SceneRenderGraphPassId::BloomV, 1900, "BloomV",
             &SceneRenderGraphPassComponents::bloomGaussianBlurVertical},
            {SceneRenderGraphPassId::ColorGrading, 1950, "ColorGrading", &SceneRenderGraphPassComponents::colorGrading},
            {SceneRenderGraphPassId::DOF, 2000, "DOF", &SceneRenderGraphPassComponents::dof},
            {SceneRenderGraphPassId::MotionBlur, 2100, "MotionBlur", &SceneRenderGraphPassComponents::motionBlur},
            {SceneRenderGraphPassId::Grid, 1240, "Grid", &SceneRenderGraphPassComponents::grid},
            {SceneRenderGraphPassId::Particle, 2300, "Particle", &SceneRenderGraphPassComponents::particle},
            // Before TAA (1300): the temporal resolve de-jitters the outline with the scene.
            {SceneRenderGraphPassId::SelectionOutline, 1250, "SelectionOutline",
             &SceneRenderGraphPassComponents::selectionOutline},
        };

        bool UsesDx12RenderOrchestration()
        {
            return RHII.GetApi() == PE_GRAPHICS_API_DX12;
        }

        void SetPassEnabled(std::span<bool> passEnabled, SceneRenderGraphPassId passId, bool enabled)
        {
            passEnabled[static_cast<size_t>(passId)] = enabled;
        }

        template <class T>
        void CreateSceneRenderGraphPassComponent(OrderedMap<size_t, IRenderPassComponent *> &renderPassComponents)
        {
            renderPassComponents[ID::GetTypeID<T>()] = CreateGlobalComponent<T>();
        }

        template <class T>
        void SetPassScene(IRenderPassComponent *component, Scene &scene)
        {
            if (component)
                static_cast<T *>(component)->SetScene(&scene);
        }

        void InitSceneRenderGraphPassComponent(IRenderPassComponent *component, CommandBuffer *cmd)
        {
            component->Init();
            for (PassInfo *passInfo : component->GetPassInfos())
            {
                if (passInfo)
                    passInfo->DestroyShaders();
            }
            component->UpdatePassInfo();
            component->CreateUniforms(cmd);
        }

        bool ShouldKeepSceneRenderGraphPassInitialized(SceneRenderGraphPassId passId,
                                                       const SceneRenderGraphPassCondition &isPassEnabled)
        {
            return isPassEnabled(passId);
        }

        bool ShouldUpdateSceneRenderGraphPass(const SceneRenderGraphPassComponents &components,
                                              const SceneRenderGraphPassCondition &isPassEnabled,
                                              IRenderPassComponent *component)
        {
            if (!component || !component->IsEnabled())
                return false;

            if (component == components.culling)
                return isPassEnabled(SceneRenderGraphPassId::Culling);
            if (component == components.shadow)
                return isPassEnabled(SceneRenderGraphPassId::Shadow);
            if (component == components.cullPhase1)
                return isPassEnabled(SceneRenderGraphPassId::CullPhase1);
            if (component == components.depth)
                return isPassEnabled(SceneRenderGraphPassId::Depth);
            if (component == components.depthPyramid)
                return isPassEnabled(SceneRenderGraphPassId::DepthPyramid);
            if (component == components.occlusionCulling)
                return isPassEnabled(SceneRenderGraphPassId::OcclusionCulling);
            if (component == components.depthLate)
                return isPassEnabled(SceneRenderGraphPassId::DepthLate);
            if (component == components.gbufferOpaque)
                return isPassEnabled(SceneRenderGraphPassId::GBufferOpaque);
            if (component == components.gbufferTransparent)
                return isPassEnabled(SceneRenderGraphPassId::GBufferTransparent);
            if (component == components.ssao)
                return isPassEnabled(SceneRenderGraphPassId::SSAO);
            if (component == components.forwardPlusLightCulling)
                return isPassEnabled(SceneRenderGraphPassId::ForwardPlusLightCulling);
            if (component == components.lightOpaque)
                return isPassEnabled(SceneRenderGraphPassId::LightOpaque);
            if (component == components.lightTransparent)
                return isPassEnabled(SceneRenderGraphPassId::LightTransparent);
            if (component == components.lines)
                return isPassEnabled(SceneRenderGraphPassId::Lines);
            if (component == components.rayTracing)
                return isPassEnabled(SceneRenderGraphPassId::RayTracing);
            if (component == components.particleCompute || component == components.particle)
                return isPassEnabled(SceneRenderGraphPassId::ParticleCompute) ||
                       isPassEnabled(SceneRenderGraphPassId::Particle);
            if (component == components.ssr)
                return isPassEnabled(SceneRenderGraphPassId::SSR);
            if (component == components.fxaa)
                return isPassEnabled(SceneRenderGraphPassId::FXAA);
            if (component == components.aabbs)
                return isPassEnabled(SceneRenderGraphPassId::Aabbs);
            if (component == components.taa)
                return isPassEnabled(SceneRenderGraphPassId::TAA);
            if (component == components.sharpen)
                return isPassEnabled(SceneRenderGraphPassId::Sharpen);
            if (component == components.upsample)
                return isPassEnabled(SceneRenderGraphPassId::Upsample);
            if (component == components.tonemap)
                return isPassEnabled(SceneRenderGraphPassId::Tonemap);
            if (component == components.colorGrading)
                return isPassEnabled(SceneRenderGraphPassId::ColorGrading);
            if (component == components.bloomBrightFilter || component == components.bloomGaussianBlurHorizontal ||
                component == components.bloomGaussianBlurVertical)
            {
                return isPassEnabled(SceneRenderGraphPassId::BloomBF) ||
                       isPassEnabled(SceneRenderGraphPassId::BloomH) ||
                       isPassEnabled(SceneRenderGraphPassId::BloomV);
            }
            if (component == components.dof)
                return isPassEnabled(SceneRenderGraphPassId::DOF);
            if (component == components.motionBlur)
                return isPassEnabled(SceneRenderGraphPassId::MotionBlur);
            if (component == components.grid)
                return isPassEnabled(SceneRenderGraphPassId::Grid);
            if (component == components.selectionOutline)
                return isPassEnabled(SceneRenderGraphPassId::SelectionOutline);

            return true;
        }
    } // namespace

    void AddSceneRenderGraphPasses(RenderGraph &renderGraph,
                                   const SceneRenderGraphPassComponents &components,
                                   SceneRenderGraphPassCondition isPassEnabled)
    {
        for (const SceneRenderGraphPassDesc &desc : kSceneRenderGraphPasses)
        {
            auto condition = [isPassEnabled, passId = desc.id]()
            {
                return isPassEnabled(passId);
            };
            renderGraph.AddPass(static_cast<RenderGraph::PassID>(desc.id),
                                desc.order,
                                desc.name,
                                condition,
                                components.*desc.component);
        }
    }

    void CreateSceneRenderGraphPassComponents(OrderedMap<size_t, IRenderPassComponent *> &renderPassComponents,
                                              bool includeRayTracingPass)
    {
        CreateSceneRenderGraphPassComponent<CullingPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<ShadowPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<CullPhase1Pass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<DepthPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<DepthPyramidPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<OcclusionCullingPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<DepthLatePass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<GbufferOpaquePass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<GbufferTransparentPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<SSAOPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<ForwardPlusLightCullingPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<LightOpaquePass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<LightTransparentPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<LinesPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<ParticleComputePass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<ParticlePass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<SSRPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<FXAAPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<AabbsPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<TAAPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<SharpenPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<UpsamplePass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<TonemapPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<ColorGradingPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<BloomBrightFilterPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<BloomGaussianBlurHorizontalPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<BloomGaussianBlurVerticalPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<DOFPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<MotionBlurPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<GridPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<SelectionOutlinePass>(renderPassComponents);
        if (includeRayTracingPass)
            CreateSceneRenderGraphPassComponent<RayTracingPass>(renderPassComponents);
    }

    void InitEnabledSceneRenderGraphPassComponents(const SceneRenderGraphPassComponents &components,
                                                   SceneRenderGraphPassCondition isPassEnabled,
                                                   std::span<bool> passInitialized,
                                                   CommandBuffer *cmd)
    {
        PE_ASSERT(passInitialized.size() >= kSceneRenderGraphPassCount,
                  "Scene render graph pass init state span is too small");

        for (const SceneRenderGraphPassDesc &desc : kSceneRenderGraphPasses)
        {
            const size_t index = static_cast<size_t>(desc.id);
            if (passInitialized[index] || !ShouldKeepSceneRenderGraphPassInitialized(desc.id, isPassEnabled))
                continue;

            IRenderPassComponent *component = components.*desc.component;
            if (!component)
                continue;

            InitSceneRenderGraphPassComponent(component, cmd);
            passInitialized[index] = true;
        }
    }

    void ResizeInitializedSceneRenderGraphPassComponents(const SceneRenderGraphPassComponents &components,
                                                         SceneRenderGraphPassCondition isPassEnabled,
                                                         std::span<bool> passInitialized,
                                                         uint32_t width,
                                                         uint32_t height)
    {
        PE_ASSERT(passInitialized.size() >= kSceneRenderGraphPassCount,
                  "Scene render graph pass init state span is too small");

        for (const SceneRenderGraphPassDesc &desc : kSceneRenderGraphPasses)
        {
            const size_t index = static_cast<size_t>(desc.id);
            if (!passInitialized[index])
                continue;

            IRenderPassComponent *component = components.*desc.component;
            if (!component)
            {
                passInitialized[index] = false;
                continue;
            }

            if (ShouldKeepSceneRenderGraphPassInitialized(desc.id, isPassEnabled))
            {
                component->Resize(width, height);
            }
            else
            {
                component->Destroy();
                passInitialized[index] = false;
            }
        }
    }

    bool HasDisabledInitializedSceneRenderGraphPassComponents(const SceneRenderGraphPassComponents &components,
                                                              SceneRenderGraphPassCondition isPassEnabled,
                                                              std::span<const bool> passInitialized)
    {
        (void)components;
        PE_ASSERT(passInitialized.size() >= kSceneRenderGraphPassCount,
                  "Scene render graph pass init state span is too small");

        for (const SceneRenderGraphPassDesc &desc : kSceneRenderGraphPasses)
        {
            const size_t index = static_cast<size_t>(desc.id);
            if (passInitialized[index] && !ShouldKeepSceneRenderGraphPassInitialized(desc.id, isPassEnabled))
                return true;
        }

        return false;
    }

    void DestroyDisabledSceneRenderGraphPassComponents(const SceneRenderGraphPassComponents &components,
                                                       SceneRenderGraphPassCondition isPassEnabled,
                                                       std::span<bool> passInitialized)
    {
        PE_ASSERT(passInitialized.size() >= kSceneRenderGraphPassCount,
                  "Scene render graph pass init state span is too small");

        for (const SceneRenderGraphPassDesc &desc : kSceneRenderGraphPasses)
        {
            const size_t index = static_cast<size_t>(desc.id);
            if (!passInitialized[index] || ShouldKeepSceneRenderGraphPassInitialized(desc.id, isPassEnabled))
                continue;

            if (IRenderPassComponent *component = components.*desc.component)
                component->Destroy();
            passInitialized[index] = false;
        }
    }

    void DestroyInitializedSceneRenderGraphPassComponents(const SceneRenderGraphPassComponents &components,
                                                          std::span<bool> passInitialized)
    {
        PE_ASSERT(passInitialized.size() >= kSceneRenderGraphPassCount,
                  "Scene render graph pass init state span is too small");

        for (const SceneRenderGraphPassDesc &desc : kSceneRenderGraphPasses)
        {
            const size_t index = static_cast<size_t>(desc.id);
            if (!passInitialized[index])
                continue;

            if (IRenderPassComponent *component = components.*desc.component)
                component->Destroy();
            passInitialized[index] = false;
        }
    }

    SceneRenderGraphPassComponents GetGlobalSceneRenderGraphPassComponents()
    {
        SceneRenderGraphPassComponents scenePasses{};
        scenePasses.culling = GetGlobalComponent<CullingPass>();
        scenePasses.shadow = GetGlobalComponent<ShadowPass>();
        scenePasses.cullPhase1 = GetGlobalComponent<CullPhase1Pass>();
        scenePasses.depth = GetGlobalComponent<DepthPass>();
        scenePasses.depthPyramid = GetGlobalComponent<DepthPyramidPass>();
        scenePasses.occlusionCulling = GetGlobalComponent<OcclusionCullingPass>();
        scenePasses.depthLate = GetGlobalComponent<DepthLatePass>();
        scenePasses.gbufferOpaque = GetGlobalComponent<GbufferOpaquePass>();
        scenePasses.ssao = GetGlobalComponent<SSAOPass>();
        scenePasses.forwardPlusLightCulling = GetGlobalComponent<ForwardPlusLightCullingPass>();
        scenePasses.lightOpaque = GetGlobalComponent<LightOpaquePass>();
        scenePasses.gbufferTransparent = GetGlobalComponent<GbufferTransparentPass>();
        scenePasses.lightTransparent = GetGlobalComponent<LightTransparentPass>();
        scenePasses.lines = GetGlobalComponent<LinesPass>();
        scenePasses.rayTracing = GetGlobalComponent<RayTracingPass>();
        scenePasses.particleCompute = GetGlobalComponent<ParticleComputePass>();
        scenePasses.particle = GetGlobalComponent<ParticlePass>();
        scenePasses.ssr = GetGlobalComponent<SSRPass>();
        scenePasses.fxaa = GetGlobalComponent<FXAAPass>();
        scenePasses.aabbs = GetGlobalComponent<AabbsPass>();
        scenePasses.taa = GetGlobalComponent<TAAPass>();
        scenePasses.sharpen = GetGlobalComponent<SharpenPass>();
        scenePasses.upsample = GetGlobalComponent<UpsamplePass>();
        scenePasses.tonemap = GetGlobalComponent<TonemapPass>();
        scenePasses.colorGrading = GetGlobalComponent<ColorGradingPass>();
        scenePasses.bloomBrightFilter = GetGlobalComponent<BloomBrightFilterPass>();
        scenePasses.bloomGaussianBlurHorizontal = GetGlobalComponent<BloomGaussianBlurHorizontalPass>();
        scenePasses.bloomGaussianBlurVertical = GetGlobalComponent<BloomGaussianBlurVerticalPass>();
        scenePasses.dof = GetGlobalComponent<DOFPass>();
        scenePasses.motionBlur = GetGlobalComponent<MotionBlurPass>();
        scenePasses.grid = GetGlobalComponent<GridPass>();
        scenePasses.selectionOutline = GetGlobalComponent<SelectionOutlinePass>();
        return scenePasses;
    }

    void UpdateSceneRenderGraphPassStates(std::span<bool> passEnabled, bool hasRayTracingGeometry)
    {
        PE_ASSERT(passEnabled.size() >= kSceneRenderGraphPassCount, "Scene render graph pass state span is too small");

        std::fill(passEnabled.begin(), passEnabled.end(), false);

        // The master switch + active post-process profile were resolved at the top of
        // Scene::UpdateCameras() THIS frame, before cameras computed projection jitter from
        // ActivePostProcessProfile().taa. Read the already-resolved state here (re-resolving would
        // let the camera and this gating disagree on the frame a volume is crossed / the node toggled).
        // Master-switch scope: when inactive (node disabled / deleted / absent) the active profile is
        // all-off (kills post-process) and shadows are dropped below. Performance/debug toggles
        // (culling, Forward+, grid, AABBs) are independent of it and keep reading gs.
        const bool settingsActive = SceneSettingsActive();

        const auto &gs = Settings::Get<SceneSettings>();
        const auto &pp = ActivePostProcessProfile();

        const bool renderRaster = (gs.render_mode != RenderMode::RayTracing) || !hasRayTracingGeometry;
        const bool renderRayTracing = (gs.render_mode != RenderMode::Raster) && hasRayTracingGeometry;
        const bool needVelocity = pp.taa || pp.motion_blur;
        const bool renderSSR = pp.ssr && renderRaster;
        const bool renderSSAO = pp.ssao && renderRaster;
        const bool needDepth = renderRaster || needVelocity || pp.dof || pp.motion_blur || gs.draw_aabbs || gs.draw_grid;
        const bool needGBuffer = renderRaster || needVelocity || renderSSR || renderSSAO;

        if (UsesDx12RenderOrchestration())
        {
            const bool dx12RayTracing = renderRayTracing;
            const bool dx12RenderRaster = renderRaster || !dx12RayTracing;
            const bool dx12RtOnly = dx12RayTracing && !dx12RenderRaster;
            const bool dx12RenderTAA = pp.taa && (dx12RenderRaster || dx12RtOnly);
            const bool dx12NeedVelocity = dx12RenderTAA || (pp.motion_blur && dx12RenderRaster);
            const bool dx12NeedDepth = dx12RenderRaster || dx12NeedVelocity || gs.draw_aabbs || gs.draw_grid;
            const bool dx12NeedGBuffer = dx12RenderRaster || dx12NeedVelocity;

            SetPassEnabled(passEnabled, SceneRenderGraphPassId::Culling, true);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::Shadow, settingsActive && gs.shadows && dx12RenderRaster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::CullPhase1, gs.occlusion_culling && dx12NeedDepth);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::Depth, dx12NeedDepth);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::DepthPyramid, gs.occlusion_culling && dx12NeedDepth);
            // Phase 2 + late depth gate on needDepth (not needGBuffer): whenever the depth prepass
            // draws set A only, set B must be added back so depth-only consumers (DOF/motion/etc) see
            // a complete depth buffer. needGBuffer is a subset of needDepth, so this never under-runs.
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::OcclusionCulling, gs.occlusion_culling && dx12NeedDepth);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::DepthLate, gs.occlusion_culling && dx12NeedDepth);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::GBufferOpaque, dx12NeedGBuffer);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::SSAO, pp.ssao && dx12RenderRaster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::ForwardPlusLightCulling,
                           gs.forward_plus && dx12RenderRaster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::LightOpaque, dx12RenderRaster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::GBufferTransparent, dx12RenderRaster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::LightTransparent, dx12RenderRaster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::Lines, dx12RenderRaster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::RayTracing, dx12RayTracing);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::ParticleCompute, dx12RenderRaster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::Particle, dx12RenderRaster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::SSR, pp.ssr && dx12RenderRaster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::FXAA, pp.fxaa && dx12RenderRaster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::Aabbs, gs.draw_aabbs);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::TAA, dx12RenderTAA);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::Sharpen, dx12RenderTAA && pp.cas_sharpening);
            SetPassEnabled(passEnabled,
                           SceneRenderGraphPassId::Upsample,
                           (!pp.taa && dx12RenderRaster) || (dx12RtOnly && !dx12RenderTAA));
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::Tonemap, pp.tonemapping && dx12RenderRaster);
            SetPassEnabled(passEnabled,
                           SceneRenderGraphPassId::ColorGrading,
                           pp.color_grading && dx12RenderRaster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::BloomBF, pp.bloom && dx12RenderRaster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::BloomH, pp.bloom && dx12RenderRaster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::BloomV, pp.bloom && dx12RenderRaster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::DOF, pp.dof && dx12RenderRaster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::MotionBlur, pp.motion_blur && dx12RenderRaster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::Grid, gs.draw_grid);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::SelectionOutline,
                           gs.selection_outline && dx12RenderRaster);
            return;
        }

        SetPassEnabled(passEnabled, SceneRenderGraphPassId::Culling, true);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::Shadow, settingsActive && gs.shadows && renderRaster);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::CullPhase1, gs.occlusion_culling && needDepth);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::Depth, needDepth);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::DepthPyramid, gs.occlusion_culling && needDepth);
        // Phase 2 + late depth gate on needDepth (not needGBuffer): whenever the depth prepass draws
        // set A only, set B must be added back so depth-only consumers see a complete depth buffer.
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::OcclusionCulling, gs.occlusion_culling && needDepth);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::DepthLate, gs.occlusion_culling && needDepth);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::GBufferOpaque, needGBuffer);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::SSAO, renderSSAO);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::ForwardPlusLightCulling, gs.forward_plus && renderRaster);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::LightOpaque, renderRaster);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::GBufferTransparent, renderRaster);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::LightTransparent, renderRaster);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::Lines, renderRaster);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::RayTracing, renderRayTracing);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::ParticleCompute, true);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::Particle, true);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::SSR, renderSSR);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::FXAA, pp.fxaa);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::Aabbs, gs.draw_aabbs);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::TAA, pp.taa);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::Sharpen, pp.taa && pp.cas_sharpening);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::Upsample, !pp.taa);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::Tonemap, pp.tonemapping);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::ColorGrading, pp.color_grading);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::BloomBF, pp.bloom);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::BloomH, pp.bloom);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::BloomV, pp.bloom);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::DOF, pp.dof);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::MotionBlur, pp.motion_blur);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::Grid, gs.draw_grid);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::SelectionOutline, gs.selection_outline && renderRaster);
    }

    void UpdateSceneRenderGraphPassComponents(const OrderedMap<size_t, IRenderPassComponent *> &renderPassComponents,
                                              const SceneRenderGraphPassComponents &components,
                                              const SceneRenderGraphPassCondition &isPassEnabled)
    {
        std::vector<std::shared_future<void>> futures;
        futures.reserve(renderPassComponents.size());
        for (auto &rc : renderPassComponents)
        {
            if (!ShouldUpdateSceneRenderGraphPass(components, isPassEnabled, rc))
                continue;

            futures.push_back(ThreadPool::Update.Enqueue([rc]()
                                                         { rc->Update(); }));
        }

        for (auto &future : futures)
            future.wait();
    }

    void SetSceneRenderGraphPassScene(const SceneRenderGraphPassComponents &components, Scene &scene)
    {
        SetPassScene<CullingPass>(components.culling, scene);
        SetPassScene<ShadowPass>(components.shadow, scene);
        SetPassScene<CullPhase1Pass>(components.cullPhase1, scene);
        SetPassScene<DepthPass>(components.depth, scene);
        SetPassScene<OcclusionCullingPass>(components.occlusionCulling, scene);
        SetPassScene<DepthLatePass>(components.depthLate, scene);
        SetPassScene<GbufferOpaquePass>(components.gbufferOpaque, scene);
        SetPassScene<GbufferTransparentPass>(components.gbufferTransparent, scene);
        SetPassScene<RayTracingPass>(components.rayTracing, scene);
        SetPassScene<ParticleComputePass>(components.particleCompute, scene);
        SetPassScene<ParticlePass>(components.particle, scene);
        SetPassScene<GridPass>(components.grid, scene);
        SetPassScene<AabbsPass>(components.aabbs, scene);
        SetPassScene<LinesPass>(components.lines, scene);
        SetPassScene<SelectionOutlinePass>(components.selectionOutline, scene);
    }
} // namespace pe
