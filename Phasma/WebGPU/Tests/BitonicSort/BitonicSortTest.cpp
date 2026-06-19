#include "../Common/SampleApp.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"


#include <cstring>

namespace
{
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    constexpr uint32_t kNumElements = 1024;
    constexpr uint32_t kGridSize = 32; // 32*32 = 1024
    constexpr uint32_t kWorkgroupSize = 64;
    constexpr uint32_t kLocalCapacity = kWorkgroupSize * 2;            // 128 elements per workgroup
    constexpr uint32_t kWorkgroupsPer = kNumElements / kLocalCapacity; // 8

    // Must match the HLSL ALGO_* constants.
    enum Algo : uint32_t
    {
        ALGO_NONE = 0,
        ALGO_LOCAL_FLIP = 1,
        ALGO_LOCAL_DISPERSE = 2,
        ALGO_GLOBAL_FLIP = 3,
        ALGO_GLOBAL_DISPERSE = 4,
    };

    struct SortPass
    {
        uint32_t blockHeight;
        uint32_t algo;
    };

    // Mirrors the state machine in webgpu-samples bitonicSort/main.ts.
    // For N=1024 and workgroup-local capacity of 128, this produces 65 passes:
    //   3 within-workgroup flip+disperse cycles (local only) until highestBlockHeight exceeds 128,
    //   then global flips followed by disperse chains that end in a local disperse.
    std::vector<SortPass> BuildSortSchedule(uint32_t numElements, uint32_t localCapacity)
    {
        std::vector<SortPass> passes;
        uint32_t highestBlockHeight = 2;
        uint32_t nextSwapSpan = 2;
        uint32_t nextAlgo = ALGO_LOCAL_FLIP;

        while (highestBlockHeight < numElements * 2)
        {
            passes.push_back({nextSwapSpan, nextAlgo});

            if (nextSwapSpan == 1)
            {
                // End of a disperse chain - begin the next flip cycle.
                highestBlockHeight *= 2;
                if (highestBlockHeight == numElements * 2)
                {
                    nextAlgo = ALGO_NONE;
                    nextSwapSpan = 0;
                }
                else if (highestBlockHeight > localCapacity)
                {
                    nextAlgo = ALGO_GLOBAL_FLIP;
                    nextSwapSpan = highestBlockHeight;
                }
                else
                {
                    nextAlgo = ALGO_LOCAL_FLIP;
                    nextSwapSpan = highestBlockHeight;
                }
            }
            else
            {
                // Disperse - halve the swap span each step.
                nextSwapSpan /= 2;
                if (nextSwapSpan > localCapacity)
                    nextAlgo = ALGO_GLOBAL_DISPERSE;
                else
                    nextAlgo = ALGO_LOCAL_DISPERSE;
            }
        }
        return passes;
    }

    class BitonicSortSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            m_computeShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "bitonic.comp.hlsl").c_str(), "bitonic_cs");
            m_vertexShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "display.vert.hlsl").c_str(), "bitonic_display_vs");
            m_pixelShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "display.pixel.hlsl").c_str(), "bitonic_display_ps");
            if (!m_computeShader || !m_vertexShader || !m_pixelShader)
                return false;

            const uint64_t elementBytes = static_cast<uint64_t>(kNumElements) * sizeof(uint32_t);

            // sort_input: Storage + CopyDst (initial upload) + CopySrc (unused but cheap).
            m_inputBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                      elementBytes,
                                                      WGPUBufferUsage_Storage |
                                                          WGPUBufferUsage_CopyDst |
                                                          WGPUBufferUsage_CopySrc,
                                                      "sort_input");

            // sort_output: Storage (RW) + CopySrc (feeds back into input between passes).
            m_outputBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                       elementBytes,
                                                       WGPUBufferUsage_Storage |
                                                           WGPUBufferUsage_CopySrc,
                                                       "sort_output");

            // 2 uint uniform: { blockHeight, algo }. Padded to 16 bytes for alignment safety.
            m_sortUniformBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                            16,
                                                            WGPUBufferUsage_Uniform |
                                                                WGPUBufferUsage_CopyDst,
                                                            "sort_uniforms");

            // Atomic counter - zero-initialised via wgpuQueueWriteBuffer below. Only the
            // compute shader touches it (via InterlockedAdd); the render path ignores it.
            m_counterBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                        sizeof(uint32_t),
                                                        WGPUBufferUsage_Storage |
                                                            WGPUBufferUsage_CopyDst,
                                                        "sort_counter");

            m_displayUniformBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                               16,
                                                               WGPUBufferUsage_Uniform |
                                                                   WGPUBufferUsage_CopyDst,
                                                               "display_uniforms");

            if (!m_inputBuffer || !m_outputBuffer || !m_sortUniformBuffer ||
                !m_counterBuffer || !m_displayUniformBuffer)
                return false;

            // Shuffled [0, N-1] — matches the upstream webgpu-samples init so the
            // display shader's `value / totalElements` normalization produces a
            // smooth 0..1 grayscale gradient after sorting.
            std::vector<uint32_t> initial(kNumElements);
            for (uint32_t i = 0; i < kNumElements; i++)
                initial[i] = i;
            std::mt19937 rng(0xB170A1C5u);
            std::shuffle(initial.begin(), initial.end(), rng);
            wgpuQueueWriteBuffer(ctx.queue, m_inputBuffer, 0, initial.data(), elementBytes);

            const uint32_t zero = 0;
            wgpuQueueWriteBuffer(ctx.queue, m_counterBuffer, 0, &zero, sizeof(zero));

            const uint32_t displayData[4] = {kGridSize, kGridSize, 0, 0};
            wgpuQueueWriteBuffer(
                ctx.queue, m_displayUniformBuffer, 0, displayData, sizeof(displayData));

            if (!CreateComputeResources(ctx.device))
                return false;

            if (!CreateRenderResources(ctx.device, ctx.surfaceFormat))
                return false;

            // Build the sort schedule; passes are stepped one per frame in Execute()
            // so the user can watch the array progressively sort (matches upstream).
            m_schedule = BuildSortSchedule(kNumElements, kLocalCapacity);
            m_nextStep = 0;
            // 60 Hz sort-step cadence, independent of render fps.
            m_stepIntervalSeconds = 1.0 / 60.0;
            m_secondsSinceLastStep = 0.0;
            fprintf(stdout,
                    "[BitonicSort] sorting %u elements in %zu passes @ 60 Hz steps\n",
                    kNumElements,
                    m_schedule.size());

            return true;
        }

        void Resize(pwgpu::test::SampleContext &, uint32_t, uint32_t) override {}

        bool Execute(pwgpu::test::SampleContext &ctx, pwgpu::test::SampleFrame &frame) override
        {
            // Wall-clock paced: emit one sort step whenever stepIntervalSeconds of
            // real time has elapsed since the previous step.
            if (m_nextStep < m_schedule.size())
            {
                m_secondsSinceLastStep += ctx.timing.deltaSeconds;
                if (m_secondsSinceLastStep >= m_stepIntervalSeconds)
                {
                    EncodeSortStep(ctx, frame.encoder, m_schedule[m_nextStep]);
                    m_nextStep++;
                    m_secondsSinceLastStep = 0.0;
                }
            }

            WGPURenderPassColorAttachment attachment{};
            attachment.view = frame.surfaceView;
            attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            attachment.loadOp = WGPULoadOp_Clear;
            attachment.storeOp = WGPUStoreOp_Store;
            attachment.clearValue = {0.1, 0.4, 0.5, 1.0};

            WGPURenderPassDescriptor renderPassDesc{};
            renderPassDesc.colorAttachmentCount = 1;
            renderPassDesc.colorAttachments = &attachment;

            WGPURenderPassEncoder renderPass =
                wgpuCommandEncoderBeginRenderPass(frame.encoder, &renderPassDesc);
            wgpuRenderPassEncoderSetPipeline(renderPass, m_renderPipeline);
            wgpuRenderPassEncoderSetBindGroup(renderPass, 0, m_renderBindGroup, 0, nullptr);
            wgpuRenderPassEncoderDraw(renderPass, 6, 1, 0, 0);
            wgpuRenderPassEncoderEnd(renderPass);
            wgpuRenderPassEncoderRelease(renderPass);

            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            if (m_renderPipeline)
                wgpuRenderPipelineRelease(m_renderPipeline);
            if (m_computePipeline)
                wgpuComputePipelineRelease(m_computePipeline);
            if (m_renderPipelineLayout)
                wgpuPipelineLayoutRelease(m_renderPipelineLayout);
            if (m_computePipelineLayout)
                wgpuPipelineLayoutRelease(m_computePipelineLayout);
            if (m_renderBindGroup)
                wgpuBindGroupRelease(m_renderBindGroup);
            if (m_computeBindGroup)
                wgpuBindGroupRelease(m_computeBindGroup);
            if (m_renderBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_renderBindGroupLayout);
            if (m_computeBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_computeBindGroupLayout);
            if (m_pixelShader)
                wgpuShaderModuleRelease(m_pixelShader);
            if (m_vertexShader)
                wgpuShaderModuleRelease(m_vertexShader);
            if (m_computeShader)
                wgpuShaderModuleRelease(m_computeShader);
            if (m_displayUniformBuffer)
                wgpuBufferRelease(m_displayUniformBuffer);
            if (m_counterBuffer)
                wgpuBufferRelease(m_counterBuffer);
            if (m_sortUniformBuffer)
                wgpuBufferRelease(m_sortUniformBuffer);
            if (m_outputBuffer)
                wgpuBufferRelease(m_outputBuffer);
            if (m_inputBuffer)
                wgpuBufferRelease(m_inputBuffer);
        }

    private:
        bool CreateComputeResources(WGPUDevice device)
        {
            WGPUBindGroupLayoutEntry entries[4] = {};
            for (uint32_t i = 0; i < 4; i++)
            {
                entries[i].binding = i;
                entries[i].visibility = WGPUShaderStage_Compute;
            }
            entries[0].buffer.type = WGPUBufferBindingType_ReadOnlyStorage; // input_data
            entries[1].buffer.type = WGPUBufferBindingType_Storage;         // output_data
            entries[2].buffer.type = WGPUBufferBindingType_Uniform;         // uniforms
            entries[3].buffer.type = WGPUBufferBindingType_Storage;         // atomic counter

            WGPUBindGroupLayoutDescriptor bglDesc{};
            bglDesc.label = {"bitonic_compute_bgl", WGPU_STRLEN};
            bglDesc.entryCount = 4;
            bglDesc.entries = entries;
            m_computeBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);
            if (!m_computeBindGroupLayout)
                return false;

            WGPUPipelineLayoutDescriptor plDesc{};
            plDesc.bindGroupLayoutCount = 1;
            plDesc.bindGroupLayouts = &m_computeBindGroupLayout;
            m_computePipelineLayout = wgpuDeviceCreatePipelineLayout(device, &plDesc);
            if (!m_computePipelineLayout)
                return false;

            WGPUComputePipelineDescriptor pipelineDesc{};
            pipelineDesc.label = {"bitonic_compute", WGPU_STRLEN};
            pipelineDesc.layout = m_computePipelineLayout;
            pipelineDesc.compute.module = m_computeShader;
            pipelineDesc.compute.entryPoint = {"CSMain", WGPU_STRLEN};
            m_computePipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);
            if (!m_computePipeline)
                return false;

            const uint64_t elementBytes = static_cast<uint64_t>(kNumElements) * sizeof(uint32_t);
            WGPUBindGroupEntry bgEntries[4] = {};
            bgEntries[0].binding = 0;
            bgEntries[0].buffer = m_inputBuffer;
            bgEntries[0].size = elementBytes;
            bgEntries[1].binding = 1;
            bgEntries[1].buffer = m_outputBuffer;
            bgEntries[1].size = elementBytes;
            bgEntries[2].binding = 2;
            bgEntries[2].buffer = m_sortUniformBuffer;
            bgEntries[2].size = 16;
            bgEntries[3].binding = 3;
            bgEntries[3].buffer = m_counterBuffer;
            bgEntries[3].size = sizeof(uint32_t);

            WGPUBindGroupDescriptor bgDesc{};
            bgDesc.label = {"bitonic_compute_bg", WGPU_STRLEN};
            bgDesc.layout = m_computeBindGroupLayout;
            bgDesc.entryCount = 4;
            bgDesc.entries = bgEntries;
            m_computeBindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);
            return m_computeBindGroup != nullptr;
        }

        bool CreateRenderResources(WGPUDevice device, WGPUTextureFormat surfaceFormat)
        {
            WGPUBindGroupLayoutEntry entries[2] = {};
            entries[0].binding = 0;
            entries[0].visibility = WGPUShaderStage_Fragment;
            entries[0].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
            entries[1].binding = 1;
            entries[1].visibility = WGPUShaderStage_Fragment;
            entries[1].buffer.type = WGPUBufferBindingType_Uniform;
            entries[1].buffer.minBindingSize = 8;

            WGPUBindGroupLayoutDescriptor bglDesc{};
            bglDesc.label = {"bitonic_render_bgl", WGPU_STRLEN};
            bglDesc.entryCount = 2;
            bglDesc.entries = entries;
            m_renderBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);
            if (!m_renderBindGroupLayout)
                return false;

            const uint64_t elementBytes = static_cast<uint64_t>(kNumElements) * sizeof(uint32_t);
            WGPUBindGroupEntry bgEntries[2] = {};
            bgEntries[0].binding = 0;
            // Read the sorted data out of sort_input (we copy output->input at the end of the
            // sort, so the final sorted array lives there).
            bgEntries[0].buffer = m_inputBuffer;
            bgEntries[0].size = elementBytes;
            bgEntries[1].binding = 1;
            bgEntries[1].buffer = m_displayUniformBuffer;
            bgEntries[1].size = 16;

            WGPUBindGroupDescriptor bgDesc{};
            bgDesc.label = {"bitonic_render_bg", WGPU_STRLEN};
            bgDesc.layout = m_renderBindGroupLayout;
            bgDesc.entryCount = 2;
            bgDesc.entries = bgEntries;
            m_renderBindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);
            if (!m_renderBindGroup)
                return false;

            WGPUPipelineLayoutDescriptor plDesc{};
            plDesc.bindGroupLayoutCount = 1;
            plDesc.bindGroupLayouts = &m_renderBindGroupLayout;
            m_renderPipelineLayout = wgpuDeviceCreatePipelineLayout(device, &plDesc);
            if (!m_renderPipelineLayout)
                return false;

            WGPUColorTargetState colorTarget{};
            colorTarget.format = surfaceFormat;
            colorTarget.writeMask = WGPUColorWriteMask_All;

            WGPUFragmentState fragmentState{};
            fragmentState.module = m_pixelShader;
            fragmentState.entryPoint = {"PSMain", WGPU_STRLEN};
            fragmentState.targetCount = 1;
            fragmentState.targets = &colorTarget;

            WGPURenderPipelineDescriptor pipelineDesc{};
            pipelineDesc.label = {"bitonic_display", WGPU_STRLEN};
            pipelineDesc.layout = m_renderPipelineLayout;
            pipelineDesc.vertex.module = m_vertexShader;
            pipelineDesc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            pipelineDesc.primitive.cullMode = WGPUCullMode_None;
            pipelineDesc.multisample.count = 1;
            pipelineDesc.multisample.mask = 0xFFFFFFFFu;
            pipelineDesc.fragment = &fragmentState;
            m_renderPipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);
            return m_renderPipeline != nullptr;
        }

        void EncodeSortStep(pwgpu::test::SampleContext &ctx,
                            WGPUCommandEncoder encoder,
                            const SortPass &pass)
        {
            const uint32_t uniformData[4] = {pass.blockHeight, pass.algo, 0, 0};
            wgpuQueueWriteBuffer(
                ctx.queue, m_sortUniformBuffer, 0, uniformData, sizeof(uniformData));

            WGPUComputePassDescriptor passDesc{};
            WGPUComputePassEncoder cp = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
            wgpuComputePassEncoderSetPipeline(cp, m_computePipeline);
            wgpuComputePassEncoderSetBindGroup(cp, 0, m_computeBindGroup, 0, nullptr);
            wgpuComputePassEncoderDispatchWorkgroups(cp, kWorkgroupsPer, 1, 1);
            wgpuComputePassEncoderEnd(cp);
            wgpuComputePassEncoderRelease(cp);

            const uint64_t elementBytes = static_cast<uint64_t>(kNumElements) * sizeof(uint32_t);
            wgpuCommandEncoderCopyBufferToBuffer(
                encoder, m_outputBuffer, 0, m_inputBuffer, 0, elementBytes);
        }

        WGPUShaderModule m_computeShader = nullptr;
        WGPUShaderModule m_vertexShader = nullptr;
        WGPUShaderModule m_pixelShader = nullptr;
        WGPUBuffer m_inputBuffer = nullptr;
        WGPUBuffer m_outputBuffer = nullptr;
        WGPUBuffer m_sortUniformBuffer = nullptr;
        WGPUBuffer m_counterBuffer = nullptr;
        WGPUBuffer m_displayUniformBuffer = nullptr;
        WGPUBindGroupLayout m_computeBindGroupLayout = nullptr;
        WGPUPipelineLayout m_computePipelineLayout = nullptr;
        WGPUComputePipeline m_computePipeline = nullptr;
        WGPUBindGroup m_computeBindGroup = nullptr;
        WGPUBindGroupLayout m_renderBindGroupLayout = nullptr;
        WGPUPipelineLayout m_renderPipelineLayout = nullptr;
        WGPURenderPipeline m_renderPipeline = nullptr;
        WGPUBindGroup m_renderBindGroup = nullptr;

        std::vector<SortPass> m_schedule;
        size_t m_nextStep = 0;
        double m_secondsSinceLastStep = 0.0;
        double m_stepIntervalSeconds = 0.1;
    };

    pwgpu::test::SampleAppDesc MakeBitonicSortDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "BitonicSortTest";
        desc.windowTitle = "PhasmaWebGPU BitonicSort";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    pe::Log::Init();
    setvbuf(stdout, nullptr, _IONBF, 0);

    BitonicSortSample sample;
    pwgpu::test::SampleApp app(sample, MakeBitonicSortDesc());
    return app.Run(argc, argv);
}
