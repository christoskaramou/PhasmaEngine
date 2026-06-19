#pragma once

namespace pe
{
    class Image;
    class CommandBuffer;
    class Scene;

    // Draws RenderType::Lines meshes (Primitives::CreatePolyline) as hardware line
    // strips into the lit viewport: screen-constant 1px width on both backends,
    // visible from any angle and distance. Runs after LightTransparent and before
    // TAA, so the lines get antialiased. Line meshes are skipped by the indirect
    // raster path and drawn directly here, AabbsPass-style.
    class LinesPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override {};
        void UpdateDescriptorSets() override {};
        void Update() override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;
        void SetScene(Scene *scene) { m_scene = scene; }

    private:
        Image *m_viewportRT;
        Image *m_depthRT;
        Scene *m_scene = nullptr;
    };
} // namespace pe
