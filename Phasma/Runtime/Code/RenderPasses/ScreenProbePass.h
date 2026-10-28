#pragma once
#include "RayTracingPass.h"

namespace pe
{
    // ScreenProbeCommon.hlsl's ScreenProbeUBO, shared by the screen probe and reflection compute passes.
    struct ScreenProbeUniforms
    {
        mat4 invViewProj;
        mat4 prevViewProj; // last frame's, without TAA jitter
        mat4 viewProj;     // this frame's, without TAA jitter
        vec4 camPos;
        vec4 prevCamPos;
        uint32_t tilesX = 0;
        uint32_t tilesY = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t frame = 0;
        uint32_t reset = 1;
        uint32_t hasVelocity = 0;  // the velocity target is bound (else camera motion only)
        uint32_t adaptiveRows = 0; // probe rows after the tilesY uniform rows, for adaptive probes
    };
    static_assert(sizeof(ScreenProbeUniforms) == 256);

    // Lumen-style screen probes: a probe per 16x16 tile traces short guided rays, falls back to the world radiance
    // cache, and the result is filtered and resolved per pixel for the opaque lighting pass
    // (docs/superpowers/specs/2026-10-10-screen-probes-design.md).
    class ScreenProbePass : public RayTracingPass
    {
    public:
        ScreenProbePass();
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
        Image *GetIrradiance() const { return m_irradiance; }
        Image *GetFilteredSH() const { return m_shFiltered; }
        Image *GetProbePositions() const { return m_positions; }
        Image *GetProbeNormals() const { return m_normals; }
        Image *GetGeometry() const { return m_geometry; }
        Image *GetAdaptiveMap() const { return m_adaptiveMap; }
        const ScreenProbeUniforms &GetData() const { return m_data; } // frame: the last frame Update ran

    private:
        void CreateImages();
        void DestroyImages();

        ScreenProbeUniforms m_data{};
        std::vector<Buffer *> m_spUniforms;
        Buffer *m_adaptiveCount = nullptr;       // adaptive slots taken this frame
        std::vector<Buffer *> m_boundGIUniforms; // per frame: GI may create its uniforms after this pass
        std::shared_ptr<PassInfo> m_placePass;
        std::shared_ptr<PassInfo> m_adaptivePass;
        std::shared_ptr<PassInfo> m_integratePass;
        std::shared_ptr<PassInfo> m_filterPass;
        std::shared_ptr<PassInfo> m_resolvePass;
        Image *m_depth = nullptr;
        Image *m_normal = nullptr;
        Image *m_velocity = nullptr;  // G-buffer velocity, null when the scene has none
        Image *m_positions = nullptr; // per probe (uniform rows, then adaptive): world position, view distance (0 = no probe)
        Image *m_normals = nullptr;
        Image *m_positionsHistory = nullptr;
        Image *m_normalsHistory = nullptr;
        Image *m_directions = nullptr;  // per ray (m_display holds the traced radiance): direction, pdf
        Image *m_binRadiance = nullptr; // per probe 8x8 direction bins: mean radiance, for next frame's ray guiding
        Image *m_binRadianceHistory = nullptr;
        Image *m_binPrior = nullptr; // per bin: the history radiance the rays were aimed by, for bins no ray reaches
        Image *m_shFiltered = nullptr;
        Image *m_irradiance = nullptr; // per pixel: irradiance, coverage
        Image *m_irradianceHistory = nullptr;
        Image *m_geometry = nullptr; // per pixel: packed normal, view distance, frames averaged
        Image *m_geometryHistory = nullptr;
        Image *m_adaptiveMap = nullptr; // per 8x8 sub-tile: adaptive probe slot + 1, 0 = none
        mat4 m_viewProj{};
        vec4 m_camPos{};
        mat4 m_pendingViewProj{};
        vec4 m_pendingCamPos{};
        Scene *m_historyScene = nullptr;
        uint32_t m_historyGeneration = 0;
        uint64_t m_lastUpdate = 0;
    };
} // namespace pe
