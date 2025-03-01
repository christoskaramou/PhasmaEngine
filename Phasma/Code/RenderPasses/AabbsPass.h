#pragma once

namespace pe
{
    class Image;
    class CommandBuffer;
    class Camera;
    class Geometry;

    class AabbsPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override {};
        void UpdateDescriptorSets() override {};
        void Update(Camera *camera) override {};
        void Draw(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;
        void SetGeometry(Geometry *geometry) { m_geometry = geometry; }

    private:
        friend class Scene;
        friend class Renderer;

        Image *m_viewportRT;
        Image *m_depthRT;
        Geometry *m_geometry;
    };
}
