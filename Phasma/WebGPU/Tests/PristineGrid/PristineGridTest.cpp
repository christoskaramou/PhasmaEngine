#include "../Common/SampleApp.h"
#include "../Common/SampleBase.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"

#include <cmath>

namespace
{
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    constexpr float kPi = 3.14159265f;

    struct Vertex
    {
        float px, py, pz;
        float u, v;
    };

    constexpr Vertex kVertices[4] = {
        {-20.f, -0.5f, -20.f, 0.f, 0.f},
        {20.f, -0.5f, -20.f, 200.f, 0.f},
        {-20.f, -0.5f, 20.f, 0.f, 200.f},
        {20.f, -0.5f, 20.f, 200.f, 200.f},
    };
    constexpr uint32_t kIndices[6] = {0, 1, 2, 1, 2, 3};

    struct CameraCB
    {
        float projection[16];
        float view[16];
    };

    struct GridArgsCB
    {
        float lineColor[4];
        float baseColor[4];
        float lineWidth[2];
        float _pad[2];
    };

    class PristineGridSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            m_vertexShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "pristineGrid.vert.hlsl").c_str(), "pg_vs");
            m_pixelShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "pristineGrid.pixel.hlsl").c_str(), "pg_ps");
            if (!m_vertexShader || !m_pixelShader)
                return false;

            m_vertexBuffer = pwgpu::test::CreateBuffer(
                ctx.device, sizeof(kVertices),
                WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst, "pg_vb");
            m_indexBuffer = pwgpu::test::CreateBuffer(
                ctx.device, sizeof(kIndices),
                WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst, "pg_ib");
            m_cameraBuffer = pwgpu::test::CreateBuffer(
                ctx.device, sizeof(CameraCB),
                WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, "pg_camera_ubo");
            m_gridArgsBuffer = pwgpu::test::CreateBuffer(
                ctx.device, sizeof(GridArgsCB),
                WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, "pg_grid_ubo");
            if (!m_vertexBuffer || !m_indexBuffer || !m_cameraBuffer || !m_gridArgsBuffer)
                return false;

            wgpuQueueWriteBuffer(ctx.queue, m_vertexBuffer, 0, kVertices, sizeof(kVertices));
            wgpuQueueWriteBuffer(ctx.queue, m_indexBuffer, 0, kIndices, sizeof(kIndices));

            GridArgsCB gridArgs{};
            gridArgs.lineColor[0] = 1.f;
            gridArgs.lineColor[1] = 1.f;
            gridArgs.lineColor[2] = 1.f;
            gridArgs.lineColor[3] = 1.f;
            gridArgs.baseColor[0] = 0.f;
            gridArgs.baseColor[1] = 0.f;
            gridArgs.baseColor[2] = 0.f;
            gridArgs.baseColor[3] = 1.f;
            gridArgs.lineWidth[0] = 0.05f;
            gridArgs.lineWidth[1] = 0.05f;
            wgpuQueueWriteBuffer(ctx.queue, m_gridArgsBuffer, 0, &gridArgs, sizeof(gridArgs));

            WGPUBindGroupLayoutEntry cameraEntry{};
            cameraEntry.binding = 0;
            cameraEntry.visibility = WGPUShaderStage_Vertex;
            cameraEntry.buffer.type = WGPUBufferBindingType_Uniform;
            cameraEntry.buffer.minBindingSize = sizeof(CameraCB);

            WGPUBindGroupLayoutDescriptor cameraBglDesc{};
            cameraBglDesc.label = {"pg_camera_bgl", WGPU_STRLEN};
            cameraBglDesc.entryCount = 1;
            cameraBglDesc.entries = &cameraEntry;
            m_cameraBgl = wgpuDeviceCreateBindGroupLayout(ctx.device, &cameraBglDesc);

            WGPUBindGroupLayoutEntry gridEntry{};
            gridEntry.binding = 0;
            gridEntry.visibility = WGPUShaderStage_Fragment;
            gridEntry.buffer.type = WGPUBufferBindingType_Uniform;
            gridEntry.buffer.minBindingSize = sizeof(GridArgsCB);

            WGPUBindGroupLayoutDescriptor gridBglDesc{};
            gridBglDesc.label = {"pg_grid_bgl", WGPU_STRLEN};
            gridBglDesc.entryCount = 1;
            gridBglDesc.entries = &gridEntry;
            m_gridBgl = wgpuDeviceCreateBindGroupLayout(ctx.device, &gridBglDesc);

            if (!m_cameraBgl || !m_gridBgl)
                return false;

            WGPUBindGroupLayout layouts[2] = {m_cameraBgl, m_gridBgl};
            WGPUPipelineLayoutDescriptor plDesc{};
            plDesc.label = {"pg_pl", WGPU_STRLEN};
            plDesc.bindGroupLayoutCount = 2;
            plDesc.bindGroupLayouts = layouts;
            m_pipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device, &plDesc);
            if (!m_pipelineLayout)
                return false;

            WGPUBindGroupEntry cameraBgEntry{};
            cameraBgEntry.binding = 0;
            cameraBgEntry.buffer = m_cameraBuffer;
            cameraBgEntry.size = sizeof(CameraCB);
            WGPUBindGroupDescriptor cameraBgDesc{};
            cameraBgDesc.label = {"pg_camera_bg", WGPU_STRLEN};
            cameraBgDesc.layout = m_cameraBgl;
            cameraBgDesc.entryCount = 1;
            cameraBgDesc.entries = &cameraBgEntry;
            m_cameraBg = wgpuDeviceCreateBindGroup(ctx.device, &cameraBgDesc);

            WGPUBindGroupEntry gridBgEntry{};
            gridBgEntry.binding = 0;
            gridBgEntry.buffer = m_gridArgsBuffer;
            gridBgEntry.size = sizeof(GridArgsCB);
            WGPUBindGroupDescriptor gridBgDesc{};
            gridBgDesc.label = {"pg_grid_bg", WGPU_STRLEN};
            gridBgDesc.layout = m_gridBgl;
            gridBgDesc.entryCount = 1;
            gridBgDesc.entries = &gridBgEntry;
            m_gridBg = wgpuDeviceCreateBindGroup(ctx.device, &gridBgDesc);

            if (!m_cameraBg || !m_gridBg)
                return false;

            WGPUVertexAttribute attrs[2] = {};
            attrs[0].format = WGPUVertexFormat_Float32x3;
            attrs[0].offset = 0;
            attrs[0].shaderLocation = 0;
            attrs[1].format = WGPUVertexFormat_Float32x2;
            attrs[1].offset = 12;
            attrs[1].shaderLocation = 1;

            WGPUVertexBufferLayout vbLayout{};
            vbLayout.arrayStride = sizeof(Vertex);
            vbLayout.stepMode = WGPUVertexStepMode_Vertex;
            vbLayout.attributeCount = 2;
            vbLayout.attributes = attrs;

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
            depthState.depthCompare = WGPUCompareFunction_LessEqual;
            depthState.stencilFront.compare = WGPUCompareFunction_Always;
            depthState.stencilFront.failOp = WGPUStencilOperation_Keep;
            depthState.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
            depthState.stencilFront.passOp = WGPUStencilOperation_Keep;
            depthState.stencilBack = depthState.stencilFront;

            WGPURenderPipelineDescriptor pipelineDesc{};
            pipelineDesc.label = {"pg_pipe", WGPU_STRLEN};
            pipelineDesc.layout = m_pipelineLayout;
            pipelineDesc.vertex.module = m_vertexShader;
            pipelineDesc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            pipelineDesc.vertex.bufferCount = 1;
            pipelineDesc.vertex.buffers = &vbLayout;
            pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            pipelineDesc.primitive.cullMode = WGPUCullMode_None;
            pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
            pipelineDesc.depthStencil = &depthState;
            pipelineDesc.multisample.count = 1;
            pipelineDesc.multisample.mask = 0xFFFFFFFF;
            pipelineDesc.fragment = &fragmentState;
            m_pipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &pipelineDesc);
            return m_pipeline != nullptr;
        }

        void Resize(pwgpu::test::SampleContext &ctx, uint32_t width, uint32_t height) override
        {
            ReleaseDepth();
            if (width == 0 || height == 0)
                return;

            WGPUTextureDescriptor depthDesc{};
            depthDesc.label = {"pg_depth", WGPU_STRLEN};
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
            mat4 projection = glm::perspectiveRH_ZO((2.f * kPi) / 5.f, aspect, 0.1f, 100.f);
            projection[1][1] *= -1.f;

            float time = static_cast<float>(ctx.timing.elapsedSeconds);
            vec3 eye = vec3(sinf(time * 0.3f) * 6.f, 3.f, cosf(time * 0.3f) * 6.f);
            mat4 view = glm::lookAtRH(eye, vec3(0.f, 0.f, 0.f), vec3(0.f, 1.f, 0.f));

            CameraCB cbuf{};
            std::memcpy(cbuf.projection, glm::value_ptr(projection), 64);
            std::memcpy(cbuf.view, glm::value_ptr(view), 64);
            wgpuQueueWriteBuffer(ctx.queue, m_cameraBuffer, 0, &cbuf, sizeof(cbuf));
        }

        bool Execute(pwgpu::test::SampleContext &, pwgpu::test::SampleFrame &frame) override
        {
            if (!m_depthView)
                return true;

            WGPURenderPassEncoder renderPass =
                BeginRenderPass(frame, "pg_pass", {0.0, 0.0, 0.2, 1.0}, m_depthView);
            wgpuRenderPassEncoderSetPipeline(renderPass, m_pipeline);
            wgpuRenderPassEncoderSetBindGroup(renderPass, 0, m_cameraBg, 0, nullptr);
            wgpuRenderPassEncoderSetBindGroup(renderPass, 1, m_gridBg, 0, nullptr);
            wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, m_vertexBuffer, 0, sizeof(kVertices));
            wgpuRenderPassEncoderSetIndexBuffer(renderPass, m_indexBuffer,
                                                WGPUIndexFormat_Uint32, 0, sizeof(kIndices));
            wgpuRenderPassEncoderDrawIndexed(renderPass, 6, 1, 0, 0, 0);
            wgpuRenderPassEncoderEnd(renderPass);
            wgpuRenderPassEncoderRelease(renderPass);
            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            ReleaseDepth();
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
            if (m_cameraBg)
            {
                wgpuBindGroupRelease(m_cameraBg);
                m_cameraBg = nullptr;
            }
            if (m_gridBg)
            {
                wgpuBindGroupRelease(m_gridBg);
                m_gridBg = nullptr;
            }
            if (m_cameraBgl)
            {
                wgpuBindGroupLayoutRelease(m_cameraBgl);
                m_cameraBgl = nullptr;
            }
            if (m_gridBgl)
            {
                wgpuBindGroupLayoutRelease(m_gridBgl);
                m_gridBgl = nullptr;
            }
            if (m_vertexShader)
            {
                wgpuShaderModuleRelease(m_vertexShader);
                m_vertexShader = nullptr;
            }
            if (m_pixelShader)
            {
                wgpuShaderModuleRelease(m_pixelShader);
                m_pixelShader = nullptr;
            }
            if (m_gridArgsBuffer)
            {
                wgpuBufferRelease(m_gridArgsBuffer);
                m_gridArgsBuffer = nullptr;
            }
            if (m_cameraBuffer)
            {
                wgpuBufferRelease(m_cameraBuffer);
                m_cameraBuffer = nullptr;
            }
            if (m_indexBuffer)
            {
                wgpuBufferRelease(m_indexBuffer);
                m_indexBuffer = nullptr;
            }
            if (m_vertexBuffer)
            {
                wgpuBufferRelease(m_vertexBuffer);
                m_vertexBuffer = nullptr;
            }
        }

    private:
        void ReleaseDepth()
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
        WGPUBuffer m_indexBuffer = nullptr;
        WGPUBuffer m_cameraBuffer = nullptr;
        WGPUBuffer m_gridArgsBuffer = nullptr;
        WGPUBindGroupLayout m_cameraBgl = nullptr;
        WGPUBindGroupLayout m_gridBgl = nullptr;
        WGPUBindGroup m_cameraBg = nullptr;
        WGPUBindGroup m_gridBg = nullptr;
        WGPUPipelineLayout m_pipelineLayout = nullptr;
        WGPURenderPipeline m_pipeline = nullptr;
        WGPUTexture m_depthTexture = nullptr;
        WGPUTextureView m_depthView = nullptr;
    };

    pwgpu::test::SampleAppDesc MakePristineGridDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "PristineGridTest";
        desc.windowTitle = "Phasma WebGPU Pristine Grid";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    PristineGridSample sample;
    pwgpu::test::SampleApp app(sample, MakePristineGridDesc());
    return app.Run(argc, argv);
}
