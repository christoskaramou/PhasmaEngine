#pragma once

#include "API/Pipeline.h"

namespace pe
{
    class Scene;
    class CommandBuffer;
    class Image;
    class Buffer;
    class RGBuilder;

    class CullingPass : public IRenderPassComponent
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
        std::vector<PassInfo *> GetPassInfos() noexcept override { return {m_passInfo.get(), m_sortPassInfo.get()}; }

        void SetScene(Scene *scene) { m_scene = scene; }

    private:
        void AcquirePyramid();
        // Temporal voxel Hi-Z is active when occlusion culling is enabled, a voxel pyramid exists, and
        // the scene actually has live arena voxels (else the cull stays frustum-only).
        bool VoxelOcclusionActive() const;

        Scene *m_scene = nullptr;
        std::unique_ptr<PassInfo> m_sortPassInfo = std::make_unique<PassInfo>();

        // Temporal voxel occlusion: last frame's voxel-inclusive Hi-Z (built by VoxelHiZPyramidPass)
        // + a UBO carrying the PREVIOUS frame's view-proj (the camera that produced that depth, so the
        // AABB->screen projection matches the pyramid). m_prevViewProj is captured each frame for use
        // by the next.
        Image *m_voxelHiZ = nullptr;
        std::vector<Buffer *> m_occlusionUniforms;
        mat4 m_prevViewProj = mat4(1.0f);
        bool m_havePrevViewProj = false;
    };
} // namespace pe
