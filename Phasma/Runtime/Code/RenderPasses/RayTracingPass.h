#pragma once

#include "Scene/OpenPBRShapes.h"

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
        uint32_t linear_color;    // decode authored colours at hits
        uint32_t env_samples;     // MaterialX hits: openpbr_env_samples (RTCommon cb_envSamples)
        uint32_t frame;           // RTCommon cb_frame: the frame counter sun-disk samples step with
        float sunTanHalfAngle;    // tan of half sun_angular_diameter (RTCommon TraceSunShadow; 0 = hard)
        uint32_t physicalFalloff; // physical_point_falloff (RTCommon cb_physicalFalloff)
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

        // A hit build's outcome: the pipeline, or the hit group (index into OpenPBRHitSet::groups) that failed.
        struct HitBuildResult
        {
            std::shared_ptr<PassInfo> info;
            size_t failedGroup = SIZE_MAX;
            std::string error;
        };

        // One hit group per MaterialX shape group after the legacy group (OpenPBRHitSet). SyncHitModules rebuilds at
        // once; PrepareHitBuild returns a build that touches no pass state, for a worker thread, and AdoptHitBuild swaps
        // its result in unless the pass was rebuilt or released since it was prepared.
        void SyncHitModules(const OpenPBRHitSet &set);
        std::function<HitBuildResult()> PrepareHitBuild(const OpenPBRHitSet &set) const;
        bool AdoptHitBuild(const std::shared_ptr<PassInfo> &info, const OpenPBRHitSet &set, uint64_t generation);
        // The same hit groups for another module list (a module whose shape they already hold, or one gone): only the
        // slot table changes. False when the groups differ, which needs a build.
        bool RetargetHitSet(const OpenPBRHitSet &set);
        const OpenPBRHitSet &GetHitSet() const { return m_hitSet; }
        uint64_t GetHitGeneration() const { return m_hitGeneration; }

        // Main thread. A failed group with several shapes builds them alone next time; a lone shape's modules are rejected.
        static void HandleHitGroupFailure(const OpenPBRHitSet &set, size_t group, const std::string &error);

    protected:
        void BuildTracePipeline();
        void UpdateShapeSlots();

        bool m_probeTracing = false;
        bool m_screenProbes = false; // probe tracing for ScreenProbePass
        bool m_reflections = false;  // probe tracing for ReflectionPass

        // Ray tracing pipelines bind descriptor sets (VulkanPipelineImpl), written in place: a rebind during the session
        // marks every frame stale and each frame rewrites only its own sets in its Update, before recording, when its
        // previous use has finished (as for its uniforms). Rewriting them all at once lost the device while frames still
        // read them (a slider drag on a glass sphere's transmission in Full RT, 2026-11-06).
        void RebindDescriptorSets();
        void RebindStaleFrame();
        bool UpdatesFrame(uint32_t frame) const { return m_descriptorFrame == UINT32_MAX || frame == m_descriptorFrame; }
        uint32_t m_descriptorFrame = UINT32_MAX; // the one frame UpdateDescriptorSets writes; UINT32_MAX: all (idle GPU)
        uint32_t m_staleDescriptorFrames = 0;
        uint32_t m_traceHeight = 0; // rays traced per column; 0: the display image's height
        Buffer *m_giFallbackUniform = nullptr;
        Scene *m_scene = nullptr;
        Image *m_display = nullptr;
        Image *m_rtDepth = nullptr;                // primary-hit depth for RTDepthResolvePass (full RT mode)
        AccelerationStructure *m_tlas = nullptr;   // used to check if tlas is updated
        std::vector<Buffer *> m_boundLightStorage; // per frame: a grown light buffer replaces the old one
        std::vector<Buffer *> m_uniforms;
        uint64_t m_lastGeometryVersion = 0;
        OpenPBRHitSet m_hitSet;                 // the MaterialX hit groups after the legacy group, in SBT order
        Buffer *m_shapeSlots = nullptr;         // MaterialHitHead's pe_shapeSlots for m_hitSet
        uint64_t m_hitGeneration = 0;           // bumped by every rebuild and release: older prepared builds are stale
        Buffer *m_boundMaterialBytes = nullptr; // the hit groups' bindings follow a repacked buffer or a rebuilt sky
        Image *m_boundEnvRadiance = nullptr;
    };
} // namespace pe
