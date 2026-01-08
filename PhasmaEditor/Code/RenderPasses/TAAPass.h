#pragma once

#include "ECS/Component.h"

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

    private:
        Image *m_viewportRT;
        Image *m_displayRT;
        Image *m_depthStencil;
        Image *m_velocityRT;

        Image *m_historyImage;

        vec2 m_jitter;
        vec2 m_projectionJitter;
        int m_jitterPhaseCount;
        int m_jitterIndex;
    };
} // namespace pe
