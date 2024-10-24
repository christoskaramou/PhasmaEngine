#pragma once

namespace pe
{
    class Image;
    class Buffer;
    class Geometry;
    class CommandBuffer;

    class GbufferOpaquePass : public IRenderPassComponent
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
        void ClearRenderTargets(CommandBuffer *cmd);
        void ClearDepthStencil(CommandBuffer *cmd);

    private:
        friend class Geometry;
        friend class Renderer;
        friend class Scene;

        void PassBarriers(CommandBuffer *cmd);

        Buffer *uniform;
        Image *ibl_brdf_lut;
        Image *normalRT;
        Image *albedoRT;
        Image *srmRT;
        Image *velocityRT;
        Image *emissiveRT;
        Image *viewportRT;
        Image *transparencyRT;
        Image *depthStencilRT;

        Geometry *m_geometry;
    };

    class GbufferTransparentPass : public IRenderPassComponent
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
        void ClearRenderTargets(CommandBuffer *cmd);
        void ClearDepthStencil(CommandBuffer *cmd);

    private:
        friend class Geometry;
        friend class Renderer;
        friend class Scene;

        void PassBarriers(CommandBuffer *cmd);

        Image *ibl_brdf_lut;
        Image *normalRT;
        Image *albedoRT;
        Image *srmRT;
        Image *velocityRT;
        Image *emissiveRT;
        Image *viewportRT;
        Image *transparencyRT;
        Image *depthStencilRT;

        Geometry *m_geometry;
    };
}
