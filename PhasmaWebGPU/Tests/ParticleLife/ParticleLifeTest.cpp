// Port of the countingSort variant of gpu-life (github.com/AndrewKemp/gpu-life)
// to PhasmaWebGPU's C++ sample harness. Locks in the upstream default preset:
//   particleAmt = 50000, colourAmt = 200, cellAmt = 2000,
//   r=15, force=1, beta=0.3, delta=0.02, friction=0.04, avoidance=4,
//   worldSize=6, border=true, vortex=false.
// No GUI. Per-frame pipeline mirrors countingSort/main.ts exactly:
//   copyBufferToBuffer(zeroBuffer -> countBuffer)      // clear cell counts
//   cell   compute pass  (hash + scatter + atomic bump)
//   prefix compute pass  (1x1 sequential scan)
//   sort   compute pass  (scatter into sorted[] by cell offset)
//   sim    compute pass  (force accumulation + integration)
//   render pass          (instanced splat via particleBuffers[(alt+1)%2])

#include "../Common/SampleApp.h"
#include "../Common/SampleBase.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"

#include "Base/Log.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;

    // Upstream options (src/options.ts).
    constexpr uint32_t kParticleCount = 50000;
    constexpr uint32_t kColourCount = 200;
    constexpr uint32_t kCellCount = 2000;
    constexpr float kR = 15.0f;
    constexpr float kForce = 1.0f;
    constexpr float kBeta = 0.3f;
    constexpr float kDelta = 0.02f;
    constexpr float kFriction = 0.04f;
    constexpr float kAvoidance = 4.0f;
    constexpr float kWorldSize = 6.0f;
    constexpr float kBorder = 1.0f;
    constexpr float kVortex = 0.0f;

    constexpr uint32_t kWorkgroupSize = 128;

    // Buffer strides.
    constexpr uint32_t kParticleStride = 24;   // 6 floats per live particle
    constexpr uint32_t kSortStride = 32;       // packed SortParticle (see cell.comp.hlsl)
    constexpr uint64_t kUniformsSize = 16 * 4; // 16 f32 slot (uniforms CB)
    constexpr uint64_t kSimUboSize = 16 * 4;   // 16 f32 slot (simData CB)
    constexpr uint64_t kCameraUboSize = 4 * 4; // 4 f32 slot (camera CB)

    constexpr float kPi = 3.14159265358979323846f;

    // Deterministic xorshift so every run produces the same initial state.
    uint32_t g_rng = 0xC0FFEE11u;
    float FrandUnit()
    {
        uint32_t x = g_rng;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        g_rng = x ? x : 0xC0FFEE11u;
        return static_cast<float>(g_rng & 0x00FFFFFFu) / 16777216.0f;
    }

    // Mirrors src/utils.ts hslToRgb.
    void HslToRgb(float h, float s, float l, float out[3])
    {
        h = std::fmod(std::fmod(h, 360.0f) + 360.0f, 360.0f);
        if (s < 0.0f)
            s = 0.0f;
        if (s > 1.0f)
            s = 1.0f;
        if (l < 0.0f)
            l = 0.0f;
        if (l > 1.0f)
            l = 1.0f;

        float c = (1.0f - std::fabs(2.0f * l - 1.0f)) * s;
        float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
        float m = l - c / 2.0f;

        float r = 0, g = 0, b = 0;
        if (h < 60.0f)
        {
            r = c;
            g = x;
            b = 0;
        }
        else if (h < 120.0f)
        {
            r = x;
            g = c;
            b = 0;
        }
        else if (h < 180.0f)
        {
            r = 0;
            g = c;
            b = x;
        }
        else if (h < 240.0f)
        {
            r = 0;
            g = x;
            b = c;
        }
        else if (h < 300.0f)
        {
            r = x;
            g = 0;
            b = c;
        }
        else
        {
            r = c;
            g = 0;
            b = x;
        }
        out[0] = r + m;
        out[1] = g + m;
        out[2] = b + m;
    }

    class ParticleLifeSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            // ---- shaders
            m_renderVs = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "render.vert.hlsl").c_str(), "plife_render_vs");
            m_renderPs = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "render.pixel.hlsl").c_str(), "plife_render_ps");
            m_cellCs = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "cell.comp.hlsl").c_str(), "plife_cell_cs");
            m_prefixCs = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "prefix.comp.hlsl").c_str(), "plife_prefix_cs");
            m_sortCs = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "sort.comp.hlsl").c_str(), "plife_sort_cs");
            m_simCs = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "sim.comp.hlsl").c_str(), "plife_sim_cs");
            if (!m_renderVs || !m_renderPs || !m_cellCs || !m_prefixCs || !m_sortCs || !m_simCs)
                return false;

            if (!CreateUniformBuffers(ctx))
                return false;
            if (!CreateStorageBuffers(ctx))
                return false;

            if (!CreateComputeBindGroupLayouts(ctx))
                return false;
            if (!CreateRenderBindGroupLayout(ctx))
                return false;

            if (!CreateComputePipelines(ctx))
                return false;
            if (!CreateRenderPipeline(ctx))
                return false;

            InitParticles(ctx);
            WriteSimUbo(ctx);
            WriteColours(ctx);
            WriteMatrix(ctx);

            if (!CreateBindGroups(ctx))
                return false;

            return true;
        }

        void Resize(pwgpu::test::SampleContext &, uint32_t, uint32_t) override {}

        void Update(pwgpu::test::SampleContext &ctx) override
        {
            // uniformData layout (16 f32, upstream main.ts update()):
            //   [0] aspect
            //   [4..7] mouse (x, y, down, type)
            //   [8] pointSize = (1/r) * 0.015
            float data[16] = {};
            data[0] = static_cast<float>(ctx.width) /
                      static_cast<float>(ctx.height > 0 ? ctx.height : 1);
            data[4] = 0.0f; // mouse.x
            data[5] = 0.0f; // mouse.y
            data[6] = 0.0f; // mouse.down
            data[7] = 1.0f; // mouse.type
            data[8] = (1.0f / kR) * 0.015f;
            wgpuQueueWriteBuffer(ctx.queue, m_uniformBuffer, 0, data, sizeof(data));

            // cameraData layout: pos.xy, zoom, pad.
            float cam[4] = {0.0f, 0.0f, 1.0f, 0.0f};
            wgpuQueueWriteBuffer(ctx.queue, m_cameraBuffer, 0, cam, sizeof(cam));
        }

        bool Execute(pwgpu::test::SampleContext &ctx, pwgpu::test::SampleFrame &frame) override
        {
            // 1) Clear cell counts via copy from the zero buffer.
            wgpuCommandEncoderCopyBufferToBuffer(
                frame.encoder, m_zeroBuffer, 0, m_countBuffer, 0,
                static_cast<uint64_t>(kCellCount) * sizeof(uint32_t));

            // 2) cell pass (uses input = particleBuffers[alt]).
            {
                WGPUComputePassDescriptor desc{};
                desc.label = {"plife_cell", WGPU_STRLEN};
                WGPUComputePassEncoder pass =
                    wgpuCommandEncoderBeginComputePass(frame.encoder, &desc);
                wgpuComputePassEncoderSetPipeline(pass, m_cellPipeline);
                wgpuComputePassEncoderSetBindGroup(pass, 0, m_cellBindGroups[m_alternate], 0, nullptr);
                wgpuComputePassEncoderDispatchWorkgroups(
                    pass, (kParticleCount + kWorkgroupSize - 1) / kWorkgroupSize, 1, 1);
                wgpuComputePassEncoderEnd(pass);
                wgpuComputePassEncoderRelease(pass);
            }

            // 3) prefix pass (1x1x1).
            {
                WGPUComputePassDescriptor desc{};
                desc.label = {"plife_prefix", WGPU_STRLEN};
                WGPUComputePassEncoder pass =
                    wgpuCommandEncoderBeginComputePass(frame.encoder, &desc);
                wgpuComputePassEncoderSetPipeline(pass, m_prefixPipeline);
                wgpuComputePassEncoderSetBindGroup(pass, 0, m_prefixBindGroup, 0, nullptr);
                wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
                wgpuComputePassEncoderEnd(pass);
                wgpuComputePassEncoderRelease(pass);
            }

            // 4) sort pass.
            {
                WGPUComputePassDescriptor desc{};
                desc.label = {"plife_sort", WGPU_STRLEN};
                WGPUComputePassEncoder pass =
                    wgpuCommandEncoderBeginComputePass(frame.encoder, &desc);
                wgpuComputePassEncoderSetPipeline(pass, m_sortPipeline);
                wgpuComputePassEncoderSetBindGroup(pass, 0, m_sortBindGroup, 0, nullptr);
                wgpuComputePassEncoderDispatchWorkgroups(
                    pass, (kParticleCount + kWorkgroupSize - 1) / kWorkgroupSize, 1, 1);
                wgpuComputePassEncoderEnd(pass);
                wgpuComputePassEncoderRelease(pass);
            }

            // 5) sim pass (reads particleBuffers[alt], writes [1-alt]).
            {
                WGPUComputePassDescriptor desc{};
                desc.label = {"plife_sim", WGPU_STRLEN};
                WGPUComputePassEncoder pass =
                    wgpuCommandEncoderBeginComputePass(frame.encoder, &desc);
                wgpuComputePassEncoderSetPipeline(pass, m_simPipeline);
                wgpuComputePassEncoderSetBindGroup(pass, 0, m_simBindGroups[m_alternate], 0, nullptr);
                wgpuComputePassEncoderDispatchWorkgroups(
                    pass, (kParticleCount + kWorkgroupSize - 1) / kWorkgroupSize, 1, 1);
                wgpuComputePassEncoderEnd(pass);
                wgpuComputePassEncoderRelease(pass);
            }

            // 6) render pass: draws particleBuffers[(alt+1)%2], the freshly-written one.
            {
                WGPURenderPassColorAttachment color{};
                color.view = frame.surfaceView;
                color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
                color.loadOp = WGPULoadOp_Clear;
                color.storeOp = WGPUStoreOp_Store;
                color.clearValue = {0.0, 0.0, 0.0, 1.0};

                WGPURenderPassDescriptor passDesc{};
                passDesc.label = {"plife_render", WGPU_STRLEN};
                passDesc.colorAttachmentCount = 1;
                passDesc.colorAttachments = &color;

                WGPURenderPassEncoder pass =
                    wgpuCommandEncoderBeginRenderPass(frame.encoder, &passDesc);
                wgpuRenderPassEncoderSetPipeline(pass, m_renderPipeline);
                wgpuRenderPassEncoderSetBindGroup(pass, 0, m_renderBindGroup, 0, nullptr);

                const uint32_t vbIdx = (m_alternate + 1u) % 2u;
                wgpuRenderPassEncoderSetVertexBuffer(
                    pass, 0, m_particleBuffers[vbIdx], 0,
                    static_cast<uint64_t>(kParticleCount) * kParticleStride);

                // Upstream draws 6 vertices as triangle-strip; our HLSL swaps to
                // triangle-list with a 6-vertex sequence mapping to two triangles
                // (see kPositions table in render.vert.hlsl).
                wgpuRenderPassEncoderDraw(pass, 6, kParticleCount, 0, 0);

                wgpuRenderPassEncoderEnd(pass);
                wgpuRenderPassEncoderRelease(pass);
            }

            m_alternate = (m_alternate + 1u) % 2u;
            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            for (int i = 0; i < 2; ++i)
            {
                if (m_cellBindGroups[i])
                    wgpuBindGroupRelease(m_cellBindGroups[i]);
                if (m_simBindGroups[i])
                    wgpuBindGroupRelease(m_simBindGroups[i]);
            }
            if (m_prefixBindGroup)
                wgpuBindGroupRelease(m_prefixBindGroup);
            if (m_sortBindGroup)
                wgpuBindGroupRelease(m_sortBindGroup);
            if (m_renderBindGroup)
                wgpuBindGroupRelease(m_renderBindGroup);

            if (m_renderPipeline)
                wgpuRenderPipelineRelease(m_renderPipeline);
            if (m_simPipeline)
                wgpuComputePipelineRelease(m_simPipeline);
            if (m_sortPipeline)
                wgpuComputePipelineRelease(m_sortPipeline);
            if (m_prefixPipeline)
                wgpuComputePipelineRelease(m_prefixPipeline);
            if (m_cellPipeline)
                wgpuComputePipelineRelease(m_cellPipeline);

            if (m_renderPipelineLayout)
                wgpuPipelineLayoutRelease(m_renderPipelineLayout);
            if (m_simPipelineLayout)
                wgpuPipelineLayoutRelease(m_simPipelineLayout);
            if (m_sortPipelineLayout)
                wgpuPipelineLayoutRelease(m_sortPipelineLayout);
            if (m_prefixPipelineLayout)
                wgpuPipelineLayoutRelease(m_prefixPipelineLayout);
            if (m_cellPipelineLayout)
                wgpuPipelineLayoutRelease(m_cellPipelineLayout);

            if (m_renderBgl)
                wgpuBindGroupLayoutRelease(m_renderBgl);
            if (m_simBgl)
                wgpuBindGroupLayoutRelease(m_simBgl);
            if (m_sortBgl)
                wgpuBindGroupLayoutRelease(m_sortBgl);
            if (m_prefixBgl)
                wgpuBindGroupLayoutRelease(m_prefixBgl);
            if (m_cellBgl)
                wgpuBindGroupLayoutRelease(m_cellBgl);

            for (int i = 0; i < 2; ++i)
                if (m_particleBuffers[i])
                    wgpuBufferRelease(m_particleBuffers[i]);
            if (m_cellBuffer)
                wgpuBufferRelease(m_cellBuffer);
            if (m_sortedBuffer)
                wgpuBufferRelease(m_sortedBuffer);
            if (m_countBuffer)
                wgpuBufferRelease(m_countBuffer);
            if (m_indicesBuffer)
                wgpuBufferRelease(m_indicesBuffer);
            if (m_zeroBuffer)
                wgpuBufferRelease(m_zeroBuffer);
            if (m_matrixBuffer)
                wgpuBufferRelease(m_matrixBuffer);
            if (m_colourBuffer)
                wgpuBufferRelease(m_colourBuffer);
            if (m_uniformBuffer)
                wgpuBufferRelease(m_uniformBuffer);
            if (m_simUboBuffer)
                wgpuBufferRelease(m_simUboBuffer);
            if (m_cameraBuffer)
                wgpuBufferRelease(m_cameraBuffer);

            if (m_renderVs)
                wgpuShaderModuleRelease(m_renderVs);
            if (m_renderPs)
                wgpuShaderModuleRelease(m_renderPs);
            if (m_cellCs)
                wgpuShaderModuleRelease(m_cellCs);
            if (m_prefixCs)
                wgpuShaderModuleRelease(m_prefixCs);
            if (m_sortCs)
                wgpuShaderModuleRelease(m_sortCs);
            if (m_simCs)
                wgpuShaderModuleRelease(m_simCs);
        }

    private:
        bool CreateUniformBuffers(pwgpu::test::SampleContext &ctx)
        {
            m_uniformBuffer = pwgpu::test::CreateBuffer(
                ctx.device, kUniformsSize,
                WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, "plife_uniforms");
            m_simUboBuffer = pwgpu::test::CreateBuffer(
                ctx.device, kSimUboSize,
                WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, "plife_sim_ubo");
            m_cameraBuffer = pwgpu::test::CreateBuffer(
                ctx.device, kCameraUboSize,
                WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, "plife_camera");
            return m_uniformBuffer && m_simUboBuffer && m_cameraBuffer;
        }

        bool CreateStorageBuffers(pwgpu::test::SampleContext &ctx)
        {
            const uint64_t particleBytes =
                static_cast<uint64_t>(kParticleCount) * kParticleStride;
            const uint64_t sortBytes =
                static_cast<uint64_t>(kParticleCount) * kSortStride;
            const uint64_t cellBytes =
                static_cast<uint64_t>(kCellCount) * sizeof(uint32_t);
            const uint64_t indicesBytes =
                static_cast<uint64_t>(kCellCount) * 2ull * sizeof(uint32_t);

            for (int i = 0; i < 2; ++i)
            {
                char label[32];
                std::snprintf(label, sizeof(label), "plife_particles_%d", i);
                m_particleBuffers[i] = pwgpu::test::CreateBuffer(
                    ctx.device, particleBytes,
                    WGPUBufferUsage_Storage | WGPUBufferUsage_Vertex |
                        WGPUBufferUsage_CopyDst,
                    label);
                if (!m_particleBuffers[i])
                    return false;
            }

            m_cellBuffer = pwgpu::test::CreateBuffer(
                ctx.device, sortBytes,
                WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc, "plife_cell");
            m_sortedBuffer = pwgpu::test::CreateBuffer(
                ctx.device, sortBytes,
                WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc, "plife_sorted");
            m_countBuffer = pwgpu::test::CreateBuffer(
                ctx.device, cellBytes,
                WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc |
                    WGPUBufferUsage_CopyDst,
                "plife_count");
            m_indicesBuffer = pwgpu::test::CreateBuffer(
                ctx.device, indicesBytes,
                WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc, "plife_indices");
            m_zeroBuffer = pwgpu::test::CreateBuffer(
                ctx.device, cellBytes,
                WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst, "plife_zero");

            const uint64_t matrixBytes =
                static_cast<uint64_t>(kColourCount) * kColourCount * sizeof(float);
            const uint64_t colourBytes =
                static_cast<uint64_t>(kColourCount) * 3ull * sizeof(float);
            m_matrixBuffer = pwgpu::test::CreateBuffer(
                ctx.device, matrixBytes,
                WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst, "plife_matrix");
            m_colourBuffer = pwgpu::test::CreateBuffer(
                ctx.device, colourBytes,
                WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst, "plife_colours");

            if (!m_cellBuffer || !m_sortedBuffer || !m_countBuffer ||
                !m_indicesBuffer || !m_zeroBuffer || !m_matrixBuffer || !m_colourBuffer)
                return false;

            // Zero-initialise the zero buffer.
            std::vector<uint32_t> zeros(kCellCount, 0u);
            wgpuQueueWriteBuffer(
                ctx.queue, m_zeroBuffer, 0, zeros.data(),
                static_cast<uint64_t>(kCellCount) * sizeof(uint32_t));
            return true;
        }

        bool CreateComputeBindGroupLayouts(pwgpu::test::SampleContext &ctx)
        {
            // cell: { SimUBO uniform, particles ro-storage, sorted rw-storage, counts rw-storage }
            {
                WGPUBindGroupLayoutEntry e[4] = {};
                for (int i = 0; i < 4; ++i)
                {
                    e[i].binding = i;
                    e[i].visibility = WGPUShaderStage_Compute;
                }
                e[0].buffer.type = WGPUBufferBindingType_Uniform;
                e[0].buffer.minBindingSize = kSimUboSize;
                e[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
                e[2].buffer.type = WGPUBufferBindingType_Storage;
                e[3].buffer.type = WGPUBufferBindingType_Storage;

                WGPUBindGroupLayoutDescriptor d{};
                d.label = {"plife_cell_bgl", WGPU_STRLEN};
                d.entryCount = 4;
                d.entries = e;
                m_cellBgl = wgpuDeviceCreateBindGroupLayout(ctx.device, &d);
                if (!m_cellBgl)
                    return false;
            }

            // prefix: { counts rw, indices rw }
            {
                WGPUBindGroupLayoutEntry e[2] = {};
                for (int i = 0; i < 2; ++i)
                {
                    e[i].binding = i;
                    e[i].visibility = WGPUShaderStage_Compute;
                    e[i].buffer.type = WGPUBufferBindingType_Storage;
                }
                WGPUBindGroupLayoutDescriptor d{};
                d.label = {"plife_prefix_bgl", WGPU_STRLEN};
                d.entryCount = 2;
                d.entries = e;
                m_prefixBgl = wgpuDeviceCreateBindGroupLayout(ctx.device, &d);
                if (!m_prefixBgl)
                    return false;
            }

            // sort: { inputSorted ro, outputSorted rw, offsets rw }
            {
                WGPUBindGroupLayoutEntry e[3] = {};
                for (int i = 0; i < 3; ++i)
                {
                    e[i].binding = i;
                    e[i].visibility = WGPUShaderStage_Compute;
                }
                e[0].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
                e[1].buffer.type = WGPUBufferBindingType_Storage;
                e[2].buffer.type = WGPUBufferBindingType_Storage;

                WGPUBindGroupLayoutDescriptor d{};
                d.label = {"plife_sort_bgl", WGPU_STRLEN};
                d.entryCount = 3;
                d.entries = e;
                m_sortBgl = wgpuDeviceCreateBindGroupLayout(ctx.device, &d);
                if (!m_sortBgl)
                    return false;
            }

            // sim: { Uniforms UBO, SimUBO, matrix ro, particles_in ro, particles_out rw, sorted ro, indices ro }
            {
                WGPUBindGroupLayoutEntry e[7] = {};
                for (int i = 0; i < 7; ++i)
                {
                    e[i].binding = i;
                    e[i].visibility = WGPUShaderStage_Compute;
                }
                e[0].buffer.type = WGPUBufferBindingType_Uniform;
                e[0].buffer.minBindingSize = kUniformsSize;
                e[1].buffer.type = WGPUBufferBindingType_Uniform;
                e[1].buffer.minBindingSize = kSimUboSize;
                e[2].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
                e[3].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
                e[4].buffer.type = WGPUBufferBindingType_Storage;
                e[5].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
                e[6].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

                WGPUBindGroupLayoutDescriptor d{};
                d.label = {"plife_sim_bgl", WGPU_STRLEN};
                d.entryCount = 7;
                d.entries = e;
                m_simBgl = wgpuDeviceCreateBindGroupLayout(ctx.device, &d);
                if (!m_simBgl)
                    return false;
            }

            return true;
        }

        bool CreateRenderBindGroupLayout(pwgpu::test::SampleContext &ctx)
        {
            WGPUBindGroupLayoutEntry e[3] = {};

            e[0].binding = 0;
            e[0].visibility = WGPUShaderStage_Vertex;
            e[0].buffer.type = WGPUBufferBindingType_Uniform;
            e[0].buffer.minBindingSize = kUniformsSize;

            e[1].binding = 1;
            e[1].visibility = WGPUShaderStage_Vertex;
            e[1].buffer.type = WGPUBufferBindingType_Uniform;
            e[1].buffer.minBindingSize = kCameraUboSize;

            e[2].binding = 2;
            e[2].visibility = WGPUShaderStage_Fragment;
            e[2].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

            WGPUBindGroupLayoutDescriptor d{};
            d.label = {"plife_render_bgl", WGPU_STRLEN};
            d.entryCount = 3;
            d.entries = e;
            m_renderBgl = wgpuDeviceCreateBindGroupLayout(ctx.device, &d);
            return m_renderBgl != nullptr;
        }

        bool CreateComputePipelines(pwgpu::test::SampleContext &ctx)
        {
            auto makePl = [&](WGPUBindGroupLayout bgl, const char *label) -> WGPUPipelineLayout
            {
                WGPUPipelineLayoutDescriptor d{};
                d.label = {label, WGPU_STRLEN};
                d.bindGroupLayoutCount = 1;
                d.bindGroupLayouts = &bgl;
                return wgpuDeviceCreatePipelineLayout(ctx.device, &d);
            };

            m_cellPipelineLayout = makePl(m_cellBgl, "plife_cell_pl");
            m_prefixPipelineLayout = makePl(m_prefixBgl, "plife_prefix_pl");
            m_sortPipelineLayout = makePl(m_sortBgl, "plife_sort_pl");
            m_simPipelineLayout = makePl(m_simBgl, "plife_sim_pl");
            if (!m_cellPipelineLayout || !m_prefixPipelineLayout ||
                !m_sortPipelineLayout || !m_simPipelineLayout)
                return false;

            auto makeCp = [&](WGPUPipelineLayout pl, WGPUShaderModule mod,
                              const char *label) -> WGPUComputePipeline
            {
                WGPUComputePipelineDescriptor d{};
                d.label = {label, WGPU_STRLEN};
                d.layout = pl;
                d.compute.module = mod;
                d.compute.entryPoint = {"CSMain", WGPU_STRLEN};
                return wgpuDeviceCreateComputePipeline(ctx.device, &d);
            };

            m_cellPipeline = makeCp(m_cellPipelineLayout, m_cellCs, "plife_cell_cp");
            m_prefixPipeline = makeCp(m_prefixPipelineLayout, m_prefixCs, "plife_prefix_cp");
            m_sortPipeline = makeCp(m_sortPipelineLayout, m_sortCs, "plife_sort_cp");
            m_simPipeline = makeCp(m_simPipelineLayout, m_simCs, "plife_sim_cp");
            return m_cellPipeline && m_prefixPipeline && m_sortPipeline && m_simPipeline;
        }

        bool CreateRenderPipeline(pwgpu::test::SampleContext &ctx)
        {
            WGPUPipelineLayoutDescriptor plDesc{};
            plDesc.label = {"plife_render_pl", WGPU_STRLEN};
            plDesc.bindGroupLayoutCount = 1;
            plDesc.bindGroupLayouts = &m_renderBgl;
            m_renderPipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device, &plDesc);
            if (!m_renderPipelineLayout)
                return false;

            WGPUVertexAttribute attrs[3] = {};
            attrs[0].format = WGPUVertexFormat_Float32x2;
            attrs[0].offset = 0;
            attrs[0].shaderLocation = 0;
            attrs[1].format = WGPUVertexFormat_Float32x2;
            attrs[1].offset = 8;
            attrs[1].shaderLocation = 1;
            attrs[2].format = WGPUVertexFormat_Float32;
            attrs[2].offset = 16;
            attrs[2].shaderLocation = 2;

            WGPUVertexBufferLayout vbl{};
            vbl.arrayStride = kParticleStride;
            vbl.stepMode = WGPUVertexStepMode_Instance;
            vbl.attributeCount = 3;
            vbl.attributes = attrs;

            WGPUBlendState blend{};
            blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
            blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
            blend.color.operation = WGPUBlendOperation_Add;
            blend.alpha.srcFactor = WGPUBlendFactor_One;
            blend.alpha.dstFactor = WGPUBlendFactor_Zero;
            blend.alpha.operation = WGPUBlendOperation_Add;

            WGPUColorTargetState color{};
            color.format = ctx.surfaceFormat;
            color.writeMask = WGPUColorWriteMask_All;
            color.blend = &blend;

            WGPUFragmentState fragmentState{};
            fragmentState.module = m_renderPs;
            fragmentState.entryPoint = {"PSMain", WGPU_STRLEN};
            fragmentState.targetCount = 1;
            fragmentState.targets = &color;

            WGPURenderPipelineDescriptor desc{};
            desc.label = {"plife_render", WGPU_STRLEN};
            desc.layout = m_renderPipelineLayout;
            desc.vertex.module = m_renderVs;
            desc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            desc.vertex.bufferCount = 1;
            desc.vertex.buffers = &vbl;
            desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            desc.primitive.cullMode = WGPUCullMode_None;
            desc.primitive.frontFace = WGPUFrontFace_CCW;
            desc.multisample.count = 1;
            desc.multisample.mask = 0xFFFFFFFFu;
            desc.fragment = &fragmentState;

            m_renderPipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &desc);
            return m_renderPipeline != nullptr;
        }

        void InitParticles(pwgpu::test::SampleContext &ctx)
        {
            // Upstream initial spawning: clusters of a single colour, each cluster
            // located on a circle of random radius up to 0.9*worldSize and each
            // particle jittered inside a small disk around the cluster centre.
            std::vector<float> data(
                static_cast<size_t>(kParticleCount) * (kParticleStride / 4), 0.0f);

            uint32_t pi = 0;
            while (pi < kParticleCount)
            {
                float spawnAmtF = FrandUnit() *
                                  static_cast<float>(kParticleCount - pi) /
                                  static_cast<float>(kColourCount) * 5.0f;
                uint32_t spawnAmt = static_cast<uint32_t>(spawnAmtF);
                if (spawnAmt == 0u)
                    spawnAmt = 1u; // keep forward progress even when fractional

                uint32_t c = static_cast<uint32_t>(FrandUnit() * kColourCount);
                if (c >= kColourCount)
                    c = kColourCount - 1u;

                float a = FrandUnit() * 2.0f * kPi;
                float d = FrandUnit() * kWorldSize * 0.9f;
                float cx = std::cos(a) * d;
                float cy = std::sin(a) * d;

                for (uint32_t i = 0; i < spawnAmt && pi < kParticleCount; ++i)
                {
                    float ia = FrandUnit() * 2.0f * kPi;
                    float ir = FrandUnit();
                    float id = (ir * ir * ir) / 10.0f * kWorldSize;
                    size_t base = static_cast<size_t>(pi) * 6u;
                    data[base + 0] = cx + std::cos(ia) * id;
                    data[base + 1] = cy + std::sin(ia) * id;
                    data[base + 2] = 0.0f;
                    data[base + 3] = 0.0f;
                    data[base + 4] = static_cast<float>(c);
                    data[base + 5] = 0.0f;
                    ++pi;
                }
            }

            const uint64_t particleBytes =
                static_cast<uint64_t>(kParticleCount) * kParticleStride;
            wgpuQueueWriteBuffer(ctx.queue, m_particleBuffers[0], 0,
                                 data.data(), particleBytes);
            // Second buffer is scratch for the first tick; upstream leaves it
            // uninitialised but we zero-init to avoid any undefined-read
            // complaints on replay.
            std::vector<uint8_t> zeros(static_cast<size_t>(particleBytes), 0);
            wgpuQueueWriteBuffer(ctx.queue, m_particleBuffers[1], 0,
                                 zeros.data(), particleBytes);
        }

        void WriteSimUbo(pwgpu::test::SampleContext &ctx)
        {
            // Layout matches src/options.ts setSim().
            float data[16] = {};
            data[0] = static_cast<float>(kColourCount);
            data[1] = kBeta;
            data[2] = 1.0f / kR;                          // rMax (normalised)
            data[3] = kForce / (1.0f / kR);               // force / rMax
            data[4] = std::pow(0.5f, kDelta / kFriction); // friction
            data[5] = kDelta;                             // dt
            data[6] = (1.0f / kR) * 2.0f;                 // cellSize
            data[7] = static_cast<float>(kCellCount);
            data[8] = kAvoidance;
            data[9] = kWorldSize;
            data[10] = kBorder;
            data[11] = kVortex;
            wgpuQueueWriteBuffer(ctx.queue, m_simUboBuffer, 0, data, sizeof(data));
        }

        void WriteColours(pwgpu::test::SampleContext &ctx)
        {
            std::vector<float> colours(static_cast<size_t>(kColourCount) * 3);
            for (uint32_t i = 0; i < kColourCount; ++i)
            {
                float rgb[3];
                HslToRgb(static_cast<float>(i) / kColourCount * 360.0f, 1.0f, 0.5f, rgb);
                colours[i * 3 + 0] = rgb[0];
                colours[i * 3 + 1] = rgb[1];
                colours[i * 3 + 2] = rgb[2];
            }
            wgpuQueueWriteBuffer(ctx.queue, m_colourBuffer, 0, colours.data(),
                                 colours.size() * sizeof(float));
        }

        void WriteMatrix(pwgpu::test::SampleContext &ctx)
        {
            std::vector<float> m(static_cast<size_t>(kColourCount) * kColourCount);
            for (uint32_t c1 = 0; c1 < kColourCount; ++c1)
            {
                for (uint32_t c2 = 0; c2 < kColourCount; ++c2)
                {
                    m[c1 * kColourCount + c2] = FrandUnit() * 2.0f - 1.0f;
                }
            }
            wgpuQueueWriteBuffer(ctx.queue, m_matrixBuffer, 0, m.data(),
                                 m.size() * sizeof(float));
        }

        bool CreateBindGroups(pwgpu::test::SampleContext &ctx)
        {
            const uint64_t particleBytes =
                static_cast<uint64_t>(kParticleCount) * kParticleStride;
            const uint64_t sortBytes =
                static_cast<uint64_t>(kParticleCount) * kSortStride;
            const uint64_t countBytes =
                static_cast<uint64_t>(kCellCount) * sizeof(uint32_t);
            const uint64_t indicesBytes =
                static_cast<uint64_t>(kCellCount) * 2ull * sizeof(uint32_t);
            const uint64_t matrixBytes =
                static_cast<uint64_t>(kColourCount) * kColourCount * sizeof(float);
            const uint64_t colourBytes =
                static_cast<uint64_t>(kColourCount) * 3ull * sizeof(float);

            // cell bind groups (2x, ping-pong on input particles)
            for (int i = 0; i < 2; ++i)
            {
                WGPUBindGroupEntry e[4] = {};
                e[0].binding = 0;
                e[0].buffer = m_simUboBuffer;
                e[0].size = kSimUboSize;
                e[1].binding = 1;
                e[1].buffer = m_particleBuffers[i];
                e[1].size = particleBytes;
                e[2].binding = 2;
                e[2].buffer = m_cellBuffer;
                e[2].size = sortBytes;
                e[3].binding = 3;
                e[3].buffer = m_countBuffer;
                e[3].size = countBytes;

                WGPUBindGroupDescriptor d{};
                d.label = {"plife_cell_bg", WGPU_STRLEN};
                d.layout = m_cellBgl;
                d.entryCount = 4;
                d.entries = e;
                m_cellBindGroups[i] = wgpuDeviceCreateBindGroup(ctx.device, &d);
                if (!m_cellBindGroups[i])
                    return false;
            }

            // prefix bind group
            {
                WGPUBindGroupEntry e[2] = {};
                e[0].binding = 0;
                e[0].buffer = m_countBuffer;
                e[0].size = countBytes;
                e[1].binding = 1;
                e[1].buffer = m_indicesBuffer;
                e[1].size = indicesBytes;
                WGPUBindGroupDescriptor d{};
                d.label = {"plife_prefix_bg", WGPU_STRLEN};
                d.layout = m_prefixBgl;
                d.entryCount = 2;
                d.entries = e;
                m_prefixBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &d);
                if (!m_prefixBindGroup)
                    return false;
            }

            // sort bind group: reads cellBuffer, writes sortedBuffer, uses countBuffer as offsets
            {
                WGPUBindGroupEntry e[3] = {};
                e[0].binding = 0;
                e[0].buffer = m_cellBuffer;
                e[0].size = sortBytes;
                e[1].binding = 1;
                e[1].buffer = m_sortedBuffer;
                e[1].size = sortBytes;
                e[2].binding = 2;
                e[2].buffer = m_countBuffer;
                e[2].size = countBytes;
                WGPUBindGroupDescriptor d{};
                d.label = {"plife_sort_bg", WGPU_STRLEN};
                d.layout = m_sortBgl;
                d.entryCount = 3;
                d.entries = e;
                m_sortBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &d);
                if (!m_sortBindGroup)
                    return false;
            }

            // sim bind groups (2x, ping-pong on in/out particles)
            for (int i = 0; i < 2; ++i)
            {
                WGPUBindGroupEntry e[7] = {};
                e[0].binding = 0;
                e[0].buffer = m_uniformBuffer;
                e[0].size = kUniformsSize;
                e[1].binding = 1;
                e[1].buffer = m_simUboBuffer;
                e[1].size = kSimUboSize;
                e[2].binding = 2;
                e[2].buffer = m_matrixBuffer;
                e[2].size = matrixBytes;
                e[3].binding = 3;
                e[3].buffer = m_particleBuffers[i];
                e[3].size = particleBytes;
                e[4].binding = 4;
                e[4].buffer = m_particleBuffers[1 - i];
                e[4].size = particleBytes;
                e[5].binding = 5;
                e[5].buffer = m_sortedBuffer;
                e[5].size = sortBytes;
                e[6].binding = 6;
                e[6].buffer = m_indicesBuffer;
                e[6].size = indicesBytes;
                WGPUBindGroupDescriptor d{};
                d.label = {"plife_sim_bg", WGPU_STRLEN};
                d.layout = m_simBgl;
                d.entryCount = 7;
                d.entries = e;
                m_simBindGroups[i] = wgpuDeviceCreateBindGroup(ctx.device, &d);
                if (!m_simBindGroups[i])
                    return false;
            }

            // render bind group
            {
                WGPUBindGroupEntry e[3] = {};
                e[0].binding = 0;
                e[0].buffer = m_uniformBuffer;
                e[0].size = kUniformsSize;
                e[1].binding = 1;
                e[1].buffer = m_cameraBuffer;
                e[1].size = kCameraUboSize;
                e[2].binding = 2;
                e[2].buffer = m_colourBuffer;
                e[2].size = colourBytes;
                WGPUBindGroupDescriptor d{};
                d.label = {"plife_render_bg", WGPU_STRLEN};
                d.layout = m_renderBgl;
                d.entryCount = 3;
                d.entries = e;
                m_renderBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &d);
                if (!m_renderBindGroup)
                    return false;
            }

            return true;
        }

        // --- shaders
        WGPUShaderModule m_renderVs = nullptr;
        WGPUShaderModule m_renderPs = nullptr;
        WGPUShaderModule m_cellCs = nullptr;
        WGPUShaderModule m_prefixCs = nullptr;
        WGPUShaderModule m_sortCs = nullptr;
        WGPUShaderModule m_simCs = nullptr;

        // --- buffers
        WGPUBuffer m_particleBuffers[2] = {};
        WGPUBuffer m_cellBuffer = nullptr;
        WGPUBuffer m_sortedBuffer = nullptr;
        WGPUBuffer m_countBuffer = nullptr;
        WGPUBuffer m_indicesBuffer = nullptr;
        WGPUBuffer m_zeroBuffer = nullptr;
        WGPUBuffer m_matrixBuffer = nullptr;
        WGPUBuffer m_colourBuffer = nullptr;
        WGPUBuffer m_uniformBuffer = nullptr;
        WGPUBuffer m_simUboBuffer = nullptr;
        WGPUBuffer m_cameraBuffer = nullptr;

        // --- bind group layouts
        WGPUBindGroupLayout m_cellBgl = nullptr;
        WGPUBindGroupLayout m_prefixBgl = nullptr;
        WGPUBindGroupLayout m_sortBgl = nullptr;
        WGPUBindGroupLayout m_simBgl = nullptr;
        WGPUBindGroupLayout m_renderBgl = nullptr;

        // --- pipeline layouts
        WGPUPipelineLayout m_cellPipelineLayout = nullptr;
        WGPUPipelineLayout m_prefixPipelineLayout = nullptr;
        WGPUPipelineLayout m_sortPipelineLayout = nullptr;
        WGPUPipelineLayout m_simPipelineLayout = nullptr;
        WGPUPipelineLayout m_renderPipelineLayout = nullptr;

        // --- pipelines
        WGPUComputePipeline m_cellPipeline = nullptr;
        WGPUComputePipeline m_prefixPipeline = nullptr;
        WGPUComputePipeline m_sortPipeline = nullptr;
        WGPUComputePipeline m_simPipeline = nullptr;
        WGPURenderPipeline m_renderPipeline = nullptr;

        // --- bind groups
        WGPUBindGroup m_cellBindGroups[2] = {};
        WGPUBindGroup m_prefixBindGroup = nullptr;
        WGPUBindGroup m_sortBindGroup = nullptr;
        WGPUBindGroup m_simBindGroups[2] = {};
        WGPUBindGroup m_renderBindGroup = nullptr;

        uint32_t m_alternate = 0;
    };

    pwgpu::test::SampleAppDesc MakeParticleLifeDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "ParticleLifeTest";
        desc.windowTitle = "PhasmaWebGPU ParticleLife (countingSort)";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    ParticleLifeSample sample;
    pwgpu::test::SampleApp app(sample, MakeParticleLifeDesc());
    return app.Run(argc, argv);
}
