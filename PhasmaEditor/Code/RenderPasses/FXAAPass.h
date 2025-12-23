#pragma once

namespace pe
{
    class Image;
    class CommandBuffer;

    class FXAAPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override;
        void UpdateDescriptorSets() override;
        void Update() override {};
        void Draw(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;

    private:
        Image *m_frameImage;
        Image *m_viewportRT;
    };
} // namespace pe
