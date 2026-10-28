#pragma once

namespace pe
{
    class Image;
    class CommandBuffer;
    class Scene;

    // Editor gizmo: GI world probes (draw_gi_probes) and screen probes (draw_screen_probes) as spheres lit by their
    // own irradiance (Shaders/Utilities/ProbeGizmos.hlsl), drawn into the display after tonemapping as GridPass is.
    class ProbeGizmosPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override {};
        void UpdateDescriptorSets() override {};
        void Update() override {};
        void DeclareInputs(RGBuilder &builder) override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override {};
        void SetScene(Scene *scene) { m_scene = scene; }

    private:
        Image *m_displayRT = nullptr;
        Image *m_depthRT = nullptr;
        Scene *m_scene = nullptr;
    };
} // namespace pe
