#pragma once

namespace pe
{
    struct LightPassUBO
    {
        mat4 invViewProj = mat4(1.0f);
        vec4 camPos = vec4(0.0f);
        vec4 camForward = vec4(0.0f);
        uint32_t ssao = 1;
        uint32_t ssr = 0;
        uint32_t IBL = 1;
        float IBL_intensity = 0.75f;
        float lights_intensity = 7.0f;
        uint32_t shadows = 1;
        uint32_t use_Disney_PBR = 1;
        uint32_t orthographicCamera = 0;
        float skyboxTanHalfFovY = 1.0f;
        float physical_point_falloff = 0.0f;
        float pad1 = 0.0f;
        float pad2 = 0.0f;
    };

    class Image;
    class ImageView;
    class Buffer;
    class CommandBuffer;
    class Sampler;

    class LightOpaquePass : public IRenderPassComponent
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

    private:
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
        std::vector<Buffer *> m_shadowFallbackUniforms;
        Image *m_shadowFallbackTexture = nullptr;
        Sampler *m_shadowFallbackSampler = nullptr;
        std::vector<ImageView *> m_shadowFallbackViews;
        bool m_boundShadowsAvailable = false;
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
        void DeclareInputs(RGBuilder &builder) override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;

    private:
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
        std::vector<Buffer *> m_shadowFallbackUniforms;
        Image *m_shadowFallbackTexture = nullptr;
        Sampler *m_shadowFallbackSampler = nullptr;
        std::vector<ImageView *> m_shadowFallbackViews;
        bool m_boundShadowsAvailable = false;
        LightPassUBO m_ubo;
    };
} // namespace pe
