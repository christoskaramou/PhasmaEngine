#pragma once

namespace pe
{
    class Image;
    class CommandBuffer;
    // Tonemap pass
    class TonemapPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override;
        void UpdateDescriptorSets() override;
        void Update() override;
        void DeclareInputs(RGBuilder &builder) override;
        void DeclareOutputs(RGBuilder &builder) override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;

        Image *GetResolvedImage() const { return m_resolvedImage; }

    private:
        friend class Renderer;

        Image *m_sceneColor = nullptr;
        Image *m_resolvedImage = nullptr;
        Image *m_displayRT = nullptr;
    };
} // namespace pe
