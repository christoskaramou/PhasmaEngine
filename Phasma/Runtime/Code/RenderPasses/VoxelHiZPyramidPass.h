#pragma once

namespace pe
{
    class CommandBuffer;
    class Image;
    class Descriptor;
    class PassInfo;
    class RGBuilder;
    class Scene;

    // Temporal voxel occlusion (Phase 2 voxel debt): builds a Hi-Z depth pyramid ("voxelHiZ", R32F,
    // full mip chain) from the scene depth AFTER the G-buffer (priority 310), so it includes voxel
    // depth (voxels write depth in the G-buffer; they are absent from the depth prepass that feeds the
    // regular two-phase pyramid). Next frame the frustum CullingPass samples this pyramid (VOXEL_HIZ)
    // to cull voxel chunks occluded by last frame's voxels — a 1-frame-late temporal test. Mirrors
    // DepthPyramidPass; the only differences are the source ordering (post-G-buffer) and the dedicated
    // persistent target. Skips entirely when the scene has no arena voxels.
    class VoxelHiZPyramidPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override;
        void UpdateDescriptorSets() override;
        void Update() override {}
        void DeclareInputs(RGBuilder &builder) override;
        void DeclareOutputs(RGBuilder &builder) override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;
        std::vector<PassInfo *> GetPassInfos() noexcept override;

        void SetScene(Scene *scene) { m_scene = scene; }
        Image *GetPyramid() const { return m_pyramid; }

    private:
        void AcquireTargets();
        void DestroyDescriptors();

        Scene *m_scene = nullptr;
        Image *m_depth = nullptr;
        Image *m_pyramid = nullptr;
        std::shared_ptr<PassInfo> m_copyPassInfo; // scene depth (sampled) -> pyramid mip 0
        Descriptor *m_copySet = nullptr;          // {sampled depth, storage mip 0}
        std::vector<Descriptor *> m_reduceSets;   // {storage mip i, storage mip i+1}, i = 0..N-2
    };
} // namespace pe
