#include "../Common/SampleApp.h"
#include "../Common/SampleBase.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"


#include <cmath>
#include <cstring>

// Port of webgpu-samples reversedZ. Simplified to the "color" mode only:
// two viewports side-by-side rendering the same scene with different depth
// buffer modes (standard vs reversed-Z) so precision behavior far from the
// camera can be compared directly. The precision-error / depth-quad modes
// from the upstream sample require sampling the depth texture as a bind-group
// resource, which is not wired up here; only the shader sources are translated
// to HLSL so the full shader set is present.

namespace
{
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    constexpr float kPi = 3.14159265f;
    constexpr int kXCount = 1;
    constexpr int kYCount = 5;
    constexpr int kNumInstances = kXCount * kYCount;
    constexpr uint32_t kMatrixFloatCount = 16;
    constexpr uint32_t kMatrixStride = kMatrixFloatCount * sizeof(float);  // 64 bytes
    constexpr uint64_t kModelUniformBytes = kMatrixStride * kNumInstances; // 320
    constexpr uint64_t kCameraUniformBytes = kMatrixStride;                // 64
    constexpr WGPUTextureFormat kDepthFormat = WGPUTextureFormat_Depth32Float;

    // Two thin, overlapping quads stacked in Z. 12 vertices (6 per quad), each
    // with a float4 position followed by a float4 color.
    constexpr float kGeometryD = 0.0001f;                       // half-distance between the two planes
    constexpr float kGeometryO = 0.5f;                          // half x-offset for partial overlap
    constexpr uint32_t kGeometryVertexSize = 8 * sizeof(float); // 32 bytes
    constexpr uint32_t kGeometryPositionOffset = 0;
    constexpr uint32_t kGeometryColorOffset = 4 * sizeof(float);
    constexpr uint32_t kGeometryDrawCount = 6 * 2;

    // clang-format off
    constexpr float kGeometryVertices[] = {
        -1.f - kGeometryO, -1.f,  kGeometryD, 1.f,   1.f, 0.f, 0.f, 1.f,
         1.f - kGeometryO, -1.f,  kGeometryD, 1.f,   1.f, 0.f, 0.f, 1.f,
        -1.f - kGeometryO,  1.f,  kGeometryD, 1.f,   1.f, 0.f, 0.f, 1.f,
         1.f - kGeometryO, -1.f,  kGeometryD, 1.f,   1.f, 0.f, 0.f, 1.f,
         1.f - kGeometryO,  1.f,  kGeometryD, 1.f,   1.f, 0.f, 0.f, 1.f,
        -1.f - kGeometryO,  1.f,  kGeometryD, 1.f,   1.f, 0.f, 0.f, 1.f,

        -1.f + kGeometryO, -1.f, -kGeometryD, 1.f,   0.f, 1.f, 0.f, 1.f,
         1.f + kGeometryO, -1.f, -kGeometryD, 1.f,   0.f, 1.f, 0.f, 1.f,
        -1.f + kGeometryO,  1.f, -kGeometryD, 1.f,   0.f, 1.f, 0.f, 1.f,
         1.f + kGeometryO, -1.f, -kGeometryD, 1.f,   0.f, 1.f, 0.f, 1.f,
         1.f + kGeometryO,  1.f, -kGeometryD, 1.f,   0.f, 1.f, 0.f, 1.f,
        -1.f + kGeometryO,  1.f, -kGeometryD, 1.f,   0.f, 1.f, 0.f, 1.f,
    };
    // clang-format on

    enum DepthBufferMode
    {
        kDepthModeDefault = 0,
        kDepthModeReversed = 1,
        kDepthModeCount = 2,
    };

    class ReversedZSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            // Main color-pass shaders (used at runtime).
            m_vertexShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "vertex.vert.hlsl").c_str(), "revz_vs");
            m_pixelShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "fragment.pixel.hlsl").c_str(), "revz_ps");
            if (!m_vertexShader || !m_pixelShader)
                return false;

            // Extra shaders from the upstream sample. Compiled so the full
            // shader set is exercised, but no pipeline objects are built for
            // them (the runtime path only needs the simple color pass).
            m_depthPrePassVs = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device,
                (ctx.shaderDir + "vertexDepthPrePass.vert.hlsl").c_str(),
                "revz_prepass_vs");
            m_precisionErrorVs = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device,
                (ctx.shaderDir + "vertexPrecisionErrorPass.vert.hlsl").c_str(),
                "revz_precision_vs");
            m_precisionErrorPs = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device,
                (ctx.shaderDir + "fragmentPrecisionErrorPass.pixel.hlsl").c_str(),
                "revz_precision_ps");
            m_textureQuadVs = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device,
                (ctx.shaderDir + "vertexTextureQuad.vert.hlsl").c_str(),
                "revz_quad_vs");
            m_textureQuadPs = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device,
                (ctx.shaderDir + "fragmentTextureQuad.pixel.hlsl").c_str(),
                "revz_quad_ps");

            // Geometry buffer.
            m_vertexBuffer = pwgpu::test::CreateBuffer(
                ctx.device,
                sizeof(kGeometryVertices),
                WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
                "revz_vb");
            // Per-frame model matrices for all instances.
            m_modelUniformBuffer = pwgpu::test::CreateBuffer(
                ctx.device,
                kModelUniformBytes,
                WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
                "revz_model_ubo");
            // Two camera buffers: one with plain view-projection, one with the
            // depth-range-remap matrix pre-multiplied in so [0,1] becomes [1,0].
            m_cameraBuffer = pwgpu::test::CreateBuffer(
                ctx.device,
                kCameraUniformBytes,
                WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
                "revz_camera_ubo");
            m_cameraReversedBuffer = pwgpu::test::CreateBuffer(
                ctx.device,
                kCameraUniformBytes,
                WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
                "revz_camera_reversed_ubo");
            if (!m_vertexBuffer || !m_modelUniformBuffer || !m_cameraBuffer ||
                !m_cameraReversedBuffer)
                return false;

            wgpuQueueWriteBuffer(
                ctx.queue, m_vertexBuffer, 0, kGeometryVertices, sizeof(kGeometryVertices));

            // Bind group layout: two uniform buffers, both visible to vertex.
            WGPUBindGroupLayoutEntry bindGroupLayoutEntries[2] = {};
            bindGroupLayoutEntries[0].binding = 0;
            bindGroupLayoutEntries[0].visibility = WGPUShaderStage_Vertex;
            bindGroupLayoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
            bindGroupLayoutEntries[0].buffer.minBindingSize = kModelUniformBytes;
            bindGroupLayoutEntries[1].binding = 1;
            bindGroupLayoutEntries[1].visibility = WGPUShaderStage_Vertex;
            bindGroupLayoutEntries[1].buffer.type = WGPUBufferBindingType_Uniform;
            bindGroupLayoutEntries[1].buffer.minBindingSize = kCameraUniformBytes;

            WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc{};
            bindGroupLayoutDesc.label = {"revz_bgl", WGPU_STRLEN};
            bindGroupLayoutDesc.entryCount = 2;
            bindGroupLayoutDesc.entries = bindGroupLayoutEntries;
            m_bindGroupLayout =
                wgpuDeviceCreateBindGroupLayout(ctx.device, &bindGroupLayoutDesc);
            if (!m_bindGroupLayout)
                return false;

            WGPUPipelineLayoutDescriptor pipelineLayoutDesc{};
            pipelineLayoutDesc.label = {"revz_pl", WGPU_STRLEN};
            pipelineLayoutDesc.bindGroupLayoutCount = 1;
            pipelineLayoutDesc.bindGroupLayouts = &m_bindGroupLayout;
            m_pipelineLayout =
                wgpuDeviceCreatePipelineLayout(ctx.device, &pipelineLayoutDesc);
            if (!m_pipelineLayout)
                return false;

            // One bind group per depth mode, each pointing at the correct
            // camera buffer.
            if (!CreateBindGroup(
                    ctx, m_cameraBuffer, "revz_bg_default", m_bindGroups[kDepthModeDefault]))
                return false;
            if (!CreateBindGroup(ctx,
                                 m_cameraReversedBuffer,
                                 "revz_bg_reversed",
                                 m_bindGroups[kDepthModeReversed]))
                return false;

            // Two pipelines: same shaders, different depth compare functions.
            if (!CreatePipeline(ctx,
                                WGPUCompareFunction_Less,
                                "revz_pipe_default",
                                m_pipelines[kDepthModeDefault]))
                return false;
            if (!CreatePipeline(ctx,
                                WGPUCompareFunction_Greater,
                                "revz_pipe_reversed",
                                m_pipelines[kDepthModeReversed]))
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
            depthDesc.label = {"revz_depth", WGPU_STRLEN};
            depthDesc.dimension = WGPUTextureDimension_2D;
            depthDesc.size = {width, height, 1};
            depthDesc.mipLevelCount = 1;
            depthDesc.sampleCount = 1;
            depthDesc.format = kDepthFormat;
            depthDesc.usage = WGPUTextureUsage_RenderAttachment;
            m_depthTexture = wgpuDeviceCreateTexture(ctx.device, &depthDesc);
            if (!m_depthTexture)
                return;

            WGPUTextureViewDescriptor depthViewDesc{};
            depthViewDesc.format = kDepthFormat;
            depthViewDesc.dimension = WGPUTextureViewDimension_2D;
            depthViewDesc.mipLevelCount = 1;
            depthViewDesc.arrayLayerCount = 1;
            depthViewDesc.aspect = WGPUTextureAspect_DepthOnly;
            m_depthView = wgpuTextureCreateView(m_depthTexture, &depthViewDesc);

            UpdateCameraMatrices(ctx);
        }

        void Update(pwgpu::test::SampleContext &ctx) override
        {
            float time = static_cast<float>(ctx.timing.elapsedSeconds);
            vec3 axis(sinf(time), cosf(time), 0.f);
            float angle = glm::radians(30.f);

            for (int i = 0; i < kNumInstances; ++i)
            {
                mat4 rotated = glm::rotate(m_modelMatrices[i], angle, axis);
                memcpy(&m_modelData[i * kMatrixFloatCount], glm::value_ptr(rotated), 64);
            }

            wgpuQueueWriteBuffer(ctx.queue,
                                 m_modelUniformBuffer,
                                 0,
                                 m_modelData.data(),
                                 sizeof(m_modelData));
        }

        bool Execute(pwgpu::test::SampleContext &ctx, pwgpu::test::SampleFrame &frame) override
        {
            if (!m_depthView)
                return true;

            const uint32_t halfWidth = ctx.width / 2;
            if (halfWidth == 0)
                return true;

            for (int m = 0; m < kDepthModeCount; ++m)
            {
                // The first mode clears the color/depth attachment; the second
                // loads so the previously-drawn left viewport is preserved.
                const bool isFirst = (m == 0);
                const float depthClearValue = (m == kDepthModeDefault) ? 1.0f : 0.0f;

                WGPURenderPassColorAttachment colorAttachment{};
                colorAttachment.view = frame.surfaceView;
                colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
                colorAttachment.loadOp = isFirst ? WGPULoadOp_Clear : WGPULoadOp_Load;
                colorAttachment.storeOp = WGPUStoreOp_Store;
                colorAttachment.clearValue = {0.0, 0.0, 0.5, 1.0};

                WGPURenderPassDepthStencilAttachment depthAttachment{};
                depthAttachment.view = m_depthView;
                depthAttachment.depthLoadOp = WGPULoadOp_Clear;
                depthAttachment.depthStoreOp = WGPUStoreOp_Store;
                depthAttachment.depthClearValue = depthClearValue;
                depthAttachment.stencilLoadOp = WGPULoadOp_Undefined;
                depthAttachment.stencilStoreOp = WGPUStoreOp_Undefined;

                WGPURenderPassDescriptor renderPassDesc{};
                renderPassDesc.label = {"revz_render_pass", WGPU_STRLEN};
                renderPassDesc.colorAttachmentCount = 1;
                renderPassDesc.colorAttachments = &colorAttachment;
                renderPassDesc.depthStencilAttachment = &depthAttachment;

                WGPURenderPassEncoder pass =
                    wgpuCommandEncoderBeginRenderPass(frame.encoder, &renderPassDesc);

                const float viewportX = static_cast<float>(halfWidth * m);
                wgpuRenderPassEncoderSetViewport(pass,
                                                 viewportX,
                                                 0.f,
                                                 static_cast<float>(halfWidth),
                                                 static_cast<float>(ctx.height),
                                                 0.f,
                                                 1.f);
                wgpuRenderPassEncoderSetPipeline(pass, m_pipelines[m]);
                wgpuRenderPassEncoderSetBindGroup(pass, 0, m_bindGroups[m], 0, nullptr);
                wgpuRenderPassEncoderSetVertexBuffer(
                    pass, 0, m_vertexBuffer, 0, sizeof(kGeometryVertices));
                wgpuRenderPassEncoderDraw(pass,
                                          kGeometryDrawCount,
                                          static_cast<uint32_t>(kNumInstances),
                                          0,
                                          0);
                wgpuRenderPassEncoderEnd(pass);
                wgpuRenderPassEncoderRelease(pass);
            }

            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            ReleaseDepthTarget();

            for (int m = 0; m < kDepthModeCount; ++m)
            {
                if (m_pipelines[m])
                {
                    wgpuRenderPipelineRelease(m_pipelines[m]);
                    m_pipelines[m] = nullptr;
                }
                if (m_bindGroups[m])
                {
                    wgpuBindGroupRelease(m_bindGroups[m]);
                    m_bindGroups[m] = nullptr;
                }
            }

            if (m_pipelineLayout)
            {
                wgpuPipelineLayoutRelease(m_pipelineLayout);
                m_pipelineLayout = nullptr;
            }

            if (m_bindGroupLayout)
            {
                wgpuBindGroupLayoutRelease(m_bindGroupLayout);
                m_bindGroupLayout = nullptr;
            }

            ReleaseShader(m_textureQuadPs);
            ReleaseShader(m_textureQuadVs);
            ReleaseShader(m_precisionErrorPs);
            ReleaseShader(m_precisionErrorVs);
            ReleaseShader(m_depthPrePassVs);
            ReleaseShader(m_pixelShader);
            ReleaseShader(m_vertexShader);

            if (m_cameraReversedBuffer)
            {
                wgpuBufferRelease(m_cameraReversedBuffer);
                m_cameraReversedBuffer = nullptr;
            }
            if (m_cameraBuffer)
            {
                wgpuBufferRelease(m_cameraBuffer);
                m_cameraBuffer = nullptr;
            }
            if (m_modelUniformBuffer)
            {
                wgpuBufferRelease(m_modelUniformBuffer);
                m_modelUniformBuffer = nullptr;
            }
            if (m_vertexBuffer)
            {
                wgpuBufferRelease(m_vertexBuffer);
                m_vertexBuffer = nullptr;
            }
        }

    private:
        bool CreateBindGroup(pwgpu::test::SampleContext &ctx,
                             WGPUBuffer cameraBuffer,
                             const char *label,
                             WGPUBindGroup &outGroup)
        {
            WGPUBindGroupEntry entries[2] = {};
            entries[0].binding = 0;
            entries[0].buffer = m_modelUniformBuffer;
            entries[0].size = kModelUniformBytes;
            entries[1].binding = 1;
            entries[1].buffer = cameraBuffer;
            entries[1].size = kCameraUniformBytes;

            WGPUBindGroupDescriptor desc{};
            desc.label = {label, WGPU_STRLEN};
            desc.layout = m_bindGroupLayout;
            desc.entryCount = 2;
            desc.entries = entries;
            outGroup = wgpuDeviceCreateBindGroup(ctx.device, &desc);
            return outGroup != nullptr;
        }

        bool CreatePipeline(pwgpu::test::SampleContext &ctx,
                            WGPUCompareFunction depthCompare,
                            const char *label,
                            WGPURenderPipeline &outPipeline)
        {
            WGPUVertexAttribute attributes[2] = {};
            attributes[0].format = WGPUVertexFormat_Float32x4;
            attributes[0].offset = kGeometryPositionOffset;
            attributes[0].shaderLocation = 0;
            attributes[1].format = WGPUVertexFormat_Float32x4;
            attributes[1].offset = kGeometryColorOffset;
            attributes[1].shaderLocation = 1;

            WGPUVertexBufferLayout vertexBufferLayout{};
            vertexBufferLayout.arrayStride = kGeometryVertexSize;
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
            depthState.format = kDepthFormat;
            depthState.depthWriteEnabled = WGPUOptionalBool_True;
            depthState.depthCompare = depthCompare;
            depthState.stencilFront.compare = WGPUCompareFunction_Always;
            depthState.stencilFront.failOp = WGPUStencilOperation_Keep;
            depthState.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
            depthState.stencilFront.passOp = WGPUStencilOperation_Keep;
            depthState.stencilBack = depthState.stencilFront;

            WGPURenderPipelineDescriptor pipelineDesc{};
            pipelineDesc.label = {label, WGPU_STRLEN};
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

            outPipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &pipelineDesc);
            return outPipeline != nullptr;
        }

        void InitializeModelMatrices()
        {
            for (int x = 0; x < kXCount; ++x)
            {
                for (int y = 0; y < kYCount; ++y)
                {
                    const int m = x * kYCount + y;
                    const float fz = -800.f * static_cast<float>(m);
                    const float s = 1.f + 50.f * static_cast<float>(m);

                    vec3 translation(
                        static_cast<float>(x) - kXCount / 2.f + 0.5f,
                        (4.f - 0.2f * fz) * (static_cast<float>(y) - kYCount / 2.f + 1.f),
                        fz);

                    mat4 model = glm::translate(mat4(1.f), translation);
                    model = glm::scale(model, vec3(s, s, s));
                    m_modelMatrices[m] = model;
                }
            }
        }

        void UpdateCameraMatrices(pwgpu::test::SampleContext &ctx)
        {
            const float aspect = ctx.height > 0 ? (0.5f * static_cast<float>(ctx.width)) /
                                                      static_cast<float>(ctx.height)
                                                : 1.f;
            mat4 projection = glm::perspectiveRH_ZO((2.f * kPi) / 5.f, aspect, 5.f, 9999.f);
            projection[1][1] *= -1.f;

            mat4 view = glm::translate(mat4(1.f), vec3(0.f, 0.f, -12.f));
            mat4 viewProj = projection * view;

            // depthRangeRemapMatrix: remaps [0,1] -> [1,0]. In GLM column-major
            // storage, m[col][row], so z-scale sits at m[2][2] and the
            // translation at m[3][2].
            mat4 depthRangeRemap(1.f);
            depthRangeRemap[2][2] = -1.f;
            depthRangeRemap[3][2] = 1.f;

            mat4 reversedViewProj = depthRangeRemap * viewProj;

            wgpuQueueWriteBuffer(
                ctx.queue, m_cameraBuffer, 0, glm::value_ptr(viewProj), kCameraUniformBytes);
            wgpuQueueWriteBuffer(ctx.queue,
                                 m_cameraReversedBuffer,
                                 0,
                                 glm::value_ptr(reversedViewProj),
                                 kCameraUniformBytes);
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

        static void ReleaseShader(WGPUShaderModule &shader)
        {
            if (shader)
            {
                wgpuShaderModuleRelease(shader);
                shader = nullptr;
            }
        }

        WGPUShaderModule m_vertexShader = nullptr;
        WGPUShaderModule m_pixelShader = nullptr;
        WGPUShaderModule m_depthPrePassVs = nullptr;
        WGPUShaderModule m_precisionErrorVs = nullptr;
        WGPUShaderModule m_precisionErrorPs = nullptr;
        WGPUShaderModule m_textureQuadVs = nullptr;
        WGPUShaderModule m_textureQuadPs = nullptr;
        WGPUBuffer m_vertexBuffer = nullptr;
        WGPUBuffer m_modelUniformBuffer = nullptr;
        WGPUBuffer m_cameraBuffer = nullptr;
        WGPUBuffer m_cameraReversedBuffer = nullptr;
        WGPUBindGroupLayout m_bindGroupLayout = nullptr;
        WGPUPipelineLayout m_pipelineLayout = nullptr;
        std::array<WGPUBindGroup, kDepthModeCount> m_bindGroups{};
        std::array<WGPURenderPipeline, kDepthModeCount> m_pipelines{};
        WGPUTexture m_depthTexture = nullptr;
        WGPUTextureView m_depthView = nullptr;
        std::array<mat4, kNumInstances> m_modelMatrices{};
        std::array<float, kNumInstances * kMatrixFloatCount> m_modelData{};
    };

    pwgpu::test::SampleAppDesc MakeReversedZDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "ReversedZTest";
        desc.windowTitle = "Phasma WebGPU ReversedZ";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    pe::Log::Init();
    setvbuf(stdout, nullptr, _IONBF, 0);

    ReversedZSample sample;
    pwgpu::test::SampleApp app(sample, MakeReversedZDesc());
    return app.Run(argc, argv);
}
