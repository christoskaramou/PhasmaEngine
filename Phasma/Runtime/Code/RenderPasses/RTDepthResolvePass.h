#pragma once

namespace pe
{
    class Image;
    class CommandBuffer;
    class Scene;

    // Stamps the ray-traced primary-hit depth (written by RayTracingPass into "rtDepth") into the
    // depth buffer. Enabled only in full RT mode, where no raster pass writes depth but overlays
    // (grid, lines, selection outline) still depth-test against it.
    class RTDepthResolvePass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override;
        void UpdateDescriptorSets() override;
        void Update() override;
        void DeclareInputs(RGBuilder &builder) override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;
        void SetScene(Scene *scene) { m_scene = scene; }

    private:
        Image *m_rtDepth = nullptr;
        Image *m_depthStencil = nullptr;
        Scene *m_scene = nullptr;
    };
} // namespace pe
