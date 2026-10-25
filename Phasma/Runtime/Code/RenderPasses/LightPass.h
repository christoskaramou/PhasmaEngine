#pragma once

#include "RenderPasses/ModulePipelines.h"

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
        uint32_t shadows = 1;
        uint32_t use_Disney_PBR = 1;
        uint32_t orthographicCamera = 0;
        float skyboxTanHalfFovY = 1.0f;
        float physical_point_falloff = 0.0f;
        uint32_t forward_plus = 1;
        float fog_density = 0.0f; // 0 = fog off (enable bool resolved CPU-side)
        float fog_start = 0.0f;
        uint32_t linear_color = 0;  // albedo target holds sRGB-encoded albedo
        uint32_t screen_probes = 0; // opaque pass reads ScreenProbePass irradiance
        uint32_t reflections = 0;   // opaque pass reads ReflectionPass radiance
    };

    class Image;
    class ImageView;
    class Buffer;
    class CommandBuffer;
    class Sampler;
    struct Define;

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
        friend class MaterialShadePass; // reads m_uniforms (written only in Init)

        std::vector<Buffer *> m_uniforms;
        Image *m_ibl_brdf_lut;
        Image *m_normalRT;
        Image *m_albedoRT;
        Image *m_srmRT;
        Image *m_emissiveRT;
        Image *m_viewportRT;
        Image *m_depthStencilRT;
        Image *m_ssaoRT;
        Image *m_transparencyRT;
        std::vector<Buffer *> m_shadowFallbackUniforms;
        std::vector<Buffer *> m_forwardPlusFallbackTileData;
        std::vector<Buffer *> m_forwardPlusFallbackLightIndices;
        Image *m_shadowFallbackTexture = nullptr;
        Sampler *m_shadowFallbackSampler = nullptr;
        std::vector<ImageView *> m_shadowFallbackViews;
        bool m_boundShadowsAvailable = false;
        bool m_boundForwardPlusEnabled = true;
        std::vector<Buffer *> m_boundLightStorage; // per frame: a grown light buffer replaces the old one
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
        Image *m_emissiveRT;
        Image *m_viewportRT;
        Image *m_depthStencilRT;
        Image *m_ssaoRT;
        Image *m_transparencyRT;
        std::vector<Buffer *> m_shadowFallbackUniforms;
        std::vector<Buffer *> m_forwardPlusFallbackTileData;
        std::vector<Buffer *> m_forwardPlusFallbackLightIndices;
        Image *m_shadowFallbackTexture = nullptr;
        Sampler *m_shadowFallbackSampler = nullptr;
        std::vector<ImageView *> m_shadowFallbackViews;
        bool m_boundShadowsAvailable = false;
        bool m_boundForwardPlusEnabled = true;
        std::vector<Buffer *> m_boundLightStorage; // per frame: a grown light buffer replaces the old one
        LightPassUBO m_ubo;
    };

    // Material pass 2 (desktop OpenPBR): MaterialX's generated shading for visibility pixels of MaterialX materials,
    // over LightOpaque's result. Shares LightOpaquePass's bindings (Lighting.hlsl sets 0-3), uniform and push
    // constants; lives in LightPass.cpp for its helpers.
    class MaterialShadePass : public IRenderPassComponent, public ModulePipelines
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override {}
        void UpdateDescriptorSets() override;
        void Update() override;
        void DeclareInputs(RGBuilder &builder) override;
        void DeclareOutputs(RGBuilder &builder) override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;
        void SyncModules(const std::vector<uint32_t> &modules);
        std::vector<PassInfo *> GetPassInfos() noexcept override;
        std::vector<std::function<void(PassInfo &)>> ModuleBuilders(uint32_t module) const override;
        void AdoptModule(uint32_t module, const std::vector<std::shared_ptr<PassInfo>> &infos) override;
        bool HasModule(uint32_t module) const override { return m_modulePassInfos.contains(module); }

    protected:
        virtual std::function<void(PassInfo &)> PipelineBuilder(uint32_t module) const;
        virtual std::vector<Define> Defines() const;
        virtual bool TargetsReady() const;
        void BindSceneDescriptors(PassInfo &info, uint32_t frame);
        void BindEnvironmentDescriptors(PassInfo &info, uint32_t frame);
        void BindFrame(uint32_t frame);

        std::map<uint32_t, std::shared_ptr<PassInfo>> m_modulePassInfos;
        Image *m_viewportRT = nullptr;
        Image *m_visibility = nullptr;
        Image *m_albedoRT = nullptr; // bound to Ssao when the SSAO target is absent, as LightOpaquePass does
        std::string m_builtDefines;
        std::vector<bool> m_boundFrames;
        // Its own shadow fallbacks: LightOpaquePass rewrites its copies in its Update, which runs in parallel.
        std::vector<Buffer *> m_shadowFallbackUniforms;
        Image *m_shadowFallbackTexture = nullptr;
        Sampler *m_shadowFallbackSampler = nullptr;
        std::vector<ImageView *> m_shadowFallbackViews;
    };

    // OpenPBR forward pass (Phase 6): MaterialX alpha-blend and transmission meshes, shaded by their generated code
    // in a pixel shader (GBufferVS.hlsl + MaterialForward*.slang) and blended over the lit scene far to near, after
    // LightTransparent and Hybrid's ray-traced transparents (they write the viewport and see through these meshes).
    // Material pass 2's bindings, module pipelines and lighting; only the raster pipeline and draws differ.
    class OpenPBRForwardPass : public MaterialShadePass
    {
    public:
        void Init() override;
        void DeclareInputs(RGBuilder &builder) override;
        void DeclareOutputs(RGBuilder &builder) override {}
        void ExecutePass(CommandBuffer *cmd) override;
        void Destroy() override;

        // The depth of the surfaces that hide what is behind them, when this pass wrote it in the render graph
        // execution under way (it is disabled in scenes without forward meshes); else nullptr.
        Image *GetTransparentDepth() const;

    protected:
        std::function<void(PassInfo &)> PipelineBuilder(uint32_t module) const override;
        std::vector<Define> Defines() const override;
        bool TargetsReady() const override;

    private:
        std::vector<Attachment> m_attachments;
        Image *m_depthStencilRT = nullptr;
        Image *m_velocityRT = nullptr;
        Image *m_transparentDepthRT = nullptr;
        uint64_t m_transparentDepthFrame = UINT64_MAX; // SceneRendererHost::GetRenderedFrames() when last written
    };
} // namespace pe
