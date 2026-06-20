#pragma once

#include "API/Pipeline.h"

namespace pe
{
    class Scene;
    class CommandBuffer;

    // Two-phase temporal Hi-Z occlusion, phase 1 (priority 180, between Shadow@100 and the depth
    // prepass@200). A frustum cull restricted to objects that were visible last frame (the persistent
    // per-draw visibility flag) — its opaque output is "set A", which the depth prepass draws to seed
    // this frame's depth buffer, and thus the Hi-Z pyramid the phase-2 cull tests against. Always
    // registered; only enabled when GlobalSettings::occlusion_culling is on (see SceneRenderGraph),
    // so the shipping path is byte-identical when the feature is off.
    //
    // Compiles CullingCS with PHASE1 (no HIZ_OCCLUSION): no pyramid, no transparents/selected, no
    // sort. Writes only the opaque/alpha-cut SS+DS draw sets and reads (never writes) the visibility
    // buffer. All buffer hazards are handled inside Scene::DispatchCullingPhase.
    class CullPhase1Pass : public IRenderPassComponent
    {
    public:
        void Init() override {}
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override {}
        void UpdateDescriptorSets() override {}
        void Update() override {}
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override {}
        void Destroy() override;
        std::vector<PassInfo *> GetPassInfos() noexcept override { return {m_passInfo.get()}; }

        void SetScene(Scene *scene) { m_scene = scene; }

    private:
        Scene *m_scene = nullptr;
    };
} // namespace pe
