#pragma once

namespace pe
{
    class Scene;
    class Image;
    class Buffer; // Forward declaration
    class AccelerationStructure;

    struct RayTracingPassUBO
    {
        mat4 invViewProj;
        mat4 invView;
        mat4 invProj;
        vec4 camPos;
        uint32_t shadows;
        uint32_t use_Disney_PBR;
        float ibl_intensity;
        uint32_t IBL;
        uint32_t renderMode; // 0=Raster, 1=Hybrid, 2=RayTracing
        uint32_t orthographicCamera;
        uint32_t linear_color; // decode authored colours at hits
        uint32_t env_samples;  // MaterialX hits: openpbr_env_samples (RTCommon cb_envSamples)
    };

    class RayTracingPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override;
        void UpdateDescriptorSets() override;
        void Update() override;
        void DeclareInputs(RGBuilder &builder) override;
        void DeclareOutputs(RGBuilder &builder) override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;

        void SetScene(Scene *scene) { m_scene = scene; }

        // One hit group per MaterialX module after the legacy group, in m_hitModules order (OpenPBR). SyncHitModules
        // rebuilds at once; PrepareHitBuild returns a build that touches no pass state, for a worker thread, and
        // AdoptHitBuild swaps its result in unless the pass was rebuilt or released since it was prepared.
        void SyncHitModules(const std::vector<uint32_t> &modules);
        std::function<std::shared_ptr<PassInfo>()> PrepareHitBuild(const std::vector<uint32_t> &modules) const;
        bool AdoptHitBuild(const std::shared_ptr<PassInfo> &info,
                           const std::vector<uint32_t> &modules,
                           uint64_t generation);
        const std::vector<uint32_t> &GetHitModules() const { return m_hitModules; }
        uint64_t GetHitGeneration() const { return m_hitGeneration; }

    protected:
        void BuildTracePipeline();

        bool m_probeTracing = false;
        bool m_screenProbes = false; // probe tracing for ScreenProbePass
        bool m_reflections = false;  // probe tracing for ReflectionPass
        uint32_t m_traceHeight = 0;  // rays traced per column; 0: the display image's height
        Buffer *m_giFallbackUniform = nullptr;
        Scene *m_scene = nullptr;
        Image *m_display = nullptr;
        Image *m_rtDepth = nullptr;                // primary-hit depth for RTDepthResolvePass (full RT mode)
        AccelerationStructure *m_tlas = nullptr;   // used to check if tlas is updated
        std::vector<Buffer *> m_boundLightStorage; // per frame: a grown light buffer replaces the old one
        std::vector<Buffer *> m_uniforms;
        uint64_t m_lastGeometryVersion = 0;
        std::vector<uint32_t> m_hitModules;     // MaterialX modules with a hit group, in SBT order after the legacy group
        uint64_t m_hitGeneration = 0;           // bumped by every rebuild and release: older prepared builds are stale
        Buffer *m_boundMaterialBytes = nullptr; // the hit groups' bindings follow a repacked buffer or a rebuilt sky
        Image *m_boundEnvRadiance = nullptr;
    };
} // namespace pe
