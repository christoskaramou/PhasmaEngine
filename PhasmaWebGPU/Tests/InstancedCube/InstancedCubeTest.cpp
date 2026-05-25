#include "../Common/CubeGeometry.h"
#include "../Common/SampleApp.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"


#include <cmath>
#include <cstring>

namespace
{
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    constexpr int kXCount = 4;
    constexpr int kYCount = 4;
    constexpr int kNumInstances = kXCount * kYCount;
    constexpr float kPi = 3.14159265f;
    constexpr uint64_t kUniformBytes = static_cast<uint64_t>(kNumInstances) * 16 * sizeof(float);
    static_assert(kUniformBytes == 1024, "uniform buffer must be 1024 bytes");

    class InstancedCubeSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            m_vertexShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "cube.vert.hlsl").c_str(), "cube_vs");
            m_pixelShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "cube.pixel.hlsl").c_str(), "cube_ps");
            if (!m_vertexShader || !m_pixelShader)
                return false;

            m_vertexBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                       sizeof(pwgpu::test::cube::kVertices),
                                                       WGPUBufferUsage_Vertex |
                                                           WGPUBufferUsage_CopyDst,
                                                       "cube_vb");
            m_uniformBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                        kUniformBytes,
                                                        WGPUBufferUsage_Uniform |
                                                            WGPUBufferUsage_CopyDst,
                                                        "mvp_ubo");
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
            bindGroupLayoutEntry.buffer.minBindingSize = kUniformBytes;

            WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc{};
            bindGroupLayoutDesc.label = {"bgl", WGPU_STRLEN};
            bindGroupLayoutDesc.entryCount = 1;
            bindGroupLayoutDesc.entries = &bindGroupLayoutEntry;
            m_bindGroupLayout =
                wgpuDeviceCreateBindGroupLayout(ctx.device, &bindGroupLayoutDesc);
            if (!m_bindGroupLayout)
                return false;

            WGPUPipelineLayoutDescriptor pipelineLayoutDesc{};
            pipelineLayoutDesc.label = {"pl", WGPU_STRLEN};
            pipelineLayoutDesc.bindGroupLayoutCount = 1;
            pipelineLayoutDesc.bindGroupLayouts = &m_bindGroupLayout;
            m_pipelineLayout =
                wgpuDeviceCreatePipelineLayout(ctx.device, &pipelineLayoutDesc);
            if (!m_pipelineLayout)
                return false;

            WGPUBindGroupEntry bindGroupEntry{};
            bindGroupEntry.binding = 0;
            bindGroupEntry.buffer = m_uniformBuffer;
            bindGroupEntry.size = kUniformBytes;

            WGPUBindGroupDescriptor bindGroupDesc{};
            bindGroupDesc.label = {"bg", WGPU_STRLEN};
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
            depthState.stencilBack.compare = WGPUCompareFunction_Always;

            WGPURenderPipelineDescriptor pipelineDesc{};
            pipelineDesc.label = {"pipe_cube", WGPU_STRLEN};
            pipelineDesc.layout = m_pipelineLayout;
            pipelineDesc.vertex.module = m_vertexShader;
            pipelineDesc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            pipelineDesc.vertex.bufferCount = 1;
            pipelineDesc.vertex.buffers = &vertexBufferLayout;
            pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            pipelineDesc.primitive.cullMode = WGPUCullMode_Back;
            pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
            pipelineDesc.multisample.count = 1;
            pipelineDesc.multisample.mask = 0xFFFFFFFF;
            pipelineDesc.depthStencil = &depthState;
            pipelineDesc.fragment = &fragmentState;
            m_pipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &pipelineDesc);
            if (!m_pipeline)
                return false;

            InitializeModelMatrices();
            Resize(ctx, ctx.width, ctx.height);
            return m_depthTexture != nullptr && m_depthView != nullptr;
        }

        void Resize(pwgpu::test::SampleContext &ctx, uint32_t width, uint32_t height) override
        {
            ReleaseDepthTarget();
            if (width == 0 || height == 0)
                return;

            WGPUTextureDescriptor depthDesc{};
            depthDesc.label = {"depth", WGPU_STRLEN};
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
            mat4 projection = glm::perspectiveRH_ZO(2.f * kPi / 5.f, aspect, 1.f, 100.f);
            projection[1][1] *= -1.f;
            mat4 view = glm::translate(mat4(1.f), vec3(0.f, 0.f, -12.f));

            float time = static_cast<float>(ctx.timing.elapsedSeconds);
            int matrixIndex = 0;
            for (int x = 0; x < kXCount; x++)
            {
                for (int y = 0; y < kYCount; y++)
                {
                    vec3 axis(sinf((x + 0.5f) * time), cosf((y + 0.5f) * time), 0.f);
                    mat4 rotated = glm::rotate(m_modelMatrices[matrixIndex], 1.f, axis);
                    mat4 mvp = projection * view * rotated;
                    memcpy(&m_mvpData[matrixIndex * 16], glm::value_ptr(mvp), 64);
                    matrixIndex++;
                }
            }

            wgpuQueueWriteBuffer(ctx.queue, m_uniformBuffer, 0, m_mvpData.data(), sizeof(m_mvpData));
        }

        bool Execute(pwgpu::test::SampleContext &, pwgpu::test::SampleFrame &frame) override
        {
            if (!m_depthView)
                return true;

            WGPURenderPassColorAttachment colorAttachment{};
            colorAttachment.view = frame.surfaceView;
            colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            colorAttachment.loadOp = WGPULoadOp_Clear;
            colorAttachment.storeOp = WGPUStoreOp_Store;
            colorAttachment.clearValue = {0.5, 0.5, 0.5, 1.0};

            WGPURenderPassDepthStencilAttachment depthAttachment{};
            depthAttachment.view = m_depthView;
            depthAttachment.depthLoadOp = WGPULoadOp_Clear;
            depthAttachment.depthStoreOp = WGPUStoreOp_Store;
            depthAttachment.depthClearValue = 1.f;
            depthAttachment.stencilLoadOp = WGPULoadOp_Undefined;
            depthAttachment.stencilStoreOp = WGPUStoreOp_Undefined;

            WGPURenderPassDescriptor renderPassDesc{};
            renderPassDesc.label = {"render_pass", WGPU_STRLEN};
            renderPassDesc.colorAttachmentCount = 1;
            renderPassDesc.colorAttachments = &colorAttachment;
            renderPassDesc.depthStencilAttachment = &depthAttachment;

            WGPURenderPassEncoder renderPass =
                wgpuCommandEncoderBeginRenderPass(frame.encoder, &renderPassDesc);
            wgpuRenderPassEncoderSetPipeline(renderPass, m_pipeline);
            wgpuRenderPassEncoderSetBindGroup(renderPass, 0, m_bindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetVertexBuffer(renderPass,
                                                 0,
                                                 m_vertexBuffer,
                                                 0,
                                                 sizeof(pwgpu::test::cube::kVertices));
            wgpuRenderPassEncoderDraw(renderPass,
                                      pwgpu::test::cube::kVertexCount,
                                      static_cast<uint32_t>(kNumInstances),
                                      0,
                                      0);
            wgpuRenderPassEncoderEnd(renderPass);
            wgpuRenderPassEncoderRelease(renderPass);
            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            ReleaseDepthTarget();

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
        void InitializeModelMatrices()
        {
            const float step = 4.0f;
            int matrixIndex = 0;
            for (int x = 0; x < kXCount; x++)
            {
                for (int y = 0; y < kYCount; y++)
                {
                    m_modelMatrices[matrixIndex] = glm::translate(
                        mat4(1.f),
                        vec3(step * (x - kXCount / 2.f + 0.5f),
                             step * (y - kYCount / 2.f + 0.5f),
                             0.f));
                    matrixIndex++;
                }
            }
        }

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
        std::array<mat4, kNumInstances> m_modelMatrices{};
        std::array<float, kNumInstances * 16> m_mvpData{};
    };

    pwgpu::test::SampleAppDesc MakeInstancedCubeDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "InstancedCubeTest";
        desc.windowTitle = "PhasmaWebGPU InstancedCube";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    pe::Log::Init();
    setvbuf(stdout, nullptr, _IONBF, 0);

    InstancedCubeSample sample;
    pwgpu::test::SampleApp app(sample, MakeInstancedCubeDesc());
    return app.Run(argc, argv);
}
