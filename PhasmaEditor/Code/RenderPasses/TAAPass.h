#pragma once

namespace pe
{
    class Image;

    class TAAPass : public IRenderPassComponent
    {
    public:
        TAAPass() = default;
        ~TAAPass() = default;

        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override;
        void UpdateDescriptorSets() override;
        void Update() override;
        void Draw(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;

        void GenerateJitter();
        const vec2 &GetProjectionJitter() const { return m_projectionJitter; }
        Image *GetResolvedImage() { return m_taaResolved; }

    private:
        Image *m_viewportRT;
        Image *m_displayRT;
        Image *m_depthStencil;
        Image *m_velocityRT;

        Image *m_historyImage;
        Image *m_taaResolved;

        vec2 m_jitter;
        vec2 m_projectionJitter;
        int m_jitterPhaseCount;
        int m_jitterIndex;
        bool m_casSharpeningEnabled{false};
    };
} // namespace pe
