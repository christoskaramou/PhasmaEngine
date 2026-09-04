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

    constexpr float kCubeVertices[] = {
        1,
        1,
        -1,
        1,
        0,
        0,
        1,
        1,
        1,
        1,
        0,
        0,
        1,
        -1,
        1,
        1,
        0,
        0,
        1,
        -1,
        -1,
        1,
        0,
        0,
        -1,
        1,
        1,
        -1,
        0,
        0,
        -1,
        1,
        -1,
        -1,
        0,
        0,
        -1,
        -1,
        -1,
        -1,
        0,
        0,
        -1,
        -1,
        1,
        -1,
        0,
        0,
        -1,
        1,
        1,
        0,
        1,
        0,
        1,
        1,
        1,
        0,
        1,
        0,
        1,
        1,
        -1,
        0,
        1,
        0,
        -1,
        1,
        -1,
        0,
        1,
        0,
        -1,
        -1,
        -1,
        0,
        -1,
        0,
        1,
        -1,
        -1,
        0,
        -1,
        0,
        1,
        -1,
        1,
        0,
        -1,
        0,
        -1,
        -1,
        1,
        0,
        -1,
        0,
        1,
        1,
        1,
        0,
        0,
        1,
        -1,
        1,
        1,
        0,
        0,
        1,
        -1,
        -1,
        1,
        0,
        0,
        1,
        1,
        -1,
        1,
        0,
        0,
        1,
        -1,
        1,
        -1,
        0,
        0,
        -1,
        1,
        1,
        -1,
        0,
        0,
        -1,
        1,
        -1,
        -1,
        0,
        0,
        -1,
        -1,
        -1,
        -1,
        0,
        0,
        -1,
    };

    constexpr uint16_t kCubeIndices[] = {
        0,
        1,
        2,
        0,
        2,
        3,
        4,
        5,
        6,
        4,
        6,
        7,
        8,
        9,
        10,
        8,
        10,
        11,
        12,
        13,
        14,
        12,
        14,
        15,
        16,
        17,
        18,
        16,
        18,
        19,
        20,
        21,
        22,
        20,
        22,
        23,
    };

    constexpr int kNumObjects = 6;
    constexpr uint32_t kUniformSize = 144;

    struct ObjectInit
    {
        float position[3];
        float color[4];
    };

    constexpr ObjectInit kObjects[kNumObjects] = {
        {{-10, 0, 0}, {1, 0, 0, 1}},
        {{10, 0, 0}, {1, 1, 0, 1}},
        {{0, -10, 0}, {0, 0.5f, 0, 1}},
        {{0, 10, 0}, {1, 0.6f, 0, 1}},
        {{0, 0, -10}, {0, 0, 1, 1}},
        {{0, 0, 10}, {0.5f, 0, 0.5f, 1}},
    };

    class OcclusionQuerySample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            m_vertexShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "solidColorLit.vert.hlsl").c_str(), "lit_vs");
            m_pixelShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "solidColorLit.pixel.hlsl").c_str(), "lit_ps");
            if (!m_vertexShader || !m_pixelShader)
                return false;

            m_vertexBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                       sizeof(kCubeVertices),
                                                       WGPUBufferUsage_Vertex |
                                                           WGPUBufferUsage_CopyDst,
                                                       "vb");
            m_indexBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                      sizeof(kCubeIndices),
                                                      WGPUBufferUsage_Index |
                                                          WGPUBufferUsage_CopyDst,
                                                      "ib");
            if (!m_vertexBuffer || !m_indexBuffer)
                return false;

            wgpuQueueWriteBuffer(
                ctx.queue, m_vertexBuffer, 0, kCubeVertices, sizeof(kCubeVertices));
            wgpuQueueWriteBuffer(
                ctx.queue, m_indexBuffer, 0, kCubeIndices, sizeof(kCubeIndices));

            if (!CreatePipelineResources(ctx))
                return false;

            WGPUQuerySetDescriptor querySetDesc{};
            querySetDesc.label = {"oq_set", WGPU_STRLEN};
            querySetDesc.type = WGPUQueryType_Occlusion;
            querySetDesc.count = kNumObjects;
            m_querySet = wgpuDeviceCreateQuerySet(ctx.device, &querySetDesc);
            m_resolveBuffer = pwgpu::test::CreateBuffer(
                ctx.device,
                static_cast<uint64_t>(kNumObjects) * 8,
                WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc,
                "resolve");
            return m_querySet != nullptr && m_resolveBuffer != nullptr;
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
            mat4 projection =
                glm::perspectiveRH_ZO(30.f * kPi / 180.f, aspect, 0.5f, 100.f);
            projection[1][1] *= -1.f;

            float time = static_cast<float>(ctx.timing.elapsedSeconds);
            mat4 camera = mat4(1.f);
            camera = glm::rotate(camera, time, vec3(1.f, 0.f, 0.f));
            camera = glm::rotate(camera, time * 0.7f, vec3(0.f, 1.f, 0.f));
            float zPing = 0.5f * (sinf(time * 0.2f * 2.f * kPi) + 1.f);
            float cameraZ = 5.f + (40.f - 5.f) * zPing;
            camera = glm::translate(camera, vec3(0.f, 0.f, cameraZ));
            mat4 viewProjection = projection * glm::inverse(camera);

            for (int i = 0; i < kNumObjects; ++i)
            {
                mat4 world = glm::translate(mat4(1.f),
                                            vec3(kObjects[i].position[0],
                                                 kObjects[i].position[1],
                                                 kObjects[i].position[2]));
                mat4 worldViewProjection = viewProjection * world;
                mat4 worldInverseTranspose = glm::transpose(glm::inverse(world));

                float uniformData[36]{};
                memcpy(uniformData + 0, glm::value_ptr(worldViewProjection), 64);
                memcpy(uniformData + 16, glm::value_ptr(worldInverseTranspose), 64);
                memcpy(uniformData + 32, kObjects[i].color, 16);
                wgpuQueueWriteBuffer(ctx.queue,
                                     m_uniformBuffers[i],
                                     0,
                                     uniformData,
                                     kUniformSize);
            }
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
            colorAttachment.clearValue = {0.5, 0.5, 0.5, 1.0};

            WGPURenderPassDepthStencilAttachment depthAttachment{};
            depthAttachment.view = m_depthView;
            depthAttachment.depthClearValue = 1.0f;
            depthAttachment.depthLoadOp = WGPULoadOp_Clear;
            depthAttachment.depthStoreOp = WGPUStoreOp_Store;
            depthAttachment.depthReadOnly = WGPUOptionalBool_False;
            depthAttachment.stencilLoadOp = WGPULoadOp_Undefined;
            depthAttachment.stencilStoreOp = WGPUStoreOp_Undefined;
            depthAttachment.stencilReadOnly = WGPUOptionalBool_True;

            WGPURenderPassDescriptor renderPassDesc{};
            renderPassDesc.label = {"render_pass", WGPU_STRLEN};
            renderPassDesc.colorAttachmentCount = 1;
            renderPassDesc.colorAttachments = &colorAttachment;
            renderPassDesc.depthStencilAttachment = &depthAttachment;
            renderPassDesc.occlusionQuerySet = m_querySet;

            WGPURenderPassEncoder renderPass =
                wgpuCommandEncoderBeginRenderPass(frame.encoder, &renderPassDesc);
            wgpuRenderPassEncoderSetPipeline(renderPass, m_pipeline);
            wgpuRenderPassEncoderSetVertexBuffer(
                renderPass, 0, m_vertexBuffer, 0, sizeof(kCubeVertices));
            wgpuRenderPassEncoderSetIndexBuffer(renderPass,
                                                m_indexBuffer,
                                                WGPUIndexFormat_Uint16,
                                                0,
                                                sizeof(kCubeIndices));

            for (int i = 0; i < kNumObjects; ++i)
            {
                wgpuRenderPassEncoderSetBindGroup(renderPass, 0, m_bindGroups[i], 0, nullptr);
                wgpuRenderPassEncoderBeginOcclusionQuery(renderPass, static_cast<uint32_t>(i));
                wgpuRenderPassEncoderDrawIndexed(renderPass, 36, 1, 0, 0, 0);
                wgpuRenderPassEncoderEndOcclusionQuery(renderPass);
            }

            wgpuRenderPassEncoderEnd(renderPass);
            wgpuRenderPassEncoderRelease(renderPass);
            wgpuCommandEncoderResolveQuerySet(frame.encoder,
                                              m_querySet,
                                              0,
                                              kNumObjects,
                                              m_resolveBuffer,
                                              0);
            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            ReleaseDepthTarget();

            if (m_resolveBuffer)
                wgpuBufferRelease(m_resolveBuffer);
            if (m_querySet)
                wgpuQuerySetRelease(m_querySet);
            if (m_pipeline)
                wgpuRenderPipelineRelease(m_pipeline);
            if (m_pipelineLayout)
                wgpuPipelineLayoutRelease(m_pipelineLayout);
            if (m_bindGroupLayout)
                wgpuBindGroupLayoutRelease(m_bindGroupLayout);
            for (int i = 0; i < kNumObjects; ++i)
            {
                if (m_bindGroups[i])
                    wgpuBindGroupRelease(m_bindGroups[i]);
                if (m_uniformBuffers[i])
                    wgpuBufferRelease(m_uniformBuffers[i]);
                m_bindGroups[i] = nullptr;
                m_uniformBuffers[i] = nullptr;
            }
            if (m_indexBuffer)
                wgpuBufferRelease(m_indexBuffer);
            if (m_vertexBuffer)
                wgpuBufferRelease(m_vertexBuffer);
            if (m_pixelShader)
                wgpuShaderModuleRelease(m_pixelShader);
            if (m_vertexShader)
                wgpuShaderModuleRelease(m_vertexShader);

            m_resolveBuffer = nullptr;
            m_querySet = nullptr;
            m_pipeline = nullptr;
            m_pipelineLayout = nullptr;
            m_bindGroupLayout = nullptr;
            m_indexBuffer = nullptr;
            m_vertexBuffer = nullptr;
            m_pixelShader = nullptr;
            m_vertexShader = nullptr;
        }

    private:
        bool CreatePipelineResources(pwgpu::test::SampleContext &ctx)
        {
            WGPUBindGroupLayoutEntry bindGroupLayoutEntry{};
            bindGroupLayoutEntry.binding = 0;
            bindGroupLayoutEntry.visibility =
                WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
            bindGroupLayoutEntry.buffer.type = WGPUBufferBindingType_Uniform;
            bindGroupLayoutEntry.buffer.minBindingSize = kUniformSize;

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

            for (int i = 0; i < kNumObjects; ++i)
            {
                m_uniformBuffers[i] = pwgpu::test::CreateBuffer(
                    ctx.device,
                    kUniformSize,
                    WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
                    "ubo");
                if (!m_uniformBuffers[i])
                    return false;

                WGPUBindGroupEntry bindGroupEntry{};
                bindGroupEntry.binding = 0;
                bindGroupEntry.buffer = m_uniformBuffers[i];
                bindGroupEntry.size = kUniformSize;

                WGPUBindGroupDescriptor bindGroupDesc{};
                bindGroupDesc.label = {"bg", WGPU_STRLEN};
                bindGroupDesc.layout = m_bindGroupLayout;
                bindGroupDesc.entryCount = 1;
                bindGroupDesc.entries = &bindGroupEntry;
                m_bindGroups[i] = wgpuDeviceCreateBindGroup(ctx.device, &bindGroupDesc);
                if (!m_bindGroups[i])
                    return false;
            }

            WGPUVertexAttribute attributes[2] = {};
            attributes[0].format = WGPUVertexFormat_Float32x3;
            attributes[0].offset = 0;
            attributes[0].shaderLocation = 0;
            attributes[1].format = WGPUVertexFormat_Float32x3;
            attributes[1].offset = 12;
            attributes[1].shaderLocation = 1;

            WGPUVertexBufferLayout vertexBufferLayout{};
            vertexBufferLayout.arrayStride = 6 * 4;
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
            pipelineDesc.label = {"oq_pipe", WGPU_STRLEN};
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
            return m_pipeline != nullptr;
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
        WGPUBuffer m_indexBuffer = nullptr;
        WGPUBuffer m_uniformBuffers[kNumObjects]{};
        WGPUBindGroup m_bindGroups[kNumObjects]{};
        WGPUBindGroupLayout m_bindGroupLayout = nullptr;
        WGPUPipelineLayout m_pipelineLayout = nullptr;
        WGPURenderPipeline m_pipeline = nullptr;
        WGPUQuerySet m_querySet = nullptr;
        WGPUBuffer m_resolveBuffer = nullptr;
        WGPUTexture m_depthTexture = nullptr;
        WGPUTextureView m_depthView = nullptr;
    };

    pwgpu::test::SampleAppDesc MakeOcclusionQueryDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "OcclusionQueryTest";
        desc.windowTitle = "Phasma WebGPU Occlusion Query";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    OcclusionQuerySample sample;
    pwgpu::test::SampleApp app(sample, MakeOcclusionQueryDesc());
    return app.Run(argc, argv);
}
