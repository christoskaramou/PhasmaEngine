#include "../Common/SampleApp.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"


#include <cstring>

namespace
{
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    constexpr uint32_t kGridW = 128;
    constexpr uint32_t kGridH = 128;
    constexpr uint32_t kWorkgroup = 8;
    constexpr double kTickPeriodSec = 4.0 / 60.0;
    constexpr uint32_t kSquareVerts[8] = {0, 0, 0, 1, 1, 0, 1, 1};

    class GameOfLifeSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            m_computeShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "life.comp.hlsl").c_str(), "life_cs");
            m_vertexShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "life.vert.hlsl").c_str(), "life_vs");
            m_pixelShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "life.pixel.hlsl").c_str(), "life_ps");
            if (!m_computeShader || !m_vertexShader || !m_pixelShader)
                return false;

            const uint32_t cellCount = kGridW * kGridH;
            const uint64_t cellBytes = static_cast<uint64_t>(cellCount) * sizeof(uint32_t);

            m_sizeBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                     16,
                                                     WGPUBufferUsage_Uniform |
                                                         WGPUBufferUsage_Storage |
                                                         WGPUBufferUsage_CopyDst,
                                                     "size");
            if (!m_sizeBuffer)
                return false;

            const uint32_t sizeData[4] = {kGridW, kGridH, 0, 0};
            wgpuQueueWriteBuffer(ctx.queue, m_sizeBuffer, 0, sizeData, sizeof(sizeData));

            std::vector<uint32_t> initial(cellCount);
            std::mt19937 rng(1337);
            std::uniform_real_distribution<float> dist(0.f, 1.f);
            for (uint32_t i = 0; i < cellCount; i++)
                initial[i] = dist(rng) < 0.25f ? 1u : 0u;

            m_cellBuffers[0] = pwgpu::test::CreateBuffer(ctx.device,
                                                         cellBytes,
                                                         WGPUBufferUsage_Storage |
                                                             WGPUBufferUsage_Vertex |
                                                             WGPUBufferUsage_CopyDst,
                                                         "cells0");
            m_cellBuffers[1] = pwgpu::test::CreateBuffer(ctx.device,
                                                         cellBytes,
                                                         WGPUBufferUsage_Storage |
                                                             WGPUBufferUsage_Vertex |
                                                             WGPUBufferUsage_CopyDst,
                                                         "cells1");
            m_squareBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                       sizeof(kSquareVerts),
                                                       WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
                                                       "square");
            if (!m_cellBuffers[0] || !m_cellBuffers[1] || !m_squareBuffer)
                return false;

            wgpuQueueWriteBuffer(ctx.queue, m_cellBuffers[0], 0, initial.data(), cellBytes);
            std::vector<uint32_t> zeros(cellCount, 0);
            wgpuQueueWriteBuffer(ctx.queue, m_cellBuffers[1], 0, zeros.data(), cellBytes);
            wgpuQueueWriteBuffer(ctx.queue, m_squareBuffer, 0, kSquareVerts, sizeof(kSquareVerts));

            if (!CreateComputeResources(ctx.device, cellBytes))
                return false;

            if (!CreateRenderResources(ctx.device, ctx.surfaceFormat))
                return false;

            return true;
        }

        void Resize(pwgpu::test::SampleContext &, uint32_t, uint32_t) override {}

        void Update(pwgpu::test::SampleContext &ctx) override
        {
            m_shouldTick = false;
            m_tickAccum += ctx.timing.deltaSeconds;
            if (m_tickAccum >= kTickPeriodSec)
            {
                m_shouldTick = true;
                m_tickAccum -= kTickPeriodSec;
                if (m_tickAccum > kTickPeriodSec)
                    m_tickAccum = 0.0;
            }
        }

        bool Execute(pwgpu::test::SampleContext &, pwgpu::test::SampleFrame &frame) override
        {
            if (m_shouldTick)
            {
                WGPUComputePassDescriptor computePassDesc{};
                WGPUComputePassEncoder computePass =
                    wgpuCommandEncoderBeginComputePass(frame.encoder, &computePassDesc);
                wgpuComputePassEncoderSetPipeline(computePass, m_computePipeline);
                wgpuComputePassEncoderSetBindGroup(
                    computePass, 0, m_computeBindGroups[m_loopIndex], 0, nullptr);
                wgpuComputePassEncoderDispatchWorkgroups(
                    computePass, kGridW / kWorkgroup, kGridH / kWorkgroup, 1);
                wgpuComputePassEncoderEnd(computePass);
                wgpuComputePassEncoderRelease(computePass);
            }

            WGPURenderPassColorAttachment attachment{};
            attachment.view = frame.surfaceView;
            attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            attachment.loadOp = WGPULoadOp_Clear;
            attachment.storeOp = WGPUStoreOp_Store;
            attachment.clearValue = {0.f, 0.f, 0.f, 1.f};

            WGPURenderPassDescriptor renderPassDesc{};
            renderPassDesc.colorAttachmentCount = 1;
            renderPassDesc.colorAttachments = &attachment;

            WGPURenderPassEncoder renderPass =
                wgpuCommandEncoderBeginRenderPass(frame.encoder, &renderPassDesc);
            wgpuRenderPassEncoderSetPipeline(renderPass, m_renderPipeline);
            WGPUBuffer currentCells = m_cellBuffers[m_loopIndex];
            wgpuRenderPassEncoderSetVertexBuffer(
                renderPass, 0, currentCells, 0, static_cast<uint64_t>(kGridW * kGridH) * 4u);
            wgpuRenderPassEncoderSetVertexBuffer(
                renderPass, 1, m_squareBuffer, 0, sizeof(kSquareVerts));
            wgpuRenderPassEncoderSetBindGroup(renderPass, 0, m_renderBindGroup, 0, nullptr);
            wgpuRenderPassEncoderDraw(renderPass, 4, kGridW * kGridH, 0, 0);
            wgpuRenderPassEncoderEnd(renderPass);
            wgpuRenderPassEncoderRelease(renderPass);

            if (m_shouldTick)
                m_loopIndex = 1 - m_loopIndex;
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
            for (WGPUBindGroup &bindGroup : m_computeBindGroups)
            {
                if (bindGroup)
                    wgpuBindGroupRelease(bindGroup);
                bindGroup = nullptr;
            }
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
            if (m_squareBuffer)
                wgpuBufferRelease(m_squareBuffer);
            for (WGPUBuffer &buffer : m_cellBuffers)
            {
                if (buffer)
                    wgpuBufferRelease(buffer);
                buffer = nullptr;
            }
            if (m_sizeBuffer)
                wgpuBufferRelease(m_sizeBuffer);
        }

    private:
        bool CreateComputeResources(WGPUDevice device, uint64_t cellBytes)
        {
            WGPUBindGroupLayoutEntry entries[3] = {};
            for (uint32_t i = 0; i < 3; i++)
            {
                entries[i].binding = i;
                entries[i].visibility = WGPUShaderStage_Compute;
            }
            entries[0].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
            entries[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
            entries[2].buffer.type = WGPUBufferBindingType_Storage;

            WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc{};
            bindGroupLayoutDesc.label = {"compute_bgl", WGPU_STRLEN};
            bindGroupLayoutDesc.entryCount = 3;
            bindGroupLayoutDesc.entries = entries;
            m_computeBindGroupLayout =
                wgpuDeviceCreateBindGroupLayout(device, &bindGroupLayoutDesc);
            if (!m_computeBindGroupLayout)
                return false;

            WGPUPipelineLayoutDescriptor pipelineLayoutDesc{};
            pipelineLayoutDesc.bindGroupLayoutCount = 1;
            pipelineLayoutDesc.bindGroupLayouts = &m_computeBindGroupLayout;
            m_computePipelineLayout =
                wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);
            if (!m_computePipelineLayout)
                return false;

            WGPUComputePipelineDescriptor pipelineDesc{};
            pipelineDesc.label = {"life_compute", WGPU_STRLEN};
            pipelineDesc.layout = m_computePipelineLayout;
            pipelineDesc.compute.module = m_computeShader;
            pipelineDesc.compute.entryPoint = {"CSMain", WGPU_STRLEN};
            m_computePipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);
            if (!m_computePipeline)
                return false;

            for (uint32_t i = 0; i < 2; i++)
            {
                WGPUBindGroupEntry bindGroupEntries[3] = {};
                bindGroupEntries[0].binding = 0;
                bindGroupEntries[0].buffer = m_sizeBuffer;
                bindGroupEntries[0].size = 8;
                bindGroupEntries[1].binding = 1;
                bindGroupEntries[1].buffer = m_cellBuffers[i];
                bindGroupEntries[1].size = cellBytes;
                bindGroupEntries[2].binding = 2;
                bindGroupEntries[2].buffer = m_cellBuffers[(i + 1) % 2];
                bindGroupEntries[2].size = cellBytes;

                WGPUBindGroupDescriptor bindGroupDesc{};
                bindGroupDesc.label = {i == 0 ? "cbg0" : "cbg1", WGPU_STRLEN};
                bindGroupDesc.layout = m_computeBindGroupLayout;
                bindGroupDesc.entryCount = 3;
                bindGroupDesc.entries = bindGroupEntries;
                m_computeBindGroups[i] = wgpuDeviceCreateBindGroup(device, &bindGroupDesc);
                if (!m_computeBindGroups[i])
                    return false;
            }

            return true;
        }

        bool CreateRenderResources(WGPUDevice device, WGPUTextureFormat surfaceFormat)
        {
            WGPUBindGroupLayoutEntry renderBglEntry{};
            renderBglEntry.binding = 0;
            renderBglEntry.visibility = WGPUShaderStage_Vertex;
            renderBglEntry.buffer.type = WGPUBufferBindingType_Uniform;
            renderBglEntry.buffer.minBindingSize = 8;

            WGPUBindGroupLayoutDescriptor renderBglDesc{};
            renderBglDesc.label = {"render_bgl", WGPU_STRLEN};
            renderBglDesc.entryCount = 1;
            renderBglDesc.entries = &renderBglEntry;
            m_renderBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &renderBglDesc);
            if (!m_renderBindGroupLayout)
                return false;

            WGPUBindGroupEntry renderBgEntry{};
            renderBgEntry.binding = 0;
            renderBgEntry.buffer = m_sizeBuffer;
            renderBgEntry.size = 8;

            WGPUBindGroupDescriptor renderBgDesc{};
            renderBgDesc.layout = m_renderBindGroupLayout;
            renderBgDesc.entryCount = 1;
            renderBgDesc.entries = &renderBgEntry;
            m_renderBindGroup = wgpuDeviceCreateBindGroup(device, &renderBgDesc);
            if (!m_renderBindGroup)
                return false;

            WGPUPipelineLayoutDescriptor renderPipelineLayoutDesc{};
            renderPipelineLayoutDesc.bindGroupLayoutCount = 1;
            renderPipelineLayoutDesc.bindGroupLayouts = &m_renderBindGroupLayout;
            m_renderPipelineLayout =
                wgpuDeviceCreatePipelineLayout(device, &renderPipelineLayoutDesc);
            if (!m_renderPipelineLayout)
                return false;

            WGPUVertexAttribute cellAttr{};
            cellAttr.format = WGPUVertexFormat_Uint32;
            cellAttr.offset = 0;
            cellAttr.shaderLocation = 0;

            WGPUVertexBufferLayout cellLayout{};
            cellLayout.arrayStride = 4;
            cellLayout.stepMode = WGPUVertexStepMode_Instance;
            cellLayout.attributeCount = 1;
            cellLayout.attributes = &cellAttr;

            WGPUVertexAttribute squareAttr{};
            squareAttr.format = WGPUVertexFormat_Uint32x2;
            squareAttr.offset = 0;
            squareAttr.shaderLocation = 1;

            WGPUVertexBufferLayout squareLayout{};
            squareLayout.arrayStride = 8;
            squareLayout.stepMode = WGPUVertexStepMode_Vertex;
            squareLayout.attributeCount = 1;
            squareLayout.attributes = &squareAttr;

            WGPUVertexBufferLayout vertexLayouts[2] = {cellLayout, squareLayout};

            WGPUColorTargetState colorTarget{};
            colorTarget.format = surfaceFormat;
            colorTarget.writeMask = WGPUColorWriteMask_All;

            WGPUFragmentState fragmentState{};
            fragmentState.module = m_pixelShader;
            fragmentState.entryPoint = {"PSMain", WGPU_STRLEN};
            fragmentState.targetCount = 1;
            fragmentState.targets = &colorTarget;

            WGPURenderPipelineDescriptor pipelineDesc{};
            pipelineDesc.label = {"life_render", WGPU_STRLEN};
            pipelineDesc.layout = m_renderPipelineLayout;
            pipelineDesc.vertex.module = m_vertexShader;
            pipelineDesc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            pipelineDesc.vertex.bufferCount = 2;
            pipelineDesc.vertex.buffers = vertexLayouts;
            pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleStrip;
            pipelineDesc.primitive.cullMode = WGPUCullMode_None;
            pipelineDesc.multisample.count = 1;
            pipelineDesc.multisample.mask = 0xFFFFFFFF;
            pipelineDesc.fragment = &fragmentState;
            m_renderPipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);
            return m_renderPipeline != nullptr;
        }

        WGPUShaderModule m_computeShader = nullptr;
        WGPUShaderModule m_vertexShader = nullptr;
        WGPUShaderModule m_pixelShader = nullptr;
        WGPUBuffer m_sizeBuffer = nullptr;
        WGPUBuffer m_cellBuffers[2] = {};
        WGPUBuffer m_squareBuffer = nullptr;
        WGPUBindGroupLayout m_computeBindGroupLayout = nullptr;
        WGPUPipelineLayout m_computePipelineLayout = nullptr;
        WGPUComputePipeline m_computePipeline = nullptr;
        WGPUBindGroup m_computeBindGroups[2] = {};
        WGPUBindGroupLayout m_renderBindGroupLayout = nullptr;
        WGPUPipelineLayout m_renderPipelineLayout = nullptr;
        WGPUBindGroup m_renderBindGroup = nullptr;
        WGPURenderPipeline m_renderPipeline = nullptr;
        double m_tickAccum = 0.0;
        uint32_t m_loopIndex = 0;
        bool m_shouldTick = false;
    };

    pwgpu::test::SampleAppDesc MakeGameOfLifeDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "GameOfLifeTest";
        desc.windowTitle = "Phasma WebGPU GameOfLife";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    pe::Log::Init();
    setvbuf(stdout, nullptr, _IONBF, 0);

    GameOfLifeSample sample;
    pwgpu::test::SampleApp app(sample, MakeGameOfLifeDesc());
    return app.Run(argc, argv);
}
