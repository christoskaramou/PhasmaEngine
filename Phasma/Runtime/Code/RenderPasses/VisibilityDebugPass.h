#pragma once

namespace pe
{
    class CommandBuffer;
    class Image;

    // openpbr_debug (desktop OpenPBR): reconstructs the G-buffer's albedo and depth from the visibility payload with
    // quad-exact compute evaluation and writes per-pixel error codes to visibility_debug (decoded by pool tests).
    class VisibilityDebugPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override {}
        void UpdateDescriptorSets() override { m_lastGeometryVersion = ~0ull; }
        void Update() override;
        void DeclareInputs(RGBuilder &builder) override;
        void DeclareOutputs(RGBuilder &builder) override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;

    private:
        Image *m_visibility = nullptr;
        Image *m_depth = nullptr;
        Image *m_albedo = nullptr;
        Image *m_debug = nullptr;
        uint64_t m_lastGeometryVersion = ~0ull;
        bool m_bound = false; // this frame's descriptors were written: ExecutePass may dispatch
    };
} // namespace pe
