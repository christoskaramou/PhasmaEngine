#pragma once

namespace pe
{
    class Image;
    class CommandBuffer;

    // Color grading pass (lift / gamma / gain + saturation + contrast).
    // Runs in LDR display space after tonemapping and bloom have updated the display RT.
    class ColorGradingPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override;
        void UpdateDescriptorSets() override;
        void Update() override {};
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;

    private:
        friend class Renderer;

        Image *m_frameImage;
        Image *m_displayRT;
    };
} // namespace pe
