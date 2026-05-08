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
        void DeclareInputs(RGBuilder &builder) override;
        void DeclareOutputs(RGBuilder &builder) override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;

        void GenerateJitter();
        const vec2 &GetProjectionJitter() const { return m_projectionJitter; }
        Image *GetResolvedImage() { return m_taaResolved; }
        void RequestHistoryReset() { m_resetHistory = true; }

    private:
        Image *m_viewportRT = nullptr;
        Image *m_displayRT = nullptr;
        Image *m_depthStencil = nullptr;
        Image *m_velocityRT = nullptr;

        Image *m_historyImage = nullptr;
        Image *m_taaResolved = nullptr;

        vec2 m_jitter;
        vec2 m_projectionJitter;
        int m_jitterPhaseCount = 0;
        int m_jitterIndex = 0;
        bool m_casSharpeningEnabled{false};
        bool m_resetHistory{false};
    };
} // namespace pe
