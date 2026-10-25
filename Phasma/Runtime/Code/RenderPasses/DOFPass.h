#pragma once

namespace pe
{
    class Image;
    class CommandBuffer;
    class PassInfo;

    // Thin-lens depth of field: a compute prepass packs the frame with inverse depth and finds each tile's largest
    // blur, a second spreads it over the blur cap, and a fullscreen gather blurs the display target (DOF.hlsl).
    class DOFPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override;
        void UpdateDescriptorSets() override;
        void Update() override {};
        void DeclareInputs(RGBuilder &builder) override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;
        std::vector<PassInfo *> GetPassInfos() noexcept override;

    private:
        void DestroyImages();

        std::shared_ptr<PassInfo> m_prepassInfo;
        std::shared_ptr<PassInfo> m_dilateInfo;

        Image *m_frameImage = nullptr;
        Image *m_displayRT = nullptr;
        Image *m_depth = nullptr;
        Image *m_transparentDepth = nullptr;
        Image *m_packed = nullptr;
        Image *m_tiles = nullptr;
        Image *m_reach = nullptr;
    };
} // namespace pe
