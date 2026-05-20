#include "Render/SceneRenderGraph.h"
#include "API/RHI.h"
#include "Base/Defines.h"
#include "Base/Settings.h"
#include "Base/ThreadPool.h"
#include "ECS/Component.h"
#include "ECS/Context.h"
#include "RenderPasses/AabbsPass.h"
#include "RenderPasses/BloomPass.h"
#include "RenderPasses/CullingPass.h"
#include "RenderPasses/DOFPass.h"
#include "RenderPasses/DepthPass.h"
#include "RenderPasses/FXAAPass.h"
#include "RenderPasses/GbufferPass.h"
#include "RenderPasses/GridPass.h"
#include "RenderPasses/LightPass.h"
#include "RenderPasses/MotionBlurPass.h"
#include "RenderPasses/ParticleComputePass.h"
#include "RenderPasses/ParticlePass.h"
#include "RenderPasses/RayTracingPass.h"
#include "RenderPasses/SSAOPass.h"
#include "RenderPasses/SSRPass.h"
#include "RenderPasses/ShadowPass.h"
#include "RenderPasses/SharpenPass.h"
#include "RenderPasses/TAAPass.h"
#include "RenderPasses/TonemapPass.h"
#include "RenderPasses/UpsamplePass.h"

#include <algorithm>
#include <future>

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
            {SceneRenderGraphPassId::Depth, 200, "Depth", &SceneRenderGraphPassComponents::depth},
            {SceneRenderGraphPassId::GBufferOpaque, 300, "GBufferOpaque", &SceneRenderGraphPassComponents::gbufferOpaque},
            {SceneRenderGraphPassId::SSAO, 400, "SSAO", &SceneRenderGraphPassComponents::ssao},
            {SceneRenderGraphPassId::LightOpaque, 500, "LightOpaque", &SceneRenderGraphPassComponents::lightOpaque},
            {SceneRenderGraphPassId::GBufferTransparent, 600, "GBufferTransparent",
             &SceneRenderGraphPassComponents::gbufferTransparent},
            {SceneRenderGraphPassId::LightTransparent, 700, "LightTransparent",
             &SceneRenderGraphPassComponents::lightTransparent},
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
            {SceneRenderGraphPassId::DOF, 2000, "DOF", &SceneRenderGraphPassComponents::dof},
            {SceneRenderGraphPassId::MotionBlur, 2100, "MotionBlur", &SceneRenderGraphPassComponents::motionBlur},
            {SceneRenderGraphPassId::Grid, 2200, "Grid", &SceneRenderGraphPassComponents::grid},
            {SceneRenderGraphPassId::Particle, 2300, "Particle", &SceneRenderGraphPassComponents::particle},
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
            if (component == components.depth)
                return isPassEnabled(SceneRenderGraphPassId::Depth);
            if (component == components.gbufferOpaque)
                return isPassEnabled(SceneRenderGraphPassId::GBufferOpaque);
            if (component == components.gbufferTransparent)
                return isPassEnabled(SceneRenderGraphPassId::GBufferTransparent);
            if (component == components.ssao)
                return isPassEnabled(SceneRenderGraphPassId::SSAO);
            if (component == components.lightOpaque)
                return isPassEnabled(SceneRenderGraphPassId::LightOpaque);
            if (component == components.lightTransparent)
                return isPassEnabled(SceneRenderGraphPassId::LightTransparent);
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
        CreateSceneRenderGraphPassComponent<DepthPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<GbufferOpaquePass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<GbufferTransparentPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<SSAOPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<LightOpaquePass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<LightTransparentPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<ParticleComputePass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<ParticlePass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<SSRPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<FXAAPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<AabbsPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<TAAPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<SharpenPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<UpsamplePass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<TonemapPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<BloomBrightFilterPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<BloomGaussianBlurHorizontalPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<BloomGaussianBlurVerticalPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<DOFPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<MotionBlurPass>(renderPassComponents);
        CreateSceneRenderGraphPassComponent<GridPass>(renderPassComponents);
        if (includeRayTracingPass)
            CreateSceneRenderGraphPassComponent<RayTracingPass>(renderPassComponents);
    }

    void InitSceneRenderGraphPassComponents(OrderedMap<size_t, IRenderPassComponent *> &renderPassComponents,
                                            CommandBuffer *cmd)
    {
        for (auto &renderPassComponent : renderPassComponents)
        {
            renderPassComponent->Init();
            renderPassComponent->UpdatePassInfo();
            renderPassComponent->CreateUniforms(cmd);
        }
    }

    void ResizeSceneRenderGraphPassComponents(OrderedMap<size_t, IRenderPassComponent *> &renderPassComponents,
                                              uint32_t width,
                                              uint32_t height)
    {
        for (auto &renderPassComponent : renderPassComponents)
            renderPassComponent->Resize(width, height);
    }

    void DestroySceneRenderGraphPassComponents(OrderedMap<size_t, IRenderPassComponent *> &renderPassComponents)
    {
        for (auto &renderPassComponent : renderPassComponents)
            renderPassComponent->Destroy();
    }

    SceneRenderGraphPassComponents GetGlobalSceneRenderGraphPassComponents()
    {
        SceneRenderGraphPassComponents scenePasses{};
        scenePasses.culling = GetGlobalComponent<CullingPass>();
        scenePasses.shadow = GetGlobalComponent<ShadowPass>();
        scenePasses.depth = GetGlobalComponent<DepthPass>();
        scenePasses.gbufferOpaque = GetGlobalComponent<GbufferOpaquePass>();
        scenePasses.ssao = GetGlobalComponent<SSAOPass>();
        scenePasses.lightOpaque = GetGlobalComponent<LightOpaquePass>();
        scenePasses.gbufferTransparent = GetGlobalComponent<GbufferTransparentPass>();
        scenePasses.lightTransparent = GetGlobalComponent<LightTransparentPass>();
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
        scenePasses.bloomBrightFilter = GetGlobalComponent<BloomBrightFilterPass>();
        scenePasses.bloomGaussianBlurHorizontal = GetGlobalComponent<BloomGaussianBlurHorizontalPass>();
        scenePasses.bloomGaussianBlurVertical = GetGlobalComponent<BloomGaussianBlurVerticalPass>();
        scenePasses.dof = GetGlobalComponent<DOFPass>();
        scenePasses.motionBlur = GetGlobalComponent<MotionBlurPass>();
        scenePasses.grid = GetGlobalComponent<GridPass>();
        return scenePasses;
    }

    void UpdateSceneRenderGraphPassStates(std::span<bool> passEnabled, bool hasRayTracingGeometry)
    {
        PE_ASSERT(passEnabled.size() >= kSceneRenderGraphPassCount, "Scene render graph pass state span is too small");

        std::fill(passEnabled.begin(), passEnabled.end(), false);

        const auto &gs = Settings::Get<GlobalSettings>();

        const bool renderRaster = (gs.render_mode != RenderMode::RayTracing) || !hasRayTracingGeometry;
        const bool renderRayTracing = (gs.render_mode != RenderMode::Raster) && hasRayTracingGeometry;
        const bool needVelocity = gs.taa || gs.motion_blur;
        const bool renderSSR = gs.ssr && renderRaster;
        const bool renderSSAO = gs.ssao && renderRaster;
        const bool needDepth = renderRaster || needVelocity || gs.dof || gs.motion_blur || gs.draw_aabbs || gs.draw_grid;
        const bool needGBuffer = renderRaster || needVelocity || renderSSR || renderSSAO;

        if (UsesDx12RenderOrchestration())
        {
            const bool dx12RayTracing = renderRayTracing;
            const bool dx12RenderRaster = renderRaster || !dx12RayTracing;
            const bool dx12RtOnly = dx12RayTracing && !dx12RenderRaster;
            const bool dx12RenderTAA = gs.taa && (dx12RenderRaster || dx12RtOnly);
            const bool dx12NeedVelocity = dx12RenderTAA || (gs.motion_blur && dx12RenderRaster);
            const bool dx12NeedDepth = dx12RenderRaster || dx12NeedVelocity || gs.draw_aabbs || gs.draw_grid;
            const bool dx12NeedGBuffer = dx12RenderRaster || dx12NeedVelocity;

            SetPassEnabled(passEnabled, SceneRenderGraphPassId::Culling, true);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::Shadow, gs.shadows && dx12RenderRaster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::Depth, dx12NeedDepth);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::GBufferOpaque, dx12NeedGBuffer);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::SSAO, gs.ssao && dx12RenderRaster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::LightOpaque, dx12RenderRaster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::GBufferTransparent, gs.render_mode == RenderMode::Raster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::LightTransparent, dx12RenderRaster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::RayTracing, dx12RayTracing);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::ParticleCompute, dx12RenderRaster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::Particle, dx12RenderRaster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::SSR, gs.ssr && dx12RenderRaster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::FXAA, gs.fxaa && dx12RenderRaster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::Aabbs, gs.draw_aabbs);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::TAA, dx12RenderTAA);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::Sharpen, dx12RenderTAA && gs.cas_sharpening);
            SetPassEnabled(passEnabled,
                           SceneRenderGraphPassId::Upsample,
                           (!gs.taa && dx12RenderRaster) || (dx12RtOnly && !dx12RenderTAA));
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::Tonemap, gs.tonemapping && dx12RenderRaster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::BloomBF, gs.bloom && dx12RenderRaster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::BloomH, gs.bloom && dx12RenderRaster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::BloomV, gs.bloom && dx12RenderRaster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::DOF, gs.dof && dx12RenderRaster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::MotionBlur, gs.motion_blur && dx12RenderRaster);
            SetPassEnabled(passEnabled, SceneRenderGraphPassId::Grid, gs.draw_grid);
            return;
        }

        SetPassEnabled(passEnabled, SceneRenderGraphPassId::Culling, true);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::Shadow, gs.shadows && renderRaster);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::Depth, needDepth);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::GBufferOpaque, needGBuffer);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::SSAO, renderSSAO);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::LightOpaque, renderRaster);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::GBufferTransparent, gs.render_mode == RenderMode::Raster);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::LightTransparent, renderRaster);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::RayTracing, renderRayTracing);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::ParticleCompute, true);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::Particle, true);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::SSR, renderSSR);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::FXAA, gs.fxaa);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::Aabbs, gs.draw_aabbs);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::TAA, gs.taa);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::Sharpen, gs.taa && gs.cas_sharpening);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::Upsample, !gs.taa);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::Tonemap, gs.tonemapping);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::BloomBF, gs.bloom);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::BloomH, gs.bloom);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::BloomV, gs.bloom);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::DOF, gs.dof);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::MotionBlur, gs.motion_blur);
        SetPassEnabled(passEnabled, SceneRenderGraphPassId::Grid, gs.draw_grid);
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
        SetPassScene<DepthPass>(components.depth, scene);
        SetPassScene<GbufferOpaquePass>(components.gbufferOpaque, scene);
        SetPassScene<GbufferTransparentPass>(components.gbufferTransparent, scene);
        SetPassScene<RayTracingPass>(components.rayTracing, scene);
        SetPassScene<ParticleComputePass>(components.particleCompute, scene);
        SetPassScene<ParticlePass>(components.particle, scene);
        SetPassScene<GridPass>(components.grid, scene);
        SetPassScene<AabbsPass>(components.aabbs, scene);
    }
} // namespace pe
