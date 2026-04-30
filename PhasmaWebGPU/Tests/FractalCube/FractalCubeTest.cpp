#include "../Common/CubeGeometry.h"
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

    class FractalCubeSample final : public pwgpu::test::SampleBase
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

            const WGPUSamplerDescriptor samplerDesc = MakeSamplerDesc();
            m_sampler = wgpuDeviceCreateSampler(ctx.device, &samplerDesc);
            m_vertexBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                       sizeof(pwgpu::test::cube::kVertices),
                                                       WGPUBufferUsage_Vertex |
                                                           WGPUBufferUsage_CopyDst,
                                                       "cube_vb");
            m_uniformBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                        64,
                                                        WGPUBufferUsage_Uniform |
                                                            WGPUBufferUsage_CopyDst,
                                                        "cube_ubo");
            if (!m_sampler || !m_vertexBuffer || !m_uniformBuffer)
                return false;

            wgpuQueueWriteBuffer(ctx.queue,
                                 m_vertexBuffer,
                                 0,
                                 pwgpu::test::cube::kVertices,
                                 sizeof(pwgpu::test::cube::kVertices));

            if (!CreatePipelineResources(ctx))
                return false;

            return true;
        }

        void Resize(pwgpu::test::SampleContext &ctx, uint32_t width, uint32_t height) override
        {
            ReleaseResizeTargets();
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

            WGPUTextureDescriptor cubeTextureDesc{};
            cubeTextureDesc.label = {"cube_self", WGPU_STRLEN};
            cubeTextureDesc.dimension = WGPUTextureDimension_2D;
            cubeTextureDesc.size = {width, height, 1};
            cubeTextureDesc.mipLevelCount = 1;
            cubeTextureDesc.sampleCount = 1;
            cubeTextureDesc.format = ctx.surfaceFormat;
            cubeTextureDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
            m_feedbackTexture = wgpuDeviceCreateTexture(ctx.device, &cubeTextureDesc);
            if (!m_feedbackTexture)
                return;

            m_feedbackView =
                pwgpu::test::CreateTextureView(m_feedbackTexture, ctx.surfaceFormat);
            if (!m_feedbackView)
                return;

            RebuildBindGroup(ctx.device);
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

        bool Execute(pwgpu::test::SampleContext &ctx,
                     pwgpu::test::SampleFrame &frame) override
        {
            if (!m_depthView || !m_bindGroup || !m_feedbackTexture)
                return true;

            WGPURenderPassEncoder renderPass =
                BeginRenderPass(frame, "render_pass", {0.5, 0.5, 0.5, 1.0}, m_depthView);
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

            WGPUTexelCopyTextureInfo source{};
            source.texture = frame.surfaceTexture.texture;
            source.aspect = WGPUTextureAspect_All;

            WGPUTexelCopyTextureInfo destination{};
            destination.texture = m_feedbackTexture;
            destination.aspect = WGPUTextureAspect_All;

            WGPUExtent3D copySize{ctx.width, ctx.height, 1};
            wgpuCommandEncoderCopyTextureToTexture(frame.encoder,
                                                   &source,
                                                   &destination,
                                                   &copySize);
            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            ReleaseResizeTargets();

            if (m_pipeline)
                wgpuRenderPipelineRelease(m_pipeline);
            if (m_pipelineLayout)
                wgpuPipelineLayoutRelease(m_pipelineLayout);
            if (m_bindGroup)
                wgpuBindGroupRelease(m_bindGroup);
            if (m_bindGroupLayout)
                wgpuBindGroupLayoutRelease(m_bindGroupLayout);
            if (m_sampler)
                wgpuSamplerRelease(m_sampler);
            if (m_pixelShader)
                wgpuShaderModuleRelease(m_pixelShader);
            if (m_vertexShader)
                wgpuShaderModuleRelease(m_vertexShader);
            if (m_uniformBuffer)
                wgpuBufferRelease(m_uniformBuffer);
            if (m_vertexBuffer)
                wgpuBufferRelease(m_vertexBuffer);

            m_pipeline = nullptr;
            m_pipelineLayout = nullptr;
            m_bindGroup = nullptr;
            m_bindGroupLayout = nullptr;
            m_sampler = nullptr;
            m_pixelShader = nullptr;
            m_vertexShader = nullptr;
            m_uniformBuffer = nullptr;
            m_vertexBuffer = nullptr;
        }

    private:
        static WGPUSamplerDescriptor MakeSamplerDesc()
        {
            WGPUSamplerDescriptor desc{};
            desc.label = {"linear", WGPU_STRLEN};
            desc.addressModeU = WGPUAddressMode_ClampToEdge;
            desc.addressModeV = WGPUAddressMode_ClampToEdge;
            desc.addressModeW = WGPUAddressMode_ClampToEdge;
            desc.magFilter = WGPUFilterMode_Linear;
            desc.minFilter = WGPUFilterMode_Linear;
            desc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
            desc.lodMinClamp = 0.f;
            desc.lodMaxClamp = 32.f;
            desc.maxAnisotropy = 1;
            return desc;
        }

        bool CreatePipelineResources(pwgpu::test::SampleContext &ctx)
        {
            WGPUBindGroupLayoutEntry entries[3] = {};
            entries[0].binding = 0;
            entries[0].visibility = WGPUShaderStage_Vertex;
            entries[0].buffer.type = WGPUBufferBindingType_Uniform;
            entries[0].buffer.minBindingSize = 64;
            entries[1].binding = 1;
            entries[1].visibility = WGPUShaderStage_Fragment;
            entries[1].sampler.type = WGPUSamplerBindingType_Filtering;
            entries[2].binding = 2;
            entries[2].visibility = WGPUShaderStage_Fragment;
            entries[2].texture.sampleType = WGPUTextureSampleType_Float;
            entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

            WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc{};
            bindGroupLayoutDesc.label = {"bgl", WGPU_STRLEN};
            bindGroupLayoutDesc.entryCount = 3;
            bindGroupLayoutDesc.entries = entries;
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
            pipelineDesc.label = {"pipe", WGPU_STRLEN};
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

        void RebuildBindGroup(WGPUDevice device)
        {
            if (m_bindGroup)
            {
                wgpuBindGroupRelease(m_bindGroup);
                m_bindGroup = nullptr;
            }

            if (!m_bindGroupLayout || !m_uniformBuffer || !m_sampler || !m_feedbackView)
                return;

            WGPUBindGroupEntry entries[3] = {};
            entries[0].binding = 0;
            entries[0].buffer = m_uniformBuffer;
            entries[0].size = 64;
            entries[1].binding = 1;
            entries[1].sampler = m_sampler;
            entries[2].binding = 2;
            entries[2].textureView = m_feedbackView;

            WGPUBindGroupDescriptor bindGroupDesc{};
            bindGroupDesc.label = {"bg", WGPU_STRLEN};
            bindGroupDesc.layout = m_bindGroupLayout;
            bindGroupDesc.entryCount = 3;
            bindGroupDesc.entries = entries;
            m_bindGroup = wgpuDeviceCreateBindGroup(device, &bindGroupDesc);
        }

        void ReleaseResizeTargets()
        {
            if (m_bindGroup)
            {
                wgpuBindGroupRelease(m_bindGroup);
                m_bindGroup = nullptr;
            }
            if (m_feedbackView)
            {
                wgpuTextureViewRelease(m_feedbackView);
                m_feedbackView = nullptr;
            }
            if (m_feedbackTexture)
            {
                wgpuTextureRelease(m_feedbackTexture);
                m_feedbackTexture = nullptr;
            }
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
        WGPUSampler m_sampler = nullptr;
        WGPUBindGroupLayout m_bindGroupLayout = nullptr;
        WGPUPipelineLayout m_pipelineLayout = nullptr;
        WGPUBindGroup m_bindGroup = nullptr;
        WGPURenderPipeline m_pipeline = nullptr;
        WGPUTexture m_depthTexture = nullptr;
        WGPUTextureView m_depthView = nullptr;
        WGPUTexture m_feedbackTexture = nullptr;
        WGPUTextureView m_feedbackView = nullptr;
    };

    pwgpu::test::SampleAppDesc MakeFractalCubeDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "FractalCubeTest";
        desc.windowTitle = "PhasmaWebGPU FractalCube";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        desc.surfaceUsage =
            WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    FractalCubeSample sample;
    pwgpu::test::SampleApp app(sample, MakeFractalCubeDesc());
    return app.Run(argc, argv);
}
