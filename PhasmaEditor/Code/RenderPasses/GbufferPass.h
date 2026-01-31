#pragma once

namespace pe
{
    class Image;
    class Buffer;
    class CommandBuffer;
    class Scene;

    struct PushConstants_GBuffer
    {
        uint32_t jointsCount;
        float pad0;
        vec2 projJitter;
        vec2 prevProjJitter;
        uint32_t passType;
        float pad1;
    };

    struct Mesh_Constants
    {
        float alphaCut;
        uint32_t meshDataOffset;
        uint32_t textureMask;
        uint32_t meshImageIndex[5];
    };

    class GbufferOpaquePass : public IRenderPassComponent
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
        void ClearRenderTargets(CommandBuffer *cmd);
        void ClearDepthStencil(CommandBuffer *cmd);

    private:
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

        Scene *m_scene = nullptr;
        uint64_t m_lastGeometryVersion = 0;
    };

    class GbufferTransparentPass : public IRenderPassComponent
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
        void ClearRenderTargets(CommandBuffer *cmd);
        void ClearDepthStencil(CommandBuffer *cmd);

    private:
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

        Scene *m_scene = nullptr;
        uint64_t m_lastGeometryVersion = 0;
    };
} // namespace pe
