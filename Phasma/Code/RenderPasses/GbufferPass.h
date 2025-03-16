#pragma once

namespace pe
{
    class Image;
    class Buffer;
    class Geometry;
    class CommandBuffer;

    struct PushConstants_GBuffer
    {
        uint32_t jointsCount;
        vec2 projJitter;
        vec2 prevProjJitter;
        uint32_t transparentPass;
    };
    
    struct Primitive_Constants
    {
        float alphaCut;
        uint32_t meshDataOffset;
        uint32_t primitiveDataOffset;
        uint32_t primitiveImageIndex[5];
    };

    class GbufferOpaquePass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override {};
        void UpdateDescriptorSets() override {};
        void Update() override {};
        void Draw(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;

        void SetGeometry(Geometry *geometry) { m_geometry = geometry; }
        void ClearRenderTargets(CommandBuffer *cmd);
        void ClearDepthStencil(CommandBuffer *cmd);

    private:
        friend class Geometry;
        friend class Scene;

        void PassBarriers(CommandBuffer *cmd);

        Image *m_ibl_brdf_lut;
        Image *m_normalRT;
        Image *m_albedoRT;
        Image *m_srmRT;
        Image *m_velocityRT;
        Image *m_emissiveRT;
        Image *m_viewportRT;
        Image *m_transparencyRT;
        Image *m_depthStencilRT;
        Buffer *m_constants;

        Geometry *m_geometry;
    };

    class GbufferTransparentPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override {};
        void UpdateDescriptorSets() override {};
        void Update() override {};
        void Draw(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;

        void SetGeometry(Geometry *geometry) { m_geometry = geometry; }
        void ClearRenderTargets(CommandBuffer *cmd);
        void ClearDepthStencil(CommandBuffer *cmd);

    private:
        friend class Geometry;
        friend class Scene;

        void PassBarriers(CommandBuffer *cmd);

        Image *m_ibl_brdf_lut;
        Image *m_normalRT;
        Image *m_albedoRT;
        Image *m_srmRT;
        Image *m_velocityRT;
        Image *m_emissiveRT;
        Image *m_viewportRT;
        Image *m_transparencyRT;
        Image *m_depthStencilRT;
        Buffer *m_constants;

        Geometry *m_geometry;
    };
}
