#pragma once
#include "ScreenProbePass.h"

namespace pe
{
    // Lumen-style reflections: a GGX ray per pixel for surfaces up to 0.4 rough, reused across
    // neighbouring pixels and averaged over time; rougher surfaces take the screen probes' radiance. Replaces the
    // cubemap specular of the opaque lighting pass (docs/superpowers/specs/2026-10-10-gi-reflections-design.md).
    class ReflectionPass : public RayTracingPass
    {
    public:
        ReflectionPass();
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
        Image *GetSpecular() const { return m_specular; }

    private:
        void CreateImages();
        void DestroyImages();

        ScreenProbeUniforms m_data{};
        std::vector<Buffer *> m_lrUniforms;
        std::vector<Buffer *> m_boundGIUniforms; // per frame: GI may create its uniforms after this pass
        std::vector<Image *> m_boundProbeImages; // the screen probe images the resolve set was built with
        std::shared_ptr<PassInfo> m_resolvePass;
        Image *m_depth = nullptr;
        Image *m_normal = nullptr;
        Image *m_velocity = nullptr; // G-buffer velocity, null when the scene has none
        Image *m_material = nullptr;
        Image *m_specular = nullptr; // per pixel: prefiltered reflected radiance, a = frames averaged (0: none)
        Image *m_history = nullptr;
        mat4 m_viewProj{};
        vec4 m_camPos{};
        mat4 m_pendingViewProj{};
        vec4 m_pendingCamPos{};
        Scene *m_historyScene = nullptr;
        uint32_t m_historyGeneration = 0;
        uint64_t m_lastUpdate = 0;
    };
} // namespace pe
