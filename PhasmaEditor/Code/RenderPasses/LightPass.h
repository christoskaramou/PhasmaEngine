#pragma once

namespace pe
{
    struct LightPassUBO
    {
        mat4 invViewProj = mat4(1.0f);
        uint32_t ssao = 1;
        uint32_t ssr = 0;
        uint32_t tonemapping = 0;
        uint32_t fsr2 = 0;
        uint32_t IBL = 1;
        float IBL_intensity = 0.75f;
        float lights_intensity = 7.0f;
        float lights_range = 7.0f;
        uint32_t shadows = 1;
    };

    class Image;
    class Buffer;
    class CommandBuffer;

    class LightOpaquePass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override;
        void UpdateDescriptorSets() override;
        void Update() override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;

    private:
        void PassBarriers(CommandBuffer *cmd);

        std::vector<Buffer *> m_uniforms;
        Image *m_ibl_brdf_lut;
        Image *m_normalRT;
        Image *m_albedoRT;
        Image *m_srmRT;
        Image *m_velocityRT;
        Image *m_emissiveRT;
        Image *m_viewportRT;
        Image *m_depthStencilRT;
        Image *m_ssaoRT;
        Image *m_transparencyRT;
        LightPassUBO m_ubo;
    };

    class LightTransparentPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override;
        void UpdateDescriptorSets() override;
        void Update() override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;

    private:
        void PassBarriers(CommandBuffer *cmd);

        std::vector<Buffer *> m_uniforms;
        Image *m_ibl_brdf_lut;
        Image *m_normalRT;
        Image *m_albedoRT;
        Image *m_srmRT;
        Image *m_velocityRT;
        Image *m_emissiveRT;
        Image *m_viewportRT;
        Image *m_depthStencilRT;
        Image *m_ssaoRT;
        Image *m_transparencyRT;
        LightPassUBO m_ubo;
    };
} // namespace pe
