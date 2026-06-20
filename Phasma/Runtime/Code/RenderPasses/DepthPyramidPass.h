#pragma once

namespace pe
{
    class CommandBuffer;
    class Image;
    class Descriptor;
    class PassInfo;
    class RGBuilder;

    // Phase 2a of the occlusion-culling plan: builds a Hi-Z depth pyramid (R32F, full
    // mip chain) from the scene depth buffer by min-reducing each level (reverse-Z =>
    // min is the conservative far depth). Runs between the depth prepass (200) and the
    // G-buffer (300). The pyramid is sampled by OcclusionCullingPass (275); the consumer's
    // ReadCompute emits the GENERAL->SHADER_READ_ONLY barrier (see DeclareOutputs).
    //
    // The whole pyramid stays in one GENERAL / UNORDERED_ACCESS state for the entire
    // build (previous level read as a storage image, next level written as a storage
    // image, ordered by a UAV/memory barrier) so it never needs per-subresource layout
    // transitions, which the DX12 backend cannot express.
    class DepthPyramidPass : public IRenderPassComponent
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

    private:
        void AcquireTargets();
        void DestroyDescriptors();

        Image *m_depth = nullptr;
        Image *m_pyramid = nullptr;
        std::shared_ptr<PassInfo> m_copyPassInfo; // scene depth (sampled) -> pyramid mip 0
        Descriptor *m_copySet = nullptr;          // {sampled depth, storage mip 0}
        std::vector<Descriptor *> m_reduceSets;   // {storage mip i, storage mip i+1}, i = 0..N-2
    };
} // namespace pe
