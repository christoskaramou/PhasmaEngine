#include "../Common/CubeGeometry.h"
#include "../Common/SampleApp.h"
#include "../Common/SampleBase.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"

#include <cmath>
#include <cstring>

namespace
{
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    constexpr float kPi = 3.14159265f;

    // Matches upstream TimestampQueryManager: two queries (begin + end of pass).
    constexpr uint32_t kQueryCount = 2;
    constexpr uint64_t kTimestampByteSize = 8;

    // Online mean + stddev accumulator, mirrors sample/timestampQuery/PerfCounter.ts.
    struct PerfCounter
    {
        uint64_t sampleCount = 0;
        double accumulated = 0.0;
        double accumulatedSq = 0.0;

        void AddSample(double value)
        {
            sampleCount += 1;
            accumulated += value;
            accumulatedSq += value * value;
        }

        double GetAverage() const
        {
            return sampleCount == 0 ? 0.0 : accumulated / static_cast<double>(sampleCount);
        }

        double GetStddev() const
        {
            if (sampleCount == 0)
                return 0.0;
            const double avg = GetAverage();
            const double variance =
                accumulatedSq / static_cast<double>(sampleCount) - avg * avg;
            return std::sqrt(variance < 0.0 ? 0.0 : variance);
        }
    };

    class TimestampQuerySample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            m_vertexShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "timestamp.vert.hlsl").c_str(), "ts_vs");
            m_pixelShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "timestamp.pixel.hlsl").c_str(), "ts_ps");
            if (!m_vertexShader || !m_pixelShader)
                return false;

            m_vertexBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                       sizeof(pwgpu::test::cube::kVertices),
                                                       WGPUBufferUsage_Vertex |
                                                           WGPUBufferUsage_CopyDst,
                                                       "ts_vb");
            m_uniformBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                        64,
                                                        WGPUBufferUsage_Uniform |
                                                            WGPUBufferUsage_CopyDst,
                                                        "ts_ubo");
            if (!m_vertexBuffer || !m_uniformBuffer)
                return false;

            wgpuQueueWriteBuffer(ctx.queue,
                                 m_vertexBuffer,
                                 0,
                                 pwgpu::test::cube::kVertices,
                                 sizeof(pwgpu::test::cube::kVertices));

            WGPUBindGroupLayoutEntry bindGroupLayoutEntry{};
            bindGroupLayoutEntry.binding = 0;
            bindGroupLayoutEntry.visibility = WGPUShaderStage_Vertex;
            bindGroupLayoutEntry.buffer.type = WGPUBufferBindingType_Uniform;
            bindGroupLayoutEntry.buffer.minBindingSize = 64;

            WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc{};
            bindGroupLayoutDesc.label = {"ts_bgl", WGPU_STRLEN};
            bindGroupLayoutDesc.entryCount = 1;
            bindGroupLayoutDesc.entries = &bindGroupLayoutEntry;
            m_bindGroupLayout =
                wgpuDeviceCreateBindGroupLayout(ctx.device, &bindGroupLayoutDesc);
            if (!m_bindGroupLayout)
                return false;

            WGPUPipelineLayoutDescriptor pipelineLayoutDesc{};
            pipelineLayoutDesc.label = {"ts_pl", WGPU_STRLEN};
            pipelineLayoutDesc.bindGroupLayoutCount = 1;
            pipelineLayoutDesc.bindGroupLayouts = &m_bindGroupLayout;
            m_pipelineLayout =
                wgpuDeviceCreatePipelineLayout(ctx.device, &pipelineLayoutDesc);
            if (!m_pipelineLayout)
                return false;

            WGPUBindGroupEntry bindGroupEntry{};
            bindGroupEntry.binding = 0;
            bindGroupEntry.buffer = m_uniformBuffer;
            bindGroupEntry.size = 64;

            WGPUBindGroupDescriptor bindGroupDesc{};
            bindGroupDesc.label = {"ts_bg", WGPU_STRLEN};
            bindGroupDesc.layout = m_bindGroupLayout;
            bindGroupDesc.entryCount = 1;
            bindGroupDesc.entries = &bindGroupEntry;
            m_bindGroup = wgpuDeviceCreateBindGroup(ctx.device, &bindGroupDesc);
            if (!m_bindGroup)
                return false;

            WGPUVertexAttribute attributes[2] = {};
            attributes[0].format = WGPUVertexFormat_Float32x4;
            attributes[0].offset = pwgpu::test::cube::kPositionOffset;
            attributes[0].shaderLocation = 0;
            attributes[1].format = WGPUVertexFormat_Float32x2;
            attributes[1].offset = pwgpu::test::cube::kUvOffset;
            attributes[1].shaderLocation = 1;

            WGPUVertexBufferLayout vertexBufferLayout{};
            vertexBufferLayout.arrayStride = pwgpu::test::cube::kVertexStride;
            vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
            vertexBufferLayout.attributeCount = 2;
            vertexBufferLayout.attributes = attributes;

            WGPUColorTargetState colorTarget{};
            colorTarget.format = ctx.surfaceFormat;
            colorTarget.writeMask = WGPUColorWriteMask_All;

            WGPUFragmentState fragmentState{};
            fragmentState.module = m_pixelShader;
            fragmentState.entryPoint = {"PSMain", WGPU_STRLEN};
            fragmentState.targetCount = 1;
            fragmentState.targets = &colorTarget;

            WGPUDepthStencilState depthState{};
            depthState.format = WGPUTextureFormat_Depth24Plus;
            depthState.depthWriteEnabled = WGPUOptionalBool_True;
            depthState.depthCompare = WGPUCompareFunction_Less;
            depthState.stencilFront.compare = WGPUCompareFunction_Always;
            depthState.stencilFront.failOp = WGPUStencilOperation_Keep;
            depthState.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
            depthState.stencilFront.passOp = WGPUStencilOperation_Keep;
            depthState.stencilBack = depthState.stencilFront;

            WGPURenderPipelineDescriptor pipelineDesc{};
            pipelineDesc.label = {"ts_pipe", WGPU_STRLEN};
            pipelineDesc.layout = m_pipelineLayout;
            pipelineDesc.vertex.module = m_vertexShader;
            pipelineDesc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            pipelineDesc.vertex.bufferCount = 1;
            pipelineDesc.vertex.buffers = &vertexBufferLayout;
            pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            pipelineDesc.primitive.cullMode = WGPUCullMode_Back;
            pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
            pipelineDesc.depthStencil = &depthState;
            pipelineDesc.multisample.count = 1;
            pipelineDesc.multisample.mask = 0xFFFFFFFF;
            pipelineDesc.fragment = &fragmentState;
            m_pipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &pipelineDesc);
            if (!m_pipeline)
                return false;

            // Inlined TimestampQueryManager resources: query set + resolve + map buffer.
            WGPUQuerySetDescriptor querySetDesc{};
            querySetDesc.label = {"ts_qs", WGPU_STRLEN};
            querySetDesc.type = WGPUQueryType_Timestamp;
            querySetDesc.count = kQueryCount;
            m_querySet = wgpuDeviceCreateQuerySet(ctx.device, &querySetDesc);
            if (!m_querySet)
                return false;

            const uint64_t timestampBufferSize = kQueryCount * kTimestampByteSize;
            m_timestampBuffer = pwgpu::test::CreateBuffer(
                ctx.device,
                timestampBufferSize,
                WGPUBufferUsage_CopySrc | WGPUBufferUsage_QueryResolve,
                "ts_resolve");
            m_timestampMapBuffer = pwgpu::test::CreateBuffer(
                ctx.device,
                timestampBufferSize,
                WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead,
                "ts_map");
            if (!m_timestampBuffer || !m_timestampMapBuffer)
                return false;

            return true;
        }

        void Resize(pwgpu::test::SampleContext &ctx, uint32_t width, uint32_t height) override
        {
            ReleaseDepthTarget();
            if (width == 0 || height == 0)
                return;

            WGPUTextureDescriptor depthDesc{};
            depthDesc.label = {"ts_depth", WGPU_STRLEN};
            depthDesc.dimension = WGPUTextureDimension_2D;
            depthDesc.size = {width, height, 1};
            depthDesc.mipLevelCount = 1;
            depthDesc.sampleCount = 1;
            depthDesc.format = WGPUTextureFormat_Depth24Plus;
            depthDesc.usage = WGPUTextureUsage_RenderAttachment;
            m_depthTexture = wgpuDeviceCreateTexture(ctx.device, &depthDesc);
            if (!m_depthTexture)
                return;

            WGPUTextureViewDescriptor depthViewDesc{};
            depthViewDesc.format = WGPUTextureFormat_Depth24Plus;
            depthViewDesc.dimension = WGPUTextureViewDimension_2D;
            depthViewDesc.mipLevelCount = 1;
            depthViewDesc.arrayLayerCount = 1;
            depthViewDesc.aspect = WGPUTextureAspect_DepthOnly;
            m_depthView = wgpuTextureCreateView(m_depthTexture, &depthViewDesc);
        }

        void Update(pwgpu::test::SampleContext &ctx) override
        {
            float aspect = ctx.height > 0 ? static_cast<float>(ctx.width) /
                                                static_cast<float>(ctx.height)
                                          : 1.0f;
            mat4 projection =
                glm::perspectiveRH_ZO((2.f * kPi) / 5.f, aspect, 1.f, 100.f);
            projection[1][1] *= -1.f;

            float time = static_cast<float>(ctx.timing.elapsedSeconds);
            mat4 view = glm::translate(mat4(1.f), vec3(0.f, 0.f, -4.f));
            view = glm::rotate(view, 1.f, vec3(sinf(time), cosf(time), 0.f));
            mat4 mvp = projection * view;
            wgpuQueueWriteBuffer(ctx.queue, m_uniformBuffer, 0, glm::value_ptr(mvp), 64);
        }

        bool Execute(pwgpu::test::SampleContext &,
                     pwgpu::test::SampleFrame &frame) override
        {
            if (!m_depthView || !m_pipeline || !m_querySet)
                return true;

            WGPURenderPassColorAttachment colorAttachment{};
            colorAttachment.view = frame.surfaceView;
            colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            colorAttachment.loadOp = WGPULoadOp_Clear;
            colorAttachment.storeOp = WGPUStoreOp_Store;
            colorAttachment.clearValue = {0.95, 0.95, 0.95, 1.0};

            WGPURenderPassDepthStencilAttachment depthAttachment{};
            depthAttachment.view = m_depthView;
            depthAttachment.depthClearValue = 1.0f;
            depthAttachment.depthLoadOp = WGPULoadOp_Clear;
            depthAttachment.depthStoreOp = WGPUStoreOp_Store;
            depthAttachment.stencilLoadOp = WGPULoadOp_Undefined;
            depthAttachment.stencilStoreOp = WGPUStoreOp_Undefined;

            // Attach timestampWrites so the engine writes ticks at begin/end of pass.
            WGPUPassTimestampWrites timestampWrites{};
            timestampWrites.querySet = m_querySet;
            timestampWrites.beginningOfPassWriteIndex = 0;
            timestampWrites.endOfPassWriteIndex = 1;

            WGPURenderPassDescriptor renderPassDesc{};
            renderPassDesc.label = {"render_pass", WGPU_STRLEN};
            renderPassDesc.colorAttachmentCount = 1;
            renderPassDesc.colorAttachments = &colorAttachment;
            renderPassDesc.depthStencilAttachment = &depthAttachment;
            renderPassDesc.timestampWrites = &timestampWrites;

            WGPURenderPassEncoder renderPass =
                wgpuCommandEncoderBeginRenderPass(frame.encoder, &renderPassDesc);
            wgpuRenderPassEncoderSetPipeline(renderPass, m_pipeline);
            wgpuRenderPassEncoderSetBindGroup(renderPass, 0, m_bindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetVertexBuffer(renderPass,
                                                 0,
                                                 m_vertexBuffer,
                                                 0,
                                                 sizeof(pwgpu::test::cube::kVertices));
            wgpuRenderPassEncoderDraw(renderPass, pwgpu::test::cube::kVertexCount, 1, 0, 0);
            wgpuRenderPassEncoderEnd(renderPass);
            wgpuRenderPassEncoderRelease(renderPass);

            // Resolve into a QUERY_RESOLVE buffer, then copy into the MAP_READ staging
            // buffer — but only when the map buffer is idle (not in-flight on the CPU).
            wgpuCommandEncoderResolveQuerySet(frame.encoder,
                                              m_querySet,
                                              0,
                                              kQueryCount,
                                              m_timestampBuffer,
                                              0);
            if (!m_mapInFlight)
            {
                wgpuCommandEncoderCopyBufferToBuffer(frame.encoder,
                                                     m_timestampBuffer,
                                                     0,
                                                     m_timestampMapBuffer,
                                                     0,
                                                     kQueryCount * kTimestampByteSize);
            }
            return true;
        }

        bool AfterSubmit(pwgpu::test::SampleContext &,
                         pwgpu::test::SampleFrame &) override
        {
            TryInitiateTimestampDownload();
            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            ReleaseDepthTarget();

            if (m_timestampMapBuffer)
            {
                wgpuBufferRelease(m_timestampMapBuffer);
                m_timestampMapBuffer = nullptr;
            }
            if (m_timestampBuffer)
            {
                wgpuBufferRelease(m_timestampBuffer);
                m_timestampBuffer = nullptr;
            }
            if (m_querySet)
            {
                wgpuQuerySetRelease(m_querySet);
                m_querySet = nullptr;
            }
            if (m_pipeline)
            {
                wgpuRenderPipelineRelease(m_pipeline);
                m_pipeline = nullptr;
            }
            if (m_pipelineLayout)
            {
                wgpuPipelineLayoutRelease(m_pipelineLayout);
                m_pipelineLayout = nullptr;
            }
            if (m_bindGroup)
            {
                wgpuBindGroupRelease(m_bindGroup);
                m_bindGroup = nullptr;
            }
            if (m_bindGroupLayout)
            {
                wgpuBindGroupLayoutRelease(m_bindGroupLayout);
                m_bindGroupLayout = nullptr;
            }
            if (m_pixelShader)
            {
                wgpuShaderModuleRelease(m_pixelShader);
                m_pixelShader = nullptr;
            }
            if (m_vertexShader)
            {
                wgpuShaderModuleRelease(m_vertexShader);
                m_vertexShader = nullptr;
            }
            if (m_uniformBuffer)
            {
                wgpuBufferRelease(m_uniformBuffer);
                m_uniformBuffer = nullptr;
            }
            if (m_vertexBuffer)
            {
                wgpuBufferRelease(m_vertexBuffer);
                m_vertexBuffer = nullptr;
            }
        }

    private:
        void ReleaseDepthTarget()
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
        }

        void TryInitiateTimestampDownload()
        {
            if (m_mapInFlight || !m_timestampMapBuffer)
                return;

            m_mapInFlight = true;

            WGPUBufferMapCallbackInfo cb{};
            cb.mode = WGPUCallbackMode_AllowProcessEvents;
            cb.userdata1 = this;
            cb.callback = [](WGPUMapAsyncStatus status,
                             WGPUStringView,
                             void *userdata1,
                             void *)
            {
                auto *self = static_cast<TimestampQuerySample *>(userdata1);
                if (status == WGPUMapAsyncStatus_Success)
                {
                    const void *data = wgpuBufferGetConstMappedRange(
                        self->m_timestampMapBuffer, 0, kQueryCount * kTimestampByteSize);
                    if (data)
                    {
                        uint64_t timestamps[kQueryCount]{};
                        std::memcpy(timestamps, data, sizeof(timestamps));
                        // Ignore invalid (negative) deltas per W3C §23.6.
                        if (timestamps[1] >= timestamps[0])
                        {
                            const uint64_t elapsedNs = timestamps[1] - timestamps[0];
                            const double elapsedMs = static_cast<double>(elapsedNs) * 1e-6;
                            self->m_counter.AddSample(elapsedMs);
                            if ((self->m_counter.sampleCount % 60) == 0)
                            {
                                fprintf(stdout,
                                        "[TimestampQuery] Render Pass duration: "
                                        "%.3f ms +/- %.3f ms (n=%llu)\n",
                                        self->m_counter.GetAverage(),
                                        self->m_counter.GetStddev(),
                                        static_cast<unsigned long long>(
                                            self->m_counter.sampleCount));
                            }
                        }
                    }
                    wgpuBufferUnmap(self->m_timestampMapBuffer);
                }
                self->m_mapInFlight = false;
            };

            wgpuBufferMapAsync(m_timestampMapBuffer,
                               WGPUMapMode_Read,
                               0,
                               kQueryCount * kTimestampByteSize,
                               cb);
        }

        WGPUShaderModule m_vertexShader = nullptr;
        WGPUShaderModule m_pixelShader = nullptr;
        WGPUBuffer m_vertexBuffer = nullptr;
        WGPUBuffer m_uniformBuffer = nullptr;
        WGPUBindGroupLayout m_bindGroupLayout = nullptr;
        WGPUPipelineLayout m_pipelineLayout = nullptr;
        WGPUBindGroup m_bindGroup = nullptr;
        WGPURenderPipeline m_pipeline = nullptr;
        WGPUTexture m_depthTexture = nullptr;
        WGPUTextureView m_depthView = nullptr;

        WGPUQuerySet m_querySet = nullptr;
        WGPUBuffer m_timestampBuffer = nullptr;
        WGPUBuffer m_timestampMapBuffer = nullptr;
        bool m_mapInFlight = false;
        PerfCounter m_counter{};
    };

    pwgpu::test::SampleAppDesc MakeTimestampQueryDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "TimestampQueryTest";
        desc.windowTitle = "PhasmaWebGPU Timestamp Query";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        desc.requiredFeatures = {WGPUFeatureName_TimestampQuery};
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    TimestampQuerySample sample;
    pwgpu::test::SampleApp app(sample, MakeTimestampQueryDesc());
    return app.Run(argc, argv);
}
