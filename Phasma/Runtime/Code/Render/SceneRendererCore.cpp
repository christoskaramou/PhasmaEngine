#include "Render/SceneRendererCore.h"
#include "Render/ScenePerception.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "Render/RenderPassShaderReload.h"
#include "Render/SceneScreenshot.h"
#include "Render/SceneSky.h"
#include "Render/SceneRendererHost.h"
#include "Render/ScriptRenderPasses.h"
#include "Scene/MaterialReflection.h"
#include "Scene/OpenPBRMaterial.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "RenderPasses/RayTracingPass.h"
#include "RenderPasses/MaterialThinPass.h"
#include "RenderPasses/LightPass.h"
#include "RenderPasses/DepthPass.h"
#include "RenderPasses/DepthLatePass.h"
#include "RenderPasses/ShadowPass.h"

namespace pe
{
    Image *SceneRendererCore::CreateRenderTarget(const std::string &name,
                                                 ::PeFormat format,
                                                 PeImageUsageFlags usage,
                                                 bool useRenderTargetScale,
                                                 bool useMips,
                                                 vec4 clearColor)
    {
        return CreateSceneRenderTarget(m_renderTargets, name, format, usage, useRenderTargetScale, useMips, clearColor);
    }

    Image *SceneRendererCore::CreateDepthStencilTarget(const std::string &name,
                                                       ::PeFormat format,
                                                       PeImageUsageFlags usage,
                                                       bool useRenderTargetScale,
                                                       float clearDepth,
                                                       uint32_t clearStencil)
    {
        return CreateSceneDepthStencilTarget(m_depthStencilTargets,
                                             name,
                                             format,
                                             usage,
                                             useRenderTargetScale,
                                             clearDepth,
                                             clearStencil);
    }

    Image *SceneRendererCore::GetRenderTarget(const std::string &name) const
    {
        if (m_frameDisplayOverride && name == "display")
            return m_frameDisplayOverride;
        return GetSceneRenderTarget(m_renderTargets, name);
    }

    Image *SceneRendererCore::GetRenderTarget(size_t hash) const
    {
        if (m_frameDisplayOverride && hash == static_cast<size_t>(StringHash("display")))
            return m_frameDisplayOverride;
        return GetSceneRenderTarget(m_renderTargets, hash);
    }

    bool SceneRendererCore::DestroyRenderTarget(const std::string &name)
    {
        return DestroySceneRenderTarget(m_renderTargets, name);
    }

    Image *SceneRendererCore::GetDepthStencilTarget(const std::string &name) const
    {
        return GetSceneRenderTarget(m_depthStencilTargets, name);
    }

    Image *SceneRendererCore::GetDepthStencilTarget(size_t hash) const
    {
        return GetSceneRenderTarget(m_depthStencilTargets, hash);
    }

    Image *SceneRendererCore::CreateFSSampledImage(const std::string &name, Image *source)
    {
        return CreateSceneFSSampledImage(name, source);
    }

    void SceneRendererCore::CreateRenderTargets(bool hasRayTracingGeometry, bool scaleOutput)
    {
        m_colorFormatChanged = m_colorFormatChanged ||
                               (m_viewportRT && (m_hdr != SceneUsesHDR() || m_openpbr != SceneUsesOpenPBR()));

        if (m_openpbr != SceneUsesOpenPBR())
            SyncOpenPBREnvironment();

        m_hdr = SceneUsesHDR();
        m_openpbr = SceneUsesOpenPBR();

        const SceneRenderTargets targets =
            CreateDefaultSceneRenderTargets(m_renderTargets, m_depthStencilTargets, hasRayTracingGeometry, scaleOutput);
        m_depthStencil = targets.depthStencil;
        m_viewportRT = targets.viewport;
        m_displayRT = targets.display;
        m_screenshotRT = targets.screenshot;
        m_renderTargetScale = Settings::Get<SceneSettings>().render_scale;
    }

    void SceneRendererCore::DestroyRenderTargets()
    {
        DestroySceneRenderTargets(m_renderTargets, m_depthStencilTargets);
        m_depthStencil = nullptr;
        m_viewportRT = nullptr;
        m_displayRT = nullptr;
        m_screenshotRT = nullptr;
        m_renderTargetScale = 0.0f;
    }

    bool SceneRendererCore::NeedsRenderScaleResize() const
    {
        return m_displayRT && (m_renderTargetScale != Settings::Get<SceneSettings>().render_scale || m_hdr != SceneUsesHDR());
    }

    bool SceneRendererCore::SyncOptionalRenderTargets(bool hasRayTracingGeometry)
    {
        if (!SyncOptionalSceneRenderTargets(m_renderTargets, hasRayTracingGeometry))
            return false;

        // The visibility target switches shader variants: Resize must rebuild pipelines, not only descriptors.
        if (m_openpbr != SceneUsesOpenPBR())
        {
            SyncOpenPBREnvironment();
            m_openpbr = SceneUsesOpenPBR();
            m_colorFormatChanged = true;
        }

        return true;
    }

    void SceneRendererCore::BlitToSwapchain(CommandBuffer *cmd, Image *src, uint32_t imageIndex)
    {
        BlitSceneImageToSwapchain(cmd, src, imageIndex);
    }

    void SceneRendererCore::LoadSky(CommandBuffer *cmd)
    {
        LoadDefaultSceneSky(cmd, m_skyBox, m_ibl_brdf_lut);
    }

    void SceneRendererCore::ReloadSkyFromSettings()
    {
        RHII.WaitDeviceIdle();

        Queue *queue = RHII.GetMainQueue();
        CommandBuffer *cmd = queue->AcquireCommandBuffer();
        cmd->Begin();
        LoadConfiguredSceneSkybox(cmd, m_skyBox);
        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        cmd->Return();
    }

    // Turning OpenPBR on reloads the sky: its environment pyramid is built from the HDR file's own equirect, which the
    // legacy load drops once the cube exists.
    void SceneRendererCore::SyncOpenPBREnvironment()
    {
        if (!m_skyBox.IsLoaded())
            return;

        if (SceneUsesOpenPBR())
        {
            ReloadSkyFromSettings();
            return;
        }

        RHII.WaitDeviceIdle();
        m_skyBox.DestroyOpenPBREnvironment();
    }

    void SceneRendererCore::DestroySky()
    {
        DestroyDefaultSceneSky(m_skyBox, m_ibl_brdf_lut);
    }

    void SceneRendererCore::CreateRenderPassComponents(bool includeRayTracingPass, CommandBuffer *cmd)
    {
        (void)cmd;
        CreateSceneRenderGraphPassComponents(m_renderPassComponents, includeRayTracingPass);
    }

    void SceneRendererCore::DestroyRenderPassComponents()
    {
        // The worker creates pipelines on the device: let it stop after its current pass first.
        if (m_rtHitBuild.done.valid())
        {
            m_rtHitBuild.cancel->store(true);
            FinishRTHitBuild();
        }

        // No owners: the build is dropped and its PassInfos retired.
        if (m_moduleBuild.done.valid())
        {
            m_moduleBuild.cancel->store(true);
            FinishModuleBuild({});
        }

        DestroyInitializedSceneRenderGraphPassComponents(m_scenePasses, m_renderGraphPassInitialized);
        DestroyScenePerception();
    }

    void SceneRendererCore::ResizeRenderPassComponents(uint32_t width, uint32_t height, bool hasRayTracingGeometry)
    {
        pe::UpdateSceneRenderGraphPassStates(m_renderGraphPassEnabled, hasRayTracingGeometry);
        ResizeInitializedSceneRenderGraphPassComponents(m_scenePasses, m_renderGraphPassInitialized, width, height, m_colorFormatChanged);
        m_colorFormatChanged = false;
        InitEnabledRenderPassComponents(nullptr);
    }

    void SceneRendererCore::CacheGlobalComponents()
    {
        m_scenePasses = GetGlobalSceneRenderGraphPassComponents();
    }

    void SceneRendererCore::UpdateRenderGraphPassStates(bool hasRayTracingGeometry, CommandBuffer *cmd)
    {
        pe::UpdateSceneRenderGraphPassStates(m_renderGraphPassEnabled, hasRayTracingGeometry);

        // Setting toggles pass no command buffer; lazily initable passes must tolerate nullptr here. A pass
        // disabled for long enough is destroyed and initializes again when re-enabled.
        InitEnabledRenderPassComponents(cmd);
        ReleaseDisabledSceneRenderGraphPassComponents(m_scenePasses,
                                                      m_renderGraphPassInitialized,
                                                      m_renderGraphPassLastEnabledFrame);

        // Pipelines follow the modules the scene's material bytes hold (rebuilt by the byte packing, not per frame).
        static const std::vector<uint32_t> noModules;
        const Scene *scene = GetActiveScene();
        const std::vector<uint32_t> &modules = SceneUsesOpenPBR() && scene ? scene->GetOpenPBRModules() : noModules;

        if (m_renderGraphPassInitialized[static_cast<size_t>(SceneRenderGraphPassId::MaterialThin)])
            static_cast<MaterialThinPass *>(m_scenePasses.materialThin)->SyncModules(modules);

        if (m_renderGraphPassInitialized[static_cast<size_t>(SceneRenderGraphPassId::MaterialShade)])
            static_cast<MaterialShadePass *>(m_scenePasses.materialShade)->SyncModules(modules);

        if (Scene *active = GetActiveScene())
            active->SetOpenPBRForwardRaster(SceneUsesOpenPBR());

        if (m_renderGraphPassInitialized[static_cast<size_t>(SceneRenderGraphPassId::OpenPBRForward)])
        {
            const bool openpbr = SceneUsesOpenPBR() && scene;
            const std::vector<uint32_t> &forward = openpbr ? scene->GetOpenPBRForwardModules() : noModules;
            static_cast<OpenPBRForwardPass *>(m_scenePasses.openpbrForward)->SyncModules(forward);
        }

        SyncModulePipelines();
        SyncRTHitModules();
    }

    namespace
    {
        // Changes when any owner rebuilds or releases its module pipelines, or the owners change.
        uint64_t GenerationSum(const std::vector<std::pair<ModulePipelines *, const std::vector<uint32_t> *>> &owners)
        {
            uint64_t sum = 0;
            for (const auto &[owner, modules] : owners)
                sum += owner->GetModuleGeneration() + 1;

            return sum;
        }
    } // namespace

    SceneRendererCore::ModuleOwners SceneRendererCore::GetModuleOwners() const
    {
        const Scene *scene = GetActiveScene();
        if (!SceneUsesOpenPBR() || !scene)
            return {};

        auto initialized = [this](SceneRenderGraphPassId id)
        { return m_renderGraphPassInitialized[static_cast<size_t>(id)]; };

        ModuleOwners owners;
        if (initialized(SceneRenderGraphPassId::MaterialThin))
            owners.push_back({static_cast<MaterialThinPass *>(m_scenePasses.materialThin), &scene->GetOpenPBRModules()});
        if (initialized(SceneRenderGraphPassId::MaterialShade))
            owners.push_back({static_cast<MaterialShadePass *>(m_scenePasses.materialShade), &scene->GetOpenPBRModules()});
        if (initialized(SceneRenderGraphPassId::OpenPBRForward))
        {
            owners.push_back({static_cast<OpenPBRForwardPass *>(m_scenePasses.openpbrForward),
                              &scene->GetOpenPBRForwardModules()});
        }

        // The mask owners sync later in the frame (UpdateSceneRenderGraphPassComponents); those that did last frame want
        // the scene's mask modules.
        OpenPBRMask *masks[] = {
            m_scenePasses.depth ? &static_cast<DepthPass *>(m_scenePasses.depth)->GetMasks() : nullptr,
            m_scenePasses.depthLate ? &static_cast<DepthLatePass *>(m_scenePasses.depthLate)->GetMasks() : nullptr,
            m_scenePasses.shadow ? &static_cast<ShadowPass *>(m_scenePasses.shadow)->GetMasks() : nullptr,
        };
        for (OpenPBRMask *mask : masks)
        {
            if (mask && mask->IsActive())
                owners.push_back({mask, &scene->GetOpenPBRMaskModules()});
        }

        return owners;
    }

    // A new MaterialX module's raster pipelines (thin, shade, forward, masks) build on a worker: cold, they took about 5 s
    // per module on DX12 and 24 s on Vulkan, all on the main thread before (2026-10-25). One job runs at a time and builds
    // one module, so the shader cache never compiles a file twice at once and modules appear one by one. Every pass takes
    // a module in the same frame or none does: until then its opaque meshes keep the legacy G-buffer and lighting, and
    // its alpha-cut and forward meshes do not draw.
    void SceneRendererCore::SyncModulePipelines()
    {
        const ModuleOwners owners = GetModuleOwners();
        const bool buildDone = m_moduleBuild.done.valid() &&
                               m_moduleBuild.done.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        if (buildDone)
            FinishModuleBuild(owners);

        if (!m_moduleBuild.done.valid())
            StartModuleBuild(owners);
    }

    // A module is ready once every pass that wants it holds its pipelines. Failed and rejected modules render legacy
    // and are not waited for; the ray-traced hit groups build as one job, so they only report that they are building.
    OpenPBRBuildProgress SceneRendererCore::GetOpenPBRBuildProgress() const
    {
        std::map<uint32_t, bool> modules;
        for (const auto &[owner, wanted] : GetModuleOwners())
        {
            for (uint32_t module : *wanted)
            {
                if (m_failedModules.contains(module) || GetOpenPBRModule(module).rejected)
                    continue;

                auto [it, inserted] = modules.emplace(module, true);
                it->second = it->second && owner->HasModule(module);
            }
        }

        OpenPBRBuildProgress progress;
        progress.total = static_cast<uint32_t>(modules.size());
        progress.ready = static_cast<uint32_t>(std::count_if(modules.begin(), modules.end(), [](const auto &module)
                                                             { return module.second; }));
        progress.hitsBuilding = m_rtHitBuild.done.valid();
        return progress;
    }

    void SceneRendererCore::StartModuleBuild(const ModuleOwners &owners)
    {
        // The first module some pass lacks; a failed one waits until a pass rebuilds, and a rejected one (here or by
        // the RT hit build) until the scene's repack drops it.
        const uint64_t generations = GenerationSum(owners);
        std::optional<uint32_t> next;
        for (const auto &[owner, modules] : owners)
        {
            for (uint32_t module : *modules)
            {
                const auto failed = m_failedModules.find(module);
                const bool retry = failed == m_failedModules.end() || failed->second != generations;
                if (!next && !owner->HasModule(module) && retry && !GetOpenPBRModule(module).rejected)
                    next = module;
            }
        }

        if (!next)
            return;

        ModuleBuild build;
        build.module = *next;
        std::vector<std::function<void(PassInfo &)>> builders;
        for (const auto &[owner, modules] : owners)
        {
            if (owner->HasModule(build.module) || std::find(modules->begin(), modules->end(), build.module) == modules->end())
                continue;

            std::vector<std::function<void(PassInfo &)>> ownerBuilders = owner->ModuleBuilders(build.module);
            build.owners.push_back(owner);
            build.generations.push_back(owner->GetModuleGeneration());
            build.counts.push_back(ownerBuilders.size());
            builders.insert(builders.end(), ownerBuilders.begin(), ownerBuilders.end());
        }

        // Compute pipelines bind outside render passes, and graphics ones do under dynamic rendering (always on DX12):
        // then the frame's bind finds them in the cache.
        const bool nullPassKey = RHII.GetApi() == PE_GRAPHICS_API_DX12 ||
                                 (RHII.GetCaps().dynamicRendering && Settings::Get<SceneSettings>().dynamic_rendering);
        build.cancel = std::make_shared<std::atomic<bool>>(false);
        auto buildAll = [builders = std::move(builders), cancel = build.cancel, nullPassKey]
        {
            ModuleBuild::Result result;
            try
            {
                for (const auto &builder : builders)
                {
                    if (cancel->load())
                        break;

                    // Held before building, so the main thread retires a failed build's partial PassInfo too.
                    PassInfo &info = *result.infos.emplace_back(std::make_shared<PassInfo>());
                    builder(info);
                    if (info.pCompShader || nullPassKey)
                        CommandBuffer::GetPipeline(nullptr, info);
                }
            }
            catch (const std::exception &e)
            {
                result.error = e.what();
            }
            catch (...)
            {
                result.error = "unknown error";
            }

            return result;
        };
        build.done = ThreadPool::General.Enqueue(std::move(buildAll));
        PE_INFO("[OpenPBR] module pipelines queued: module %u, %zu passes", build.module, build.owners.size());
        m_moduleBuild = std::move(build);
    }

    void SceneRendererCore::FinishModuleBuild(const ModuleOwners &owners)
    {
        const ModuleBuild &build = m_moduleBuild;
        const ModuleBuild::Result &result = build.done.get();
        bool current = result.error.empty() &&
                       result.infos.size() == std::accumulate(build.counts.begin(), build.counts.end(), size_t(0));
        for (size_t i = 0; current && i < build.owners.size(); i++)
        {
            const auto wanted = std::find_if(owners.begin(), owners.end(), [&](const auto &owner)
                                             { return owner.first == build.owners[i]; });
            current = wanted != owners.end() &&
                      std::find(wanted->second->begin(), wanted->second->end(), build.module) != wanted->second->end() &&
                      build.owners[i]->GetModuleGeneration() == build.generations[i] &&
                      !build.owners[i]->HasModule(build.module);
        }

        if (current)
        {
            auto first = result.infos.begin();
            for (size_t i = 0; i < build.owners.size(); i++)
            {
                build.owners[i]->AdoptModule(build.module, {first, first + build.counts[i]});
                first += build.counts[i];
            }

            PE_INFO("[OpenPBR] module pipelines adopted: module %u", build.module);
        }
        else
        {
            // A stale build is queued again by this frame's sync; a failed one only after a pass rebuilds.
            for (const auto &info : result.infos)
                RetireModulePassInfo(info);

            if (result.error.empty())
            {
                PE_INFO("[OpenPBR] module pipelines dropped: module %u (stale)", build.module);
            }
            else
            {
                // Warns once and sends the module's meshes to legacy shading; the scene's next repack drops it.
                m_failedModules[build.module] = GenerationSum(owners);
                RejectOpenPBRModule(GetOpenPBRModule(build.module), result.error);

                // Every "queued" line ends in one "adopted" or "dropped": test harnesses wait on the count.
                PE_INFO("[OpenPBR] module pipelines dropped: module %u (compile failed)", build.module);
            }
        }

        m_moduleBuild = {};
    }

    namespace
    {
        // TLAS hit-group offsets are 1 + an index into the scene's list: only a prefix of every pass's list selects the
        // same group in all of them.
        std::vector<uint32_t> ReadyHitPrefix(const std::vector<RayTracingPass *> &passes)
        {
            if (passes.empty())
                return {};

            std::vector<uint32_t> prefix = passes.front()->GetHitModules();
            for (const RayTracingPass *pass : passes)
            {
                const std::vector<uint32_t> &ready = pass->GetHitModules();
                const auto diverged = std::mismatch(prefix.begin(), prefix.end(), ready.begin(), ready.end()).first;
                prefix.erase(diverged, prefix.end());
            }

            return prefix;
        }
    } // namespace

    // RT hit groups follow the scene's modules while OpenPBR is effective and its sky (bound by module hits) exists.
    // A new list builds the RT passes' pipelines on a worker: cold slangc and driver compiles took 90 s for four modules
    // on DX12 and 410 s on Vulkan (2026-10-21), all of it on the main thread before. Instances select module groups only
    // within the list prefix every pass has ready, so they keep the legacy hit until then. Without modules the groups go
    // at once (legacy-only pipelines are cheap, and module groups bind the sky). The first RT pass to trace refits the
    // instances before any ray reads them.
    void SceneRendererCore::SyncRTHitModules()
    {
        static const std::vector<uint32_t> noModules;
        Scene *scene = GetActiveScene();
        const bool openpbrSky = m_skyBox.GetOpenPBRRadiance() && m_skyBox.GetOpenPBRIrradiance();
        const std::vector<uint32_t> &modules = SceneUsesOpenPBR() && openpbrSky && scene ? scene->GetOpenPBRModules()
                                                                                         : noModules;

        const std::pair<SceneRenderGraphPassId, IRenderPassComponent *> rayTracers[] = {
            {SceneRenderGraphPassId::RayTracing, m_scenePasses.rayTracing},
            {SceneRenderGraphPassId::GlobalIllumination, m_scenePasses.globalIllumination},
            {SceneRenderGraphPassId::ScreenProbes, m_scenePasses.screenProbes},
            {SceneRenderGraphPassId::Reflections, m_scenePasses.reflections},
        };
        std::vector<RayTracingPass *> passes;
        for (const auto &[id, pass] : rayTracers)
        {
            if (pass && m_renderGraphPassInitialized[static_cast<size_t>(id)])
                passes.push_back(static_cast<RayTracingPass *>(pass));
        }

        // Before adopting: a build still in flight is stale once its passes drop their groups.
        if (modules.empty())
        {
            for (RayTracingPass *pass : passes)
                pass->SyncHitModules(noModules);
        }

        const bool buildDone = m_rtHitBuild.done.valid() &&
                               m_rtHitBuild.done.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        if (buildDone)
            FinishRTHitBuild();

        if (!modules.empty() && !m_rtHitBuild.done.valid())
            StartRTHitBuild(passes, modules);

        if (scene)
            scene->SetRTHitModules(ReadyHitPrefix(passes));
    }

    void SceneRendererCore::StartRTHitBuild(const std::vector<RayTracingPass *> &passes,
                                            const std::vector<uint32_t> &modules)
    {
        RTHitBuild build;
        std::vector<std::function<std::shared_ptr<PassInfo>()>> prepared;
        for (RayTracingPass *pass : passes)
        {
            if (pass->GetHitModules() == modules)
                continue;

            build.passes.push_back(pass);
            build.generations.push_back(pass->GetHitGeneration());
            prepared.push_back(pass->PrepareHitBuild(modules));
        }

        if (prepared.empty())
            return;

        // One worker builds the passes in turn, so the any-hit they share compiles once.
        build.modules = modules;
        build.cancel = std::make_shared<std::atomic<bool>>(false);
        auto buildAll = [prepared = std::move(prepared), cancel = build.cancel]
        {
            std::vector<std::shared_ptr<PassInfo>> infos;
            for (const auto &buildPass : prepared)
                infos.push_back(cancel->load() ? nullptr : buildPass());

            return infos;
        };
        build.done = ThreadPool::General.Enqueue(std::move(buildAll));
        PE_INFO("[RT] hit group build started: %zu MaterialX modules, %zu passes",
                modules.size(),
                build.passes.size());
        m_rtHitBuild = std::move(build);
    }

    void SceneRendererCore::FinishRTHitBuild()
    {
        const RTHitBuild &build = m_rtHitBuild;
        const std::vector<std::shared_ptr<PassInfo>> &infos = build.done.get();
        size_t adopted = 0;
        for (size_t i = 0; i < infos.size(); i++)
        {
            if (infos[i] && build.passes[i]->AdoptHitBuild(infos[i], build.modules, build.generations[i]))
                adopted++;
        }

        PE_INFO("[RT] hit group build finished: %zu of %zu passes adopted", adopted, infos.size());
        m_rtHitBuild = {};
    }

    bool SceneRendererCore::IsPassEnabled(SceneRenderGraphPassId passId) const
    {
        return m_renderGraphPassEnabled[static_cast<size_t>(passId)];
    }

    void SceneRendererCore::SetPassDeferred(SceneRenderGraphPassId passId, bool deferred)
    {
        m_renderGraphPassDeferred[static_cast<size_t>(passId)] = deferred;
    }

    bool SceneRendererCore::IsPassDeferred(SceneRenderGraphPassId passId) const
    {
        return m_renderGraphPassDeferred[static_cast<size_t>(passId)];
    }

    void SceneRendererCore::AddScenePassesToRenderGraph()
    {
        m_renderPassOrdersRevision = Settings::Get<SceneSettings>().render_pass_orders_revision;
        AddSceneRenderGraphPasses(m_renderGraph,
                                  m_scenePasses,
                                  [this](SceneRenderGraphPassId passId)
                                  { return IsPassEnabled(passId) && !IsPassDeferred(passId); });

        // Deferred passes run in their normal graph order, just later in the frame: the player
        // executes this graph after compositing its UI so display-space post covers the whole
        // image. Own graph, so RenderGraph still issues each pass's declared input barriers.
        m_deferredRenderGraph.Clear();
        AddSceneRenderGraphPasses(m_deferredRenderGraph,
                                  m_scenePasses,
                                  [this](SceneRenderGraphPassId passId)
                                  { return IsPassEnabled(passId) && IsPassDeferred(passId); });
        // Its images come from the main graph, so its reads and loads have no writer here: nothing to report.
        m_deferredRenderGraph.Compile(false);

        // Script-registered passes (render_graph.add_pass): inserted by order among
        // the built-in passes; RenderGraph::Compile sorts them into place. The graph
        // holds only the pass name - the callback (and its Lua refs) stays in the
        // registry, so script reloads can release Lua state safely; a stale graph
        // entry resolves to nothing and no-ops until the next rebuild.
        const std::vector<ScriptRenderPass> &scriptPasses = GetScriptRenderPasses();
        auto scriptDefaultOrder = [&](size_t i)
        {
            return scriptPasses[i].order == kScriptRenderPassUnplacedOrder ? kScriptRenderPassUnplacedOrder + static_cast<uint32_t>(i) * 10u
                                                                           : scriptPasses[i].order;
        };
        for (size_t i = 0; i < scriptPasses.size(); i++)
        {
            std::string passName = scriptPasses[i].name;
            const auto passId = static_cast<RenderGraph::PassID>(kScriptRenderPassIdBase + i);
            std::function<void(RGBuilder &)> declare;
            if (!scriptPasses[i].reads.empty() || !scriptPasses[i].writes.empty())
            {
                declare = [reads = scriptPasses[i].reads, writes = scriptPasses[i].writes](RGBuilder &builder)
                {
                    SceneRendererHost *host = GetActiveSceneRendererHost();
                    if (!host)
                        return;
                    const auto image = [host](const std::string &name)
                    {
                        Image *found = host->GetRenderTarget(name);
                        return found ? found : host->GetDepthStencilTarget(name);
                    };
                    for (const std::string &name : reads)
                        builder.Read(image(name));
                    for (const std::string &name : writes)
                    {
                        if (Image *target = image(name); target && PeFormatHasDepth(target->GetFormat()))
                            builder.OutputDepth(target, true);
                        else
                            builder.OutputColor(target, true);
                    }
                };
            }
            m_renderGraph.AddPass(passId, GetSceneRenderGraphPassOrder(passId, passName, scriptDefaultOrder(i)), passName, []()
                                  { return true; }, [passName](CommandBuffer *cmd)
                                  {
                                      const ScriptRenderPass *pass = FindScriptRenderPass(passName);
                                      if (pass && pass->execute)
                                          pass->execute(cmd); }, std::move(declare));
        }

        // Saved sort numbers that break the frame (a read before its writer, a column clash) render with the default
        // order instead; the saved numbers are kept. Build time only, and only when the pipeline overrides orders.
        if (!Settings::Get<SceneSettings>().render_pass_orders.empty())
        {
            std::unordered_map<RenderGraph::PassID, uint32_t> defaults;
            std::unordered_map<RenderGraph::PassID, uint32_t> saved;
            for (const RenderGraph::Pass &pass : m_renderGraph.GetPasses())
            {
                saved[pass.id] = pass.order;
                defaults[pass.id] = pass.id >= kScriptRenderPassIdBase
                                        ? scriptDefaultOrder(pass.id - kScriptRenderPassIdBase)
                                        : DefaultSceneRenderGraphPassOrder(static_cast<SceneRenderGraphPassId>(pass.id));
            }
            RenderGraph atDefaults = m_renderGraph;
            atDefaults.SetPassOrders(defaults);
            if (const auto added = atDefaults.NewProblems(saved); !added.empty())
            {
                PE_WARN("[Pipeline] Saved pass order is not valid (%s); rendering with the default order", added.front().text.c_str());
                m_renderGraph.SetPassOrders(defaults);
                m_deferredRenderGraph.SetPassOrders(defaults);
                m_deferredRenderGraph.Compile(false);
            }
        }
    }

    void SceneRendererCore::UpdateRenderPassComponents()
    {
        UpdateSceneRenderGraphPassComponents(m_renderPassComponents,
                                             m_scenePasses,
                                             [this](SceneRenderGraphPassId passId)
                                             { return IsPassEnabled(passId); });
    }

    void SceneRendererCore::InitEnabledRenderPassComponents(CommandBuffer *cmd)
    {
        InitEnabledSceneRenderGraphPassComponents(
            m_scenePasses,
            [this](SceneRenderGraphPassId passId)
            { return IsPassEnabled(passId); },
            m_renderGraphPassInitialized,
            m_renderGraphPassLastEnabledFrame,
            cmd);
    }

    void SceneRendererCore::SetRenderPassScene(Scene &scene)
    {
        m_scene = &scene;
        SetSceneRenderGraphPassScene(m_scenePasses, scene);
    }

    void SceneRendererCore::ExecuteRenderGraph(CommandBuffer *cmd)
    {
        if (m_scene)
            m_scene->UploadDynamicUniforms(cmd);

        m_renderGraph.Execute(cmd);
        ++m_renderedFrames;
    }

    void SceneRendererCore::ExecuteDeferredRenderGraph(CommandBuffer *cmd)
    {
        m_deferredRenderGraph.Execute(cmd);
    }

    void SceneRendererCore::PollShaders(std::optional<size_t> hash)
    {
        ReloadRenderPassShaders(m_renderPassComponents, hash);
        InvalidateMaterialLayouts(); // a shader file changed: material layouts re-hash their sources
    }

    void SceneRendererCore::CreateFrameResources(uint32_t imageCount,
                                                 std::string_view acquireNamePrefix,
                                                 std::string_view submitNamePrefix,
                                                 PeBarrierSync acquireStageFlags,
                                                 PeBarrierSync submitStageFlags)
    {
        m_cmds.assign(imageCount, nullptr);
        CreateSceneFrameSemaphores(m_acquireSemaphores,
                                   m_submitSemaphores,
                                   imageCount,
                                   acquireNamePrefix,
                                   submitNamePrefix,
                                   acquireStageFlags,
                                   submitStageFlags);
    }

    void SceneRendererCore::DestroyFrameResources()
    {
        WaitSceneFrameCommands(m_cmds);
        m_cmds.clear();

        DestroySceneFrameSemaphores(m_acquireSemaphores);
        DestroySceneFrameSemaphores(m_submitSemaphores);
    }

    void SceneRendererCore::WaitPreviousFrameCommands()
    {
        WaitPreviousSceneFrameCommand(m_cmds);
    }

    void SceneRendererCore::WaitAllFrameCommands()
    {
        WaitSceneFrameCommandsAndCleanup(m_cmds);
    }

    void SceneRendererCore::SubmitAndPresent(const SceneFrameRecordCallback &recordFrame,
                                             const SceneFrameScreenshotCallback &saveScreenshot,
                                             const SceneFrameSubmitLabels &labels)
    {
        SubmitAndPresentSceneFrame(m_cmds, m_acquireSemaphores, m_submitSemaphores, recordFrame, m_screenshotPending, saveScreenshot, labels);
    }

    void SceneRendererCore::SetScreenshotPath(std::string path)
    {
        m_screenshotPath = std::move(path);
    }

    void SceneRendererCore::QueueScreenshotReadback(CommandBuffer *cmd, Image *sourceImage, const std::string &stagingBufferName)
    {
        m_screenshotPending =
            QueueSceneScreenshotReadback(cmd, sourceImage, m_screenshotRT, m_screenshotBuffer, m_screenshotRowPitch, stagingBufferName);
    }

    bool SceneRendererCore::SaveScreenshot(std::string *savedPath)
    {
        return SaveSceneScreenshot(m_screenshotBuffer, m_screenshotRT, m_screenshotPath, m_screenshotRowPitch, savedPath);
    }

    void SceneRendererCore::DestroyScreenshotBuffer()
    {
        Buffer::Destroy(m_screenshotBuffer);
        m_screenshotRowPitch = 0;
        m_screenshotPending = false;
    }
} // namespace pe
