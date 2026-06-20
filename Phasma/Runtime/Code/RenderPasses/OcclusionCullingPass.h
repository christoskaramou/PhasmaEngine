#pragma once

#include "API/Pipeline.h"

namespace pe
{
    class Scene;
    class CommandBuffer;
    class Image;
    class Buffer;
    class RGBuilder;

    // Two-phase temporal Hi-Z occlusion, phase 2 (priority 260, between the depth pyramid build
    // at 250 and the late depth pass at 270). Tests every frustum-visible object against THIS
    // frame's Hi-Z pyramid (built from set A's depth), rewrites the persistent per-draw visibility
    // flag for next frame's phase 1, and emits only the newly-disoccluded opaque objects — "set B"
    // — which DepthLatePass@270 and the G-buffer@300 then draw. Occluded objects land in neither
    // set A nor B, so they are drawn 0x (the depth prepass is culled, not just the G-buffer).
    //
    // Compiles CullingCS with {HIZ_OCCLUSION, PHASE2}: needs the pyramid + OcclusionUBO (binding
    // 13/14) and the visibility buffer (binding 15). No transparents/selected, no sort. The
    // component is always registered; only enabled when GlobalSettings::occlusion_culling is on
    // (default off), so the shipping path is unchanged when the feature is disabled.
    class OcclusionCullingPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override;
        void UpdateDescriptorSets() override {}
        void Update() override;
        void DeclareInputs(RGBuilder &builder) override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;
        std::vector<PassInfo *> GetPassInfos() noexcept override { return {m_passInfo.get()}; }

        void SetScene(Scene *scene) { m_scene = scene; }

    private:
        void AcquirePyramid();

        Scene *m_scene = nullptr;
        Image *m_pyramid = nullptr;
        std::vector<Buffer *> m_occlusionUniforms; // per-frame OcclusionUBO
    };
} // namespace pe
