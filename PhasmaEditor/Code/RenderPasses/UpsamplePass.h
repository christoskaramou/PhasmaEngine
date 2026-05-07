#pragma once

namespace pe
{
    class CommandBuffer;
    class Image;
    class RGBuilder;

    class UpsamplePass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override;
        void UpdateDescriptorSets() override;
        void Update() override {}
        void DeclareInputs(RGBuilder &builder) override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override {}

    private:
        Image *m_viewportRT = nullptr;
        Image *m_displayRT = nullptr;
    };
} // namespace pe
