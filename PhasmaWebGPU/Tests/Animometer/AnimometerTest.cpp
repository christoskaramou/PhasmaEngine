#include "../Common/SampleApp.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"


namespace
{
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    constexpr uint32_t kNumTriangles = 20000;
    constexpr uint32_t kBytesPerTri = 5 * 4;

    // clang-format off
    const float kVertexData[] = {
         0.00f,  0.10f, 0.0f, 1.0f,    1.0f, 0.0f, 0.0f, 1.0f,
        -0.10f, -0.10f, 0.0f, 1.0f,    0.0f, 1.0f, 0.0f, 1.0f,
         0.10f, -0.10f, 0.0f, 1.0f,    0.0f, 0.0f, 1.0f, 1.0f,
    };
    // clang-format on

    float Rand01()
    {
        return static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
    }

    class AnimometerSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            m_vertexShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "animometer.vert.hlsl").c_str(), "anim_vs");
            m_pixelShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "animometer.pixel.hlsl").c_str(), "anim_ps");
            if (!m_vertexShader || !m_pixelShader)
                return false;

            m_vertexBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                       sizeof(kVertexData),
                                                       WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
                                                       "anim_vb");
            if (!m_vertexBuffer)
                return false;
            wgpuQueueWriteBuffer(ctx.queue, m_vertexBuffer, 0, kVertexData, sizeof(kVertexData));

            const uint64_t triRegion = static_cast<uint64_t>(kNumTriangles) * kBytesPerTri;
            m_triangleBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                         triRegion,
                                                         WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
                                                         "anim_tri_sbo");
            m_timeBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                     4,
                                                     WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
                                                     "anim_time_ubo");
            if (!m_triangleBuffer || !m_timeBuffer)
                return false;

            std::vector<uint8_t> cpu(triRegion, 0);
            srand(0xdeadbeefu);
            for (uint32_t i = 0; i < kNumTriangles; i++)
            {
                float *dst = reinterpret_cast<float *>(cpu.data() + static_cast<size_t>(i) * kBytesPerTri);
                dst[0] = Rand01() * 0.2f + 0.2f;
                dst[1] = 0.9f * 2.f * (Rand01() - 0.5f);
                dst[2] = 0.9f * 2.f * (Rand01() - 0.5f);
                dst[3] = Rand01() * 1.5f + 0.5f;
                dst[4] = Rand01() * 10.f;
            }

            constexpr uint64_t kChunk = 4u * 1024u * 1024u;
            for (uint64_t offset = 0; offset < triRegion; offset += kChunk)
            {
                const uint64_t size = triRegion - offset < kChunk ? triRegion - offset : kChunk;
                wgpuQueueWriteBuffer(ctx.queue, m_triangleBuffer, offset, cpu.data() + offset, static_cast<size_t>(size));
            }

            if (!CreatePipelineResources(ctx.device, ctx.surfaceFormat, triRegion))
                return false;

            return true;
        }

        void Resize(pwgpu::test::SampleContext &, uint32_t, uint32_t) override {}

        void Update(pwgpu::test::SampleContext &ctx) override
        {
            const float timeValue = static_cast<float>(ctx.timing.elapsedSeconds);
            wgpuQueueWriteBuffer(ctx.queue, m_timeBuffer, 0, &timeValue, sizeof(timeValue));
        }

        bool Execute(pwgpu::test::SampleContext &, pwgpu::test::SampleFrame &frame) override
        {
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
            wgpuRenderPassEncoderSetPipeline(renderPass, m_pipeline);
            wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, m_vertexBuffer, 0, sizeof(kVertexData));
            wgpuRenderPassEncoderSetBindGroup(renderPass, 0, m_timeBindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetBindGroup(renderPass, 1, m_triangleBindGroup, 0, nullptr);
            wgpuRenderPassEncoderDraw(renderPass, 3, kNumTriangles, 0, 0);
            wgpuRenderPassEncoderEnd(renderPass);
            wgpuRenderPassEncoderRelease(renderPass);
            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            if (m_triangleBindGroup)
                wgpuBindGroupRelease(m_triangleBindGroup);
            if (m_timeBindGroup)
                wgpuBindGroupRelease(m_timeBindGroup);
            if (m_pipeline)
                wgpuRenderPipelineRelease(m_pipeline);
            if (m_pipelineLayout)
                wgpuPipelineLayoutRelease(m_pipelineLayout);
            if (m_triangleBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_triangleBindGroupLayout);
            if (m_timeBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_timeBindGroupLayout);
            if (m_pixelShader)
                wgpuShaderModuleRelease(m_pixelShader);
            if (m_vertexShader)
                wgpuShaderModuleRelease(m_vertexShader);
            if (m_timeBuffer)
                wgpuBufferRelease(m_timeBuffer);
            if (m_triangleBuffer)
                wgpuBufferRelease(m_triangleBuffer);
            if (m_vertexBuffer)
                wgpuBufferRelease(m_vertexBuffer);
        }

    private:
        bool CreatePipelineResources(WGPUDevice device,
                                     WGPUTextureFormat surfaceFormat,
                                     uint64_t triangleBytes)
        {
            WGPUBindGroupLayoutEntry timeEntry{};
            timeEntry.binding = 0;
            timeEntry.visibility = WGPUShaderStage_Vertex;
            timeEntry.buffer.type = WGPUBufferBindingType_Uniform;
            timeEntry.buffer.minBindingSize = 4;

            WGPUBindGroupLayoutDescriptor timeLayoutDesc{};
            timeLayoutDesc.label = {"time_bgl", WGPU_STRLEN};
            timeLayoutDesc.entryCount = 1;
            timeLayoutDesc.entries = &timeEntry;
            m_timeBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &timeLayoutDesc);
            if (!m_timeBindGroupLayout)
                return false;

            WGPUBindGroupLayoutEntry triangleEntry{};
            triangleEntry.binding = 0;
            triangleEntry.visibility = WGPUShaderStage_Vertex;
            triangleEntry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
            triangleEntry.buffer.minBindingSize = kBytesPerTri;

            WGPUBindGroupLayoutDescriptor triangleLayoutDesc{};
            triangleLayoutDesc.label = {"tri_bgl", WGPU_STRLEN};
            triangleLayoutDesc.entryCount = 1;
            triangleLayoutDesc.entries = &triangleEntry;
            m_triangleBindGroupLayout =
                wgpuDeviceCreateBindGroupLayout(device, &triangleLayoutDesc);
            if (!m_triangleBindGroupLayout)
                return false;

            WGPUBindGroupLayout layouts[2] = {m_timeBindGroupLayout, m_triangleBindGroupLayout};
            WGPUPipelineLayoutDescriptor pipelineLayoutDesc{};
            pipelineLayoutDesc.label = {"anim_pl", WGPU_STRLEN};
            pipelineLayoutDesc.bindGroupLayoutCount = 2;
            pipelineLayoutDesc.bindGroupLayouts = layouts;
            m_pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);
            if (!m_pipelineLayout)
                return false;

            WGPUBindGroupEntry timeBindGroupEntry{};
            timeBindGroupEntry.binding = 0;
            timeBindGroupEntry.buffer = m_timeBuffer;
            timeBindGroupEntry.size = 4;

            WGPUBindGroupDescriptor timeBindGroupDesc{};
            timeBindGroupDesc.label = {"time_bg", WGPU_STRLEN};
            timeBindGroupDesc.layout = m_timeBindGroupLayout;
            timeBindGroupDesc.entryCount = 1;
            timeBindGroupDesc.entries = &timeBindGroupEntry;
            m_timeBindGroup = wgpuDeviceCreateBindGroup(device, &timeBindGroupDesc);
            if (!m_timeBindGroup)
                return false;

            WGPUBindGroupEntry triangleBindGroupEntry{};
            triangleBindGroupEntry.binding = 0;
            triangleBindGroupEntry.buffer = m_triangleBuffer;
            triangleBindGroupEntry.size = triangleBytes;

            WGPUBindGroupDescriptor triangleBindGroupDesc{};
            triangleBindGroupDesc.label = {"tri_bg", WGPU_STRLEN};
            triangleBindGroupDesc.layout = m_triangleBindGroupLayout;
            triangleBindGroupDesc.entryCount = 1;
            triangleBindGroupDesc.entries = &triangleBindGroupEntry;
            m_triangleBindGroup = wgpuDeviceCreateBindGroup(device, &triangleBindGroupDesc);
            if (!m_triangleBindGroup)
                return false;

            WGPUVertexAttribute attributes[2] = {};
            attributes[0].format = WGPUVertexFormat_Float32x4;
            attributes[0].offset = 0;
            attributes[0].shaderLocation = 0;
            attributes[1].format = WGPUVertexFormat_Float32x4;
            attributes[1].offset = 4 * 4;
            attributes[1].shaderLocation = 1;

            WGPUVertexBufferLayout vertexLayout{};
            vertexLayout.arrayStride = 8 * 4;
            vertexLayout.stepMode = WGPUVertexStepMode_Vertex;
            vertexLayout.attributeCount = 2;
            vertexLayout.attributes = attributes;

            WGPUColorTargetState colorTarget{};
            colorTarget.format = surfaceFormat;
            colorTarget.writeMask = WGPUColorWriteMask_All;

            WGPUFragmentState fragmentState{};
            fragmentState.module = m_pixelShader;
            fragmentState.entryPoint = {"PSMain", WGPU_STRLEN};
            fragmentState.targetCount = 1;
            fragmentState.targets = &colorTarget;

            WGPURenderPipelineDescriptor pipelineDesc{};
            pipelineDesc.label = {"anim_pipe", WGPU_STRLEN};
            pipelineDesc.layout = m_pipelineLayout;
            pipelineDesc.vertex.module = m_vertexShader;
            pipelineDesc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            pipelineDesc.vertex.bufferCount = 1;
            pipelineDesc.vertex.buffers = &vertexLayout;
            pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            pipelineDesc.primitive.cullMode = WGPUCullMode_None;
            pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
            pipelineDesc.multisample.count = 1;
            pipelineDesc.multisample.mask = 0xFFFFFFFF;
            pipelineDesc.fragment = &fragmentState;
            m_pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);
            return m_pipeline != nullptr;
        }

        WGPUShaderModule m_vertexShader = nullptr;
        WGPUShaderModule m_pixelShader = nullptr;
        WGPUBuffer m_vertexBuffer = nullptr;
        WGPUBuffer m_triangleBuffer = nullptr;
        WGPUBuffer m_timeBuffer = nullptr;
        WGPUBindGroupLayout m_timeBindGroupLayout = nullptr;
        WGPUBindGroupLayout m_triangleBindGroupLayout = nullptr;
        WGPUPipelineLayout m_pipelineLayout = nullptr;
        WGPUBindGroup m_timeBindGroup = nullptr;
        WGPUBindGroup m_triangleBindGroup = nullptr;
        WGPURenderPipeline m_pipeline = nullptr;
    };

    pwgpu::test::SampleAppDesc MakeAnimometerDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "AnimometerTest";
        desc.windowTitle = "PhasmaWebGPU Animometer - 20000 tris";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    pe::Log::Init();
    setvbuf(stdout, nullptr, _IONBF, 0);

    AnimometerSample sample;
    pwgpu::test::SampleApp app(sample, MakeAnimometerDesc());
    return app.Run(argc, argv);
}
