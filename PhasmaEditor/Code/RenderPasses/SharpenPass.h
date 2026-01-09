#pragma once

namespace pe
{
    class SharpenPass : public IRenderPassComponent
    {
    public:
        SharpenPass();
        ~SharpenPass();

        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override;
        void UpdateDescriptorSets() override;
        void Update() override;
        void Draw(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;
    };
} // namespace pe
