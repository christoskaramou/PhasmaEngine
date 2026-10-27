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
        Image *GetFarRadianceHistory() const { return m_farRadianceHistory; }
        static constexpr uint32_t kUniformBytes = 224; // ProbeUniforms; zeroed fallbacks bind at least this much

    private:
        // A camera-following volume is kCascades nested 8x8x8 cascades, each twice the spacing of the last (Lumen's
        // radiance cache clipmaps); a volume fitted to the scene is one. ProbeVolume.hlsl: GI_MAX_CASCADES.
        static constexpr uint32_t kCascades = 4;
        static constexpr uint32_t kCascadeProbes = 512;
        struct ProbeUniforms
        {
            vec4 originSpacing[kCascades];        // per cascade: xyz origin, w its smallest spacing
            vec4 historyOriginSpacing[kCascades]; // last frame's
            vec4 spacing[kCascades];              // per cascade: xyz spacing; w = 1 when the volume follows the camera
            uint32_t gridSize = 8;
            uint32_t rayCount = 256;
            uint32_t frame = 0;
            uint32_t reset = 1;
            uint32_t cascadeCount = 1;
            float intensity = 1.f;
            float hysteresis = 0.999f;
            uint32_t tracedVisibility = 1; // ProbeVolume.hlsl GI_TRACED_VISIBILITY: 0 keeps the distance moments alone
        };
        static_assert(sizeof(ProbeUniforms) == kUniformBytes);
        ProbeUniforms m_probeData{};
        std::vector<Buffer *> m_probeUniforms;
        Image *m_irradiance = nullptr;
        Image *m_distance = nullptr;
        Image *m_irradianceHistory = nullptr;
        Image *m_distanceHistory = nullptr;
        std::shared_ptr<PassInfo> m_updatePass;
        std::shared_ptr<PassInfo> m_samplePass;
        std::shared_ptr<PassInfo> m_radiancePass;
        Image *m_rayDirections = nullptr;
        Image *m_radiance = nullptr;
        Image *m_radianceHistory = nullptr;
        Image *m_farRays = nullptr; // per ray: radiance beyond the probe's interpolation footprint (screen probe hand-off)
        Image *m_farRadiance = nullptr;
        Image *m_farRadianceHistory = nullptr;
        Scene *m_historyScene = nullptr;
        uint32_t m_historyGeneration = 0;
        vec4 m_historyOriginSpacing[kCascades]{};
        vec4 m_historySpacing[kCascades]{};
        uint32_t m_historyCascades = 0;
        size_t m_historyInputs = 0;
        uint64_t m_lastUpdate = 0;
        Scene *m_boundsScene = nullptr;
        uint64_t m_boundsVersion = 0;
        uint32_t m_boundsGeneration = 0;
        AABB m_sceneBounds{};
        bool m_sceneBoundsValid = false;
    };
} // namespace pe
