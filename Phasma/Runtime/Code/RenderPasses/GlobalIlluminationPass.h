#pragma once
#include "RayTracingPass.h"

namespace pe
{
    class GlobalIlluminationPass : public RayTracingPass
    {
    public:
        GlobalIlluminationPass();
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
        std::vector<PassInfo *> GetPassInfos() noexcept override;
        Buffer *GetProbeUniform(uint32_t frame) const { return frame < m_probeUniforms.size() ? m_probeUniforms[frame] : nullptr; }
        Image *GetIrradianceHistory() const { return m_irradianceHistory; }
        Image *GetDistanceHistory() const { return m_distanceHistory; }

    private:
        struct ProbeUniforms
        {
            vec4 originSpacing;
            uint32_t gridSize = 8;
            uint32_t rayCount = 64;
            uint32_t frame = 0;
            uint32_t reset = 1;
            float maxDistance = 32.f;
            float intensity = 1.f;
            float normalBias = 0.2f;
            float hysteresis = 0.999f;
            vec4 historyOriginSpacing{};
            vec4 spacing{}; // xyz per-axis probe spacing; w = 1 when the volume follows the camera and fades its edges
        };
        static_assert(sizeof(ProbeUniforms) == 80);
        ProbeUniforms m_probeData{};
        std::vector<Buffer *> m_probeUniforms;
        Image *m_irradiance = nullptr;
        Image *m_distance = nullptr;
        Image *m_irradianceHistory = nullptr;
        Image *m_distanceHistory = nullptr;
        std::shared_ptr<PassInfo> m_updatePass;
        Scene *m_historyScene = nullptr;
        uint32_t m_historyGeneration = 0;
        vec4 m_historyOriginSpacing{};
        vec4 m_historySpacing{};
        size_t m_historyInputs = 0;
        uint64_t m_lastUpdate = 0;
        Scene *m_boundsScene = nullptr;
        uint64_t m_boundsVersion = 0;
        uint32_t m_boundsGeneration = 0;
        AABB m_sceneBounds{};
        bool m_sceneBoundsValid = false;
    };
} // namespace pe
