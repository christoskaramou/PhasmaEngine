// Port of webgpu-samples/alpha-to-coverage-emulator to PhasmaWebGPU's
// W3C WebGPU C API (over Vulkan). The upstream sample demonstrates a user-
// space emulator of alpha-to-coverage by emitting @builtin(sample_mask) from
// the pixel shader; this port hard-codes the GUI knobs to the upstream
// initial preset:
//
//   scene              = 'Foliage'
//   mode               = 'native'          (hardware alpha-to-coverage)
//   mode2              = 'none'            (both passes use the same mode)
//   emulatedDevice     = 'Apple M1 Pro'
//   sampleCount        = 4
//   sizeLog2           = 8                 (render target size = 256)
//   showResolvedColor  = true
//   featheringWidthPx  = 1
//   Foliage_animate    = true
//
// Rendering matches upstream frame-for-frame: (1) large-dot MSAA pass
// rendering Foliage with the Foliage-scene pipeline, (2) small-dot MSAA pass
// rendering the same (because mode2 == 'none'), (3) a visualization pass
// that shows each sample site as a colored circle plus a resolved-background
// reveal.
//
// All three scenes (CrossingGradients, Leaf, Foliage) are present in the
// source tree; all ten alpha-to-coverage emulator algorithms ship as HLSL
// in Shaders/emulators.inc.hlsl. Only the Foliage scene is compiled into
// the live pipeline because that is the default preset.

#include "../Common/SampleApp.h"
#include "../Common/SampleBase.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"

#include "Base/Log.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    constexpr float kPi = 3.14159265358979323846f;

    // Upstream kInitConfig defaults.
    constexpr uint32_t kSampleCount = 4;
    constexpr uint32_t kSizeLog2 = 8; // size = 256
    constexpr uint32_t kRenderTargetSize = 1u << kSizeLog2;
    constexpr float kFeatheringWidthPx = 1.0f;
    constexpr bool kShowResolvedColor = true;

    // Foliage scene rendering constants from upstream.
    constexpr uint32_t kFoliageInstanceCount = 1000;
    constexpr uint32_t kFoliageDiskSegmentCount = 32;
    constexpr uint32_t kFoliageVertexCount = kFoliageDiskSegmentCount * 3;

    // RGBA8Unorm is the shared MSAA/resolve color format. Upstream hard-codes
    // it; in Vulkan the same format is supported as RenderAttachment+TextureBinding.
    constexpr WGPUTextureFormat kColorFormat = WGPUTextureFormat_RGBA8Unorm;
    constexpr WGPUTextureFormat kDepthFormat = WGPUTextureFormat_Depth24Plus;

    // ---- scene uniform layout ---------------------------------------------
    // Matches FoliageCommon.wgsl:
    //   struct Uniforms { viewProj: mat4x4f, featheringWidthPx: f32 }
    struct FoliageUniforms
    {
        glm::mat4 viewProj;
        float featheringWidthPx;
        float pad0;
        float pad1;
        float pad2;
    };
    static_assert(sizeof(FoliageUniforms) == 80, "FoliageUniforms must be 80 bytes");

    // Upstream FoliageCommon.ts getViewProjMatrix(distLog, rotRad).
    glm::mat4 GetFoliageViewProj(float cameraDistanceLog, float cameraRotationRad)
    {
        const float aspect = 1.0f; // render target is square
        glm::mat4 projection = glm::perspectiveRH_ZO((2.0f * kPi) / 5.0f, aspect, 1.0f, 2000.0f);
        // Engine-side Y-flip for Vulkan NDC (we sample this via the
        // ShowMultisampleTexture pipeline, which does not flip, so the content
        // must be authored in Y-up clip space).
        projection[1][1] *= -1.0f;

        const float dist = std::pow(1.5f, cameraDistanceLog);
        const glm::vec3 up{0.0f, 1.0f, 0.0f};
        const glm::vec3 look{0.0f, 0.5f, 0.0f};
        glm::vec3 eye{0.0f, 3.0f * dist, 8.0f * dist};

        glm::mat4 rotation = glm::translate(glm::mat4(1.0f), look);
        rotation = glm::rotate(rotation, cameraRotationRad, glm::vec3(0.0f, 1.0f, 0.0f));
        eye = glm::vec3(rotation * glm::vec4(eye, 1.0f));

        glm::mat4 view = glm::lookAtRH(eye, look, up);
        return projection * view;
    }

    class AlphaToCoverageEmulatorSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            m_showVS = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "showMultisampleTexture.vert.hlsl").c_str(), "show_vs");
            m_showPS = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "showMultisampleTexture.pixel.hlsl").c_str(), "show_ps");
            m_foliageVS = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "foliage.vert.hlsl").c_str(), "foliage_vs");
            m_foliagePS = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "foliage.pixel.hlsl").c_str(), "foliage_ps");
            if (!m_showVS || !m_showPS || !m_foliageVS || !m_foliagePS)
                return false;

            if (!CreateFoliageResources(ctx))
                return false;

            if (!CreateShowResources(ctx))
                return false;

            if (!AllocateRenderTargets(ctx))
                return false;

            return true;
        }

        void Resize(pwgpu::test::SampleContext &, uint32_t, uint32_t) override
        {
            // Render targets are sized to kRenderTargetSize independently of
            // the swapchain (upstream caps size at the canvas resolution; for
            // this port the fixed 256x256 target always fits our 1280x720
            // swapchain).
        }

        void Update(pwgpu::test::SampleContext &ctx) override
        {
            // Upstream: Foliage_animate => cameraRotation advances 360deg
            // over 60 seconds.
            m_cameraRotationDeg = std::fmod(
                m_cameraRotationDeg + float(ctx.timing.deltaSeconds) * (360.0f / 60.0f),
                360.0f);

            FoliageUniforms u{};
            u.viewProj = GetFoliageViewProj(0.0f, glm::radians(m_cameraRotationDeg));
            u.featheringWidthPx = kFeatheringWidthPx;
            wgpuQueueWriteBuffer(ctx.queue, m_foliageUniformBuffer, 0, &u, sizeof(u));
        }

        bool Execute(pwgpu::test::SampleContext &, pwgpu::test::SampleFrame &frame) override
        {
            // 1. Optionally clear the resolve texture to black when it would
            //    otherwise be left undefined. With kShowResolvedColor = true
            //    this branch is skipped, matching upstream.
            if (!kShowResolvedColor)
            {
                WGPURenderPassColorAttachment ca{};
                ca.view = m_resolveView;
                ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
                ca.loadOp = WGPULoadOp_Clear;
                ca.storeOp = WGPUStoreOp_Store;
                ca.clearValue = {0.0, 0.0, 0.0, 1.0};

                WGPURenderPassDescriptor rp{};
                rp.label = {"clear_resolve_pass", WGPU_STRLEN};
                rp.colorAttachmentCount = 1;
                rp.colorAttachments = &ca;
                WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(frame.encoder, &rp);
                wgpuRenderPassEncoderEnd(pass);
                wgpuRenderPassEncoderRelease(pass);
            }

            // 2. Large-dot MSAA pass: renders the Foliage scene into
            //    largeDotMSTexture, optionally resolving to resolveTexture.
            {
                WGPURenderPassColorAttachment ca{};
                ca.view = m_largeDotView;
                ca.resolveTarget = kShowResolvedColor ? m_resolveView : nullptr;
                ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
                ca.loadOp = WGPULoadOp_Clear;
                ca.storeOp = WGPUStoreOp_Store;
                ca.clearValue = {0.0, 0.0, 0.0, 1.0};

                WGPURenderPassDepthStencilAttachment da{};
                da.view = m_depthView;
                da.depthClearValue = 1.0f;
                da.depthLoadOp = WGPULoadOp_Clear;
                da.depthStoreOp = WGPUStoreOp_Discard;
                da.stencilLoadOp = WGPULoadOp_Undefined;
                da.stencilStoreOp = WGPUStoreOp_Undefined;

                WGPURenderPassDescriptor rp{};
                rp.label = {"large_dot_pass", WGPU_STRLEN};
                rp.colorAttachmentCount = 1;
                rp.colorAttachments = &ca;
                rp.depthStencilAttachment = &da;

                WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(frame.encoder, &rp);
                DrawFoliage(pass);
                wgpuRenderPassEncoderEnd(pass);
                wgpuRenderPassEncoderRelease(pass);
            }

            // 3. Small-dot MSAA pass. Upstream's comparison-mode dot only
            //    differs when mode2 != 'none'; with the initial preset mode2
            //    is 'none' so we render exactly the same content.
            {
                WGPURenderPassColorAttachment ca{};
                ca.view = m_smallDotView;
                ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
                ca.loadOp = WGPULoadOp_Clear;
                ca.storeOp = WGPUStoreOp_Store;
                ca.clearValue = {0.0, 0.0, 0.0, 1.0};

                WGPURenderPassDepthStencilAttachment da{};
                da.view = m_depthView;
                da.depthClearValue = 1.0f;
                da.depthLoadOp = WGPULoadOp_Clear;
                da.depthStoreOp = WGPUStoreOp_Discard;
                da.stencilLoadOp = WGPULoadOp_Undefined;
                da.stencilStoreOp = WGPUStoreOp_Undefined;

                WGPURenderPassDescriptor rp{};
                rp.label = {"small_dot_pass", WGPU_STRLEN};
                rp.colorAttachmentCount = 1;
                rp.colorAttachments = &ca;
                rp.depthStencilAttachment = &da;

                WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(frame.encoder, &rp);
                DrawFoliage(pass);
                wgpuRenderPassEncoderEnd(pass);
                wgpuRenderPassEncoderRelease(pass);
            }

            // 4. Visualization pass - samples MSAA + resolved textures and
            //    draws a 6-vertex fullscreen triangle pair into the swapchain.
            {
                WGPURenderPassColorAttachment ca{};
                ca.view = frame.surfaceView;
                ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
                ca.loadOp = WGPULoadOp_Clear;
                ca.storeOp = WGPUStoreOp_Store;
                ca.clearValue = {1.0, 0.0, 1.0, 1.0}; // upstream's magenta "error" clear

                WGPURenderPassDescriptor rp{};
                rp.label = {"show_multisample_pass", WGPU_STRLEN};
                rp.colorAttachmentCount = 1;
                rp.colorAttachments = &ca;

                WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(frame.encoder, &rp);
                wgpuRenderPassEncoderSetPipeline(pass, m_showPipeline);
                wgpuRenderPassEncoderSetBindGroup(pass, 0, m_showBindGroup, 0, nullptr);
                wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
                wgpuRenderPassEncoderEnd(pass);
                wgpuRenderPassEncoderRelease(pass);
            }

            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            ReleaseRenderTargets();
            ReleaseShowResources();
            ReleaseFoliageResources();

            if (m_foliagePS)
            {
                wgpuShaderModuleRelease(m_foliagePS);
                m_foliagePS = nullptr;
            }
            if (m_foliageVS)
            {
                wgpuShaderModuleRelease(m_foliageVS);
                m_foliageVS = nullptr;
            }
            if (m_showPS)
            {
                wgpuShaderModuleRelease(m_showPS);
                m_showPS = nullptr;
            }
            if (m_showVS)
            {
                wgpuShaderModuleRelease(m_showVS);
                m_showVS = nullptr;
            }
        }

    private:
        // --- Foliage scene resources --------------------------------------
        bool CreateFoliageResources(pwgpu::test::SampleContext &ctx)
        {
            m_foliageUniformBuffer = pwgpu::test::CreateBuffer(
                ctx.device, sizeof(FoliageUniforms),
                WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
                "foliage_ubo");
            if (!m_foliageUniformBuffer)
                return false;

            WGPUBindGroupLayoutEntry e{};
            e.binding = 0;
            e.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
            e.buffer.type = WGPUBufferBindingType_Uniform;
            e.buffer.minBindingSize = sizeof(FoliageUniforms);

            WGPUBindGroupLayoutDescriptor bglDesc{};
            bglDesc.label = {"foliage_bgl", WGPU_STRLEN};
            bglDesc.entryCount = 1;
            bglDesc.entries = &e;
            m_foliageBGL = wgpuDeviceCreateBindGroupLayout(ctx.device, &bglDesc);
            if (!m_foliageBGL)
                return false;

            WGPUPipelineLayoutDescriptor plDesc{};
            plDesc.label = {"foliage_pl", WGPU_STRLEN};
            plDesc.bindGroupLayoutCount = 1;
            plDesc.bindGroupLayouts = &m_foliageBGL;
            m_foliagePipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device, &plDesc);
            if (!m_foliagePipelineLayout)
                return false;

            WGPUBindGroupEntry bge{};
            bge.binding = 0;
            bge.buffer = m_foliageUniformBuffer;
            bge.size = sizeof(FoliageUniforms);

            WGPUBindGroupDescriptor bgDesc{};
            bgDesc.label = {"foliage_bg", WGPU_STRLEN};
            bgDesc.layout = m_foliageBGL;
            bgDesc.entryCount = 1;
            bgDesc.entries = &bge;
            m_foliageBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &bgDesc);
            if (!m_foliageBindGroup)
                return false;

            // Pipeline: rgba8unorm target, 4xMSAA, alphaToCoverage=true (native mode).
            // depth24plus depth-write+depth-less (upstream depthTest = mode != 'blending'
            // for Foliage, and Foliage is not in blending mode here).
            WGPUColorTargetState target{};
            target.format = kColorFormat;
            target.writeMask = WGPUColorWriteMask_All;
            // In 'native' mode upstream passes blend: undefined. No alpha-blend.

            WGPUFragmentState fs{};
            fs.module = m_foliagePS;
            fs.entryPoint = {"PSMain", WGPU_STRLEN};
            fs.targetCount = 1;
            fs.targets = &target;

            WGPUDepthStencilState ds{};
            ds.format = kDepthFormat;
            ds.depthWriteEnabled = WGPUOptionalBool_True;
            ds.depthCompare = WGPUCompareFunction_Less;
            ds.stencilFront.compare = WGPUCompareFunction_Always;
            ds.stencilFront.failOp = WGPUStencilOperation_Keep;
            ds.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
            ds.stencilFront.passOp = WGPUStencilOperation_Keep;
            ds.stencilBack = ds.stencilFront;

            WGPURenderPipelineDescriptor desc{};
            desc.label = {"foliage_pipe", WGPU_STRLEN};
            desc.layout = m_foliagePipelineLayout;
            desc.vertex.module = m_foliageVS;
            desc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            desc.primitive.cullMode = WGPUCullMode_None;
            desc.primitive.frontFace = WGPUFrontFace_CCW;
            desc.depthStencil = &ds;
            desc.multisample.count = kSampleCount;
            desc.multisample.mask = 0xFFFFFFFF;
            desc.multisample.alphaToCoverageEnabled = true; // 'native' mode
            desc.fragment = &fs;

            m_foliagePipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &desc);
            return m_foliagePipeline != nullptr;
        }

        void ReleaseFoliageResources()
        {
            if (m_foliagePipeline)
            {
                wgpuRenderPipelineRelease(m_foliagePipeline);
                m_foliagePipeline = nullptr;
            }
            if (m_foliagePipelineLayout)
            {
                wgpuPipelineLayoutRelease(m_foliagePipelineLayout);
                m_foliagePipelineLayout = nullptr;
            }
            if (m_foliageBindGroup)
            {
                wgpuBindGroupRelease(m_foliageBindGroup);
                m_foliageBindGroup = nullptr;
            }
            if (m_foliageBGL)
            {
                wgpuBindGroupLayoutRelease(m_foliageBGL);
                m_foliageBGL = nullptr;
            }
            if (m_foliageUniformBuffer)
            {
                wgpuBufferRelease(m_foliageUniformBuffer);
                m_foliageUniformBuffer = nullptr;
            }
        }

        void DrawFoliage(WGPURenderPassEncoder pass) const
        {
            wgpuRenderPassEncoderSetPipeline(pass, m_foliagePipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, m_foliageBindGroup, 0, nullptr);
            wgpuRenderPassEncoderDraw(pass, kFoliageVertexCount, kFoliageInstanceCount, 0, 0);
        }

        // --- ShowMultisampleTexture pipeline + BGL -------------------------
        bool CreateShowResources(pwgpu::test::SampleContext &ctx)
        {
            WGPUBindGroupLayoutEntry entries[3] = {};
            // @binding(0) texLargeDot : Texture2DMS<float4>
            entries[0].binding = 0;
            entries[0].visibility = WGPUShaderStage_Fragment;
            entries[0].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
            entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
            entries[0].texture.multisampled = WGPUOptionalBool_True;
            // @binding(1) texSmallDot : Texture2DMS<float4>
            entries[1].binding = 1;
            entries[1].visibility = WGPUShaderStage_Fragment;
            entries[1].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
            entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
            entries[1].texture.multisampled = WGPUOptionalBool_True;
            // @binding(2) resolved : Texture2D<float4>
            entries[2].binding = 2;
            entries[2].visibility = WGPUShaderStage_Fragment;
            entries[2].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
            entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;
            entries[2].texture.multisampled = WGPUOptionalBool_False;

            WGPUBindGroupLayoutDescriptor bglDesc{};
            bglDesc.label = {"show_bgl", WGPU_STRLEN};
            bglDesc.entryCount = 3;
            bglDesc.entries = entries;
            m_showBGL = wgpuDeviceCreateBindGroupLayout(ctx.device, &bglDesc);
            if (!m_showBGL)
                return false;

            WGPUPipelineLayoutDescriptor plDesc{};
            plDesc.label = {"show_pl", WGPU_STRLEN};
            plDesc.bindGroupLayoutCount = 1;
            plDesc.bindGroupLayouts = &m_showBGL;
            m_showPipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device, &plDesc);
            if (!m_showPipelineLayout)
                return false;

            WGPUColorTargetState target{};
            target.format = ctx.surfaceFormat;
            target.writeMask = WGPUColorWriteMask_All;

            WGPUFragmentState fs{};
            fs.module = m_showPS;
            fs.entryPoint = {"PSMain", WGPU_STRLEN};
            fs.targetCount = 1;
            fs.targets = &target;

            WGPURenderPipelineDescriptor desc{};
            desc.label = {"show_pipe", WGPU_STRLEN};
            desc.layout = m_showPipelineLayout;
            desc.vertex.module = m_showVS;
            desc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            desc.primitive.cullMode = WGPUCullMode_None;
            desc.primitive.frontFace = WGPUFrontFace_CCW;
            desc.multisample.count = 1;
            desc.multisample.mask = 0xFFFFFFFF;
            desc.fragment = &fs;

            m_showPipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &desc);
            return m_showPipeline != nullptr;
        }

        void ReleaseShowResources()
        {
            if (m_showPipeline)
            {
                wgpuRenderPipelineRelease(m_showPipeline);
                m_showPipeline = nullptr;
            }
            if (m_showPipelineLayout)
            {
                wgpuPipelineLayoutRelease(m_showPipelineLayout);
                m_showPipelineLayout = nullptr;
            }
            if (m_showBindGroup)
            {
                wgpuBindGroupRelease(m_showBindGroup);
                m_showBindGroup = nullptr;
            }
            if (m_showBGL)
            {
                wgpuBindGroupLayoutRelease(m_showBGL);
                m_showBGL = nullptr;
            }
        }

        // --- Render targets (fixed kRenderTargetSize square) ---------------
        bool AllocateRenderTargets(pwgpu::test::SampleContext &ctx)
        {
            const uint32_t s = kRenderTargetSize;

            auto createMsaa = [&](const char *label) -> WGPUTexture
            {
                WGPUTextureDescriptor d{};
                d.label = {label, WGPU_STRLEN};
                d.dimension = WGPUTextureDimension_2D;
                d.size = {s, s, 1};
                d.mipLevelCount = 1;
                d.sampleCount = kSampleCount;
                d.format = kColorFormat;
                d.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
                return wgpuDeviceCreateTexture(ctx.device, &d);
            };

            m_largeDotTexture = createMsaa("large_dot_ms");
            m_smallDotTexture = createMsaa("small_dot_ms");
            if (!m_largeDotTexture || !m_smallDotTexture)
                return false;

            m_largeDotView = pwgpu::test::CreateTextureView(m_largeDotTexture, kColorFormat);
            m_smallDotView = pwgpu::test::CreateTextureView(m_smallDotTexture, kColorFormat);
            if (!m_largeDotView || !m_smallDotView)
                return false;

            {
                WGPUTextureDescriptor d{};
                d.label = {"resolve_tex", WGPU_STRLEN};
                d.dimension = WGPUTextureDimension_2D;
                d.size = {s, s, 1};
                d.mipLevelCount = 1;
                d.sampleCount = 1;
                d.format = kColorFormat;
                d.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
                m_resolveTexture = wgpuDeviceCreateTexture(ctx.device, &d);
                if (!m_resolveTexture)
                    return false;
                m_resolveView = pwgpu::test::CreateTextureView(m_resolveTexture, kColorFormat);
                if (!m_resolveView)
                    return false;
            }

            {
                WGPUTextureDescriptor d{};
                d.label = {"msaa_depth", WGPU_STRLEN};
                d.dimension = WGPUTextureDimension_2D;
                d.size = {s, s, 1};
                d.mipLevelCount = 1;
                d.sampleCount = kSampleCount;
                d.format = kDepthFormat;
                d.usage = WGPUTextureUsage_RenderAttachment;
                m_depthTexture = wgpuDeviceCreateTexture(ctx.device, &d);
                if (!m_depthTexture)
                    return false;

                WGPUTextureViewDescriptor v{};
                v.format = kDepthFormat;
                v.dimension = WGPUTextureViewDimension_2D;
                v.mipLevelCount = 1;
                v.arrayLayerCount = 1;
                v.aspect = WGPUTextureAspect_DepthOnly;
                m_depthView = wgpuTextureCreateView(m_depthTexture, &v);
                if (!m_depthView)
                    return false;
            }

            // Build the visualization bind group now that all views exist.
            WGPUBindGroupEntry e[3] = {};
            e[0].binding = 0;
            e[0].textureView = m_largeDotView;
            e[1].binding = 1;
            e[1].textureView = m_smallDotView;
            e[2].binding = 2;
            e[2].textureView = m_resolveView;

            WGPUBindGroupDescriptor bgDesc{};
            bgDesc.label = {"show_bg", WGPU_STRLEN};
            bgDesc.layout = m_showBGL;
            bgDesc.entryCount = 3;
            bgDesc.entries = e;
            m_showBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &bgDesc);
            return m_showBindGroup != nullptr;
        }

        void ReleaseRenderTargets()
        {
            if (m_depthView)
            {
                wgpuTextureViewRelease(m_depthView);
                m_depthView = nullptr;
            }
            if (m_depthTexture)
            {
                wgpuTextureRelease(m_depthTexture);
                m_depthTexture = nullptr;
            }
            if (m_resolveView)
            {
                wgpuTextureViewRelease(m_resolveView);
                m_resolveView = nullptr;
            }
            if (m_resolveTexture)
            {
                wgpuTextureRelease(m_resolveTexture);
                m_resolveTexture = nullptr;
            }
            if (m_smallDotView)
            {
                wgpuTextureViewRelease(m_smallDotView);
                m_smallDotView = nullptr;
            }
            if (m_smallDotTexture)
            {
                wgpuTextureRelease(m_smallDotTexture);
                m_smallDotTexture = nullptr;
            }
            if (m_largeDotView)
            {
                wgpuTextureViewRelease(m_largeDotView);
                m_largeDotView = nullptr;
            }
            if (m_largeDotTexture)
            {
                wgpuTextureRelease(m_largeDotTexture);
                m_largeDotTexture = nullptr;
            }
        }

        // Shaders.
        WGPUShaderModule m_showVS = nullptr;
        WGPUShaderModule m_showPS = nullptr;
        WGPUShaderModule m_foliageVS = nullptr;
        WGPUShaderModule m_foliagePS = nullptr;

        // Foliage scene.
        WGPUBuffer m_foliageUniformBuffer = nullptr;
        WGPUBindGroupLayout m_foliageBGL = nullptr;
        WGPUPipelineLayout m_foliagePipelineLayout = nullptr;
        WGPUBindGroup m_foliageBindGroup = nullptr;
        WGPURenderPipeline m_foliagePipeline = nullptr;

        // Visualization.
        WGPUBindGroupLayout m_showBGL = nullptr;
        WGPUPipelineLayout m_showPipelineLayout = nullptr;
        WGPURenderPipeline m_showPipeline = nullptr;
        WGPUBindGroup m_showBindGroup = nullptr;

        // Render targets.
        WGPUTexture m_largeDotTexture = nullptr;
        WGPUTextureView m_largeDotView = nullptr;
        WGPUTexture m_smallDotTexture = nullptr;
        WGPUTextureView m_smallDotView = nullptr;
        WGPUTexture m_resolveTexture = nullptr;
        WGPUTextureView m_resolveView = nullptr;
        WGPUTexture m_depthTexture = nullptr;
        WGPUTextureView m_depthView = nullptr;

        // Animation state.
        float m_cameraRotationDeg = 0.0f;
    };

    pwgpu::test::SampleAppDesc MakeAlphaToCoverageEmulatorDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "AlphaToCoverageEmulatorTest";
        desc.windowTitle = "PhasmaWebGPU Alpha-to-Coverage Emulator";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    pe::Log::Init();
    setvbuf(stdout, nullptr, _IONBF, 0);

    AlphaToCoverageEmulatorSample sample;
    pwgpu::test::SampleApp app(sample, MakeAlphaToCoverageEmulatorDesc());
    return app.Run(argc, argv);
}
