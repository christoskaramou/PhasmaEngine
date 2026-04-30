#include "../Common/SampleApp.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"

#include "Base/Log.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace
{
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    constexpr uint32_t kNumParticles = 1500;

    const float kSpriteVerts[] = {
        -0.01f,
        -0.02f,
        0.01f,
        -0.02f,
        0.00f,
        0.02f,
    };

    float Rand01()
    {
        return static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
    }

    class ComputeBoidsSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            m_computeShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "updateSprites.comp.hlsl").c_str(), "boids_cs");
            m_vertexShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "sprite.vert.hlsl").c_str(), "sprite_vs");
            m_pixelShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "sprite.pixel.hlsl").c_str(), "sprite_ps");
            if (!m_computeShader || !m_vertexShader || !m_pixelShader)
                return false;

            m_spriteBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                       sizeof(kSpriteVerts),
                                                       WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
                                                       "sprite_vb");
            if (!m_spriteBuffer)
                return false;
            wgpuQueueWriteBuffer(ctx.queue, m_spriteBuffer, 0, kSpriteVerts, sizeof(kSpriteVerts));

            constexpr uint32_t kSimParamBytes = 7 * 4;
            m_simParamBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                         kSimParamBytes,
                                                         WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
                                                         "sim_params");
            if (!m_simParamBuffer)
                return false;

            const float simData[7] = {
                0.04f,
                0.10f,
                0.025f,
                0.025f,
                0.02f,
                0.05f,
                0.005f,
            };
            wgpuQueueWriteBuffer(ctx.queue, m_simParamBuffer, 0, simData, sizeof(simData));

            std::vector<float> initial(kNumParticles * 4);
            srand(0xc0dec0de);
            for (uint32_t i = 0; i < kNumParticles; i++)
            {
                initial[4 * i + 0] = 2.f * (Rand01() - 0.5f);
                initial[4 * i + 1] = 2.f * (Rand01() - 0.5f);
                initial[4 * i + 2] = 2.f * (Rand01() - 0.5f) * 0.1f;
                initial[4 * i + 3] = 2.f * (Rand01() - 0.5f) * 0.1f;
            }

            const uint64_t particleBytes =
                static_cast<uint64_t>(initial.size()) * sizeof(float);
            for (int i = 0; i < 2; i++)
            {
                const char *label = i == 0 ? "particles0" : "particles1";
                m_particleBuffers[i] = pwgpu::test::CreateBuffer(ctx.device,
                                                                 particleBytes,
                                                                 WGPUBufferUsage_Vertex | WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
                                                                 label);
                if (!m_particleBuffers[i])
                    return false;
                wgpuQueueWriteBuffer(ctx.queue, m_particleBuffers[i], 0, initial.data(), particleBytes);
            }

            if (!CreateComputeResources(ctx.device, particleBytes))
                return false;

            if (!CreateRenderResources(ctx.device, ctx.surfaceFormat))
                return false;

            return true;
        }

        void Resize(pwgpu::test::SampleContext &, uint32_t, uint32_t) override {}

        void Update(pwgpu::test::SampleContext &) override {}

        bool Execute(pwgpu::test::SampleContext &, pwgpu::test::SampleFrame &frame) override
        {
            WGPUComputePassDescriptor computePassDesc{};
            computePassDesc.label = {"boids_pass", WGPU_STRLEN};
            WGPUComputePassEncoder computePass =
                wgpuCommandEncoderBeginComputePass(frame.encoder, &computePassDesc);
            wgpuComputePassEncoderSetPipeline(computePass, m_computePipeline);
            wgpuComputePassEncoderSetBindGroup(
                computePass, 0, m_particleBindGroups[frame.frameIndex % 2], 0, nullptr);
            wgpuComputePassEncoderDispatchWorkgroups(computePass, (kNumParticles + 63u) / 64u, 1, 1);
            wgpuComputePassEncoderEnd(computePass);
            wgpuComputePassEncoderRelease(computePass);

            WGPURenderPassColorAttachment attachment{};
            attachment.view = frame.surfaceView;
            attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            attachment.loadOp = WGPULoadOp_Clear;
            attachment.storeOp = WGPUStoreOp_Store;
            attachment.clearValue = {0.0, 0.0, 0.0, 1.0};

            WGPURenderPassDescriptor renderPassDesc{};
            renderPassDesc.label = {"render_pass", WGPU_STRLEN};
            renderPassDesc.colorAttachmentCount = 1;
            renderPassDesc.colorAttachments = &attachment;

            WGPURenderPassEncoder renderPass =
                wgpuCommandEncoderBeginRenderPass(frame.encoder, &renderPassDesc);
            wgpuRenderPassEncoderSetPipeline(renderPass, m_renderPipeline);
            wgpuRenderPassEncoderSetVertexBuffer(
                renderPass,
                0,
                m_particleBuffers[(frame.frameIndex + 1) % 2],
                0,
                static_cast<uint64_t>(kNumParticles) * 4u * sizeof(float));
            wgpuRenderPassEncoderSetVertexBuffer(renderPass, 1, m_spriteBuffer, 0, sizeof(kSpriteVerts));
            wgpuRenderPassEncoderDraw(renderPass, 3, kNumParticles, 0, 0);
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
            if (m_computePipelineLayout)
                wgpuPipelineLayoutRelease(m_computePipelineLayout);
            if (m_computeBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_computeBindGroupLayout);
            for (WGPUBindGroup &bindGroup : m_particleBindGroups)
            {
                if (bindGroup)
                    wgpuBindGroupRelease(bindGroup);
                bindGroup = nullptr;
            }
            for (WGPUBuffer &buffer : m_particleBuffers)
            {
                if (buffer)
                    wgpuBufferRelease(buffer);
                buffer = nullptr;
            }
            if (m_simParamBuffer)
                wgpuBufferRelease(m_simParamBuffer);
            if (m_spriteBuffer)
                wgpuBufferRelease(m_spriteBuffer);
            if (m_pixelShader)
                wgpuShaderModuleRelease(m_pixelShader);
            if (m_vertexShader)
                wgpuShaderModuleRelease(m_vertexShader);
            if (m_computeShader)
                wgpuShaderModuleRelease(m_computeShader);
        }

    private:
        bool CreateComputeResources(WGPUDevice device, uint64_t particleBytes)
        {
            constexpr uint64_t kSimParamBytes = 7 * 4;

            WGPUBindGroupLayoutEntry entries[3] = {};
            entries[0].binding = 0;
            entries[0].visibility = WGPUShaderStage_Compute;
            entries[0].buffer.type = WGPUBufferBindingType_Uniform;
            entries[0].buffer.minBindingSize = kSimParamBytes;
            entries[1].binding = 1;
            entries[1].visibility = WGPUShaderStage_Compute;
            entries[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
            entries[1].buffer.minBindingSize = particleBytes;
            entries[2].binding = 2;
            entries[2].visibility = WGPUShaderStage_Compute;
            entries[2].buffer.type = WGPUBufferBindingType_Storage;
            entries[2].buffer.minBindingSize = particleBytes;

            WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc{};
            bindGroupLayoutDesc.label = {"boids_bgl", WGPU_STRLEN};
            bindGroupLayoutDesc.entryCount = 3;
            bindGroupLayoutDesc.entries = entries;
            m_computeBindGroupLayout =
                wgpuDeviceCreateBindGroupLayout(device, &bindGroupLayoutDesc);
            if (!m_computeBindGroupLayout)
                return false;

            WGPUPipelineLayoutDescriptor pipelineLayoutDesc{};
            pipelineLayoutDesc.label = {"boids_pl", WGPU_STRLEN};
            pipelineLayoutDesc.bindGroupLayoutCount = 1;
            pipelineLayoutDesc.bindGroupLayouts = &m_computeBindGroupLayout;
            m_computePipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);
            if (!m_computePipelineLayout)
                return false;

            WGPUComputePipelineDescriptor pipelineDesc{};
            pipelineDesc.label = {"boids_cp", WGPU_STRLEN};
            pipelineDesc.layout = m_computePipelineLayout;
            pipelineDesc.compute.module = m_computeShader;
            pipelineDesc.compute.entryPoint = {"CSMain", WGPU_STRLEN};
            m_computePipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);
            if (!m_computePipeline)
                return false;

            for (int i = 0; i < 2; i++)
            {
                WGPUBindGroupEntry bindGroupEntries[3] = {};
                bindGroupEntries[0].binding = 0;
                bindGroupEntries[0].buffer = m_simParamBuffer;
                bindGroupEntries[0].size = kSimParamBytes;
                bindGroupEntries[1].binding = 1;
                bindGroupEntries[1].buffer = m_particleBuffers[i];
                bindGroupEntries[1].size = particleBytes;
                bindGroupEntries[2].binding = 2;
                bindGroupEntries[2].buffer = m_particleBuffers[(i + 1) % 2];
                bindGroupEntries[2].size = particleBytes;

                WGPUBindGroupDescriptor bindGroupDesc{};
                bindGroupDesc.layout = m_computeBindGroupLayout;
                bindGroupDesc.entryCount = 3;
                bindGroupDesc.entries = bindGroupEntries;
                m_particleBindGroups[i] = wgpuDeviceCreateBindGroup(device, &bindGroupDesc);
                if (!m_particleBindGroups[i])
                    return false;
            }

            return true;
        }

        bool CreateRenderResources(WGPUDevice device, WGPUTextureFormat surfaceFormat)
        {
            WGPUVertexAttribute instanceAttrs[2] = {};
            instanceAttrs[0].format = WGPUVertexFormat_Float32x2;
            instanceAttrs[0].offset = 0;
            instanceAttrs[0].shaderLocation = 0;
            instanceAttrs[1].format = WGPUVertexFormat_Float32x2;
            instanceAttrs[1].offset = 2 * 4;
            instanceAttrs[1].shaderLocation = 1;

            WGPUVertexAttribute vertexAttr{};
            vertexAttr.format = WGPUVertexFormat_Float32x2;
            vertexAttr.offset = 0;
            vertexAttr.shaderLocation = 2;

            WGPUVertexBufferLayout vertexLayouts[2] = {};
            vertexLayouts[0].arrayStride = 4 * 4;
            vertexLayouts[0].stepMode = WGPUVertexStepMode_Instance;
            vertexLayouts[0].attributeCount = 2;
            vertexLayouts[0].attributes = instanceAttrs;
            vertexLayouts[1].arrayStride = 2 * 4;
            vertexLayouts[1].stepMode = WGPUVertexStepMode_Vertex;
            vertexLayouts[1].attributeCount = 1;
            vertexLayouts[1].attributes = &vertexAttr;

            WGPUColorTargetState colorTarget{};
            colorTarget.format = surfaceFormat;
            colorTarget.writeMask = WGPUColorWriteMask_All;

            WGPUFragmentState fragmentState{};
            fragmentState.module = m_pixelShader;
            fragmentState.entryPoint = {"PSMain", WGPU_STRLEN};
            fragmentState.targetCount = 1;
            fragmentState.targets = &colorTarget;

            WGPURenderPipelineDescriptor pipelineDesc{};
            pipelineDesc.label = {"boids_rp", WGPU_STRLEN};
            pipelineDesc.vertex.module = m_vertexShader;
            pipelineDesc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            pipelineDesc.vertex.bufferCount = 2;
            pipelineDesc.vertex.buffers = vertexLayouts;
            pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            pipelineDesc.primitive.cullMode = WGPUCullMode_None;
            pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
            pipelineDesc.multisample.count = 1;
            pipelineDesc.multisample.mask = 0xFFFFFFFF;
            pipelineDesc.fragment = &fragmentState;
            m_renderPipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);
            return m_renderPipeline != nullptr;
        }

        WGPUShaderModule m_computeShader = nullptr;
        WGPUShaderModule m_vertexShader = nullptr;
        WGPUShaderModule m_pixelShader = nullptr;
        WGPUBuffer m_spriteBuffer = nullptr;
        WGPUBuffer m_simParamBuffer = nullptr;
        WGPUBuffer m_particleBuffers[2] = {};
        WGPUBindGroupLayout m_computeBindGroupLayout = nullptr;
        WGPUPipelineLayout m_computePipelineLayout = nullptr;
        WGPUComputePipeline m_computePipeline = nullptr;
        WGPUBindGroup m_particleBindGroups[2] = {};
        WGPURenderPipeline m_renderPipeline = nullptr;
    };

    pwgpu::test::SampleAppDesc MakeComputeBoidsDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "ComputeBoidsTest";
        desc.windowTitle = "PhasmaWebGPU Compute Boids - 1500 particles";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        desc.forcePresentMode = true;
        desc.preferredPresentMode = WGPUPresentMode_Fifo;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    pe::Log::Init();
    setvbuf(stdout, nullptr, _IONBF, 0);

    ComputeBoidsSample sample;
    pwgpu::test::SampleApp app(sample, MakeComputeBoidsDesc());
    return app.Run(argc, argv);
}
