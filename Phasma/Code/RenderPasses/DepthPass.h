#pragma once

namespace pe
{
    class Image;
    class Geometry;
    class CommandBuffer;

    struct PushConstants_DepthPass
    {
        uint32_t jointsCount;
    };

    class DepthPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override;
        void UpdateDescriptorSets() override;
        void Update() override {};
        void Draw(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;

        void SetGeometry(Geometry *geometry) { m_geometry = geometry; }
        void ClearDepthStencil(CommandBuffer *cmd);

    private:
        friend class Geometry;
        friend class Scene;
        friend class Renderer;

        Image *m_depthStencil;
        Geometry *m_geometry;
    };
}
