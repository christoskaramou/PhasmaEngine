#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb/stb_image.h"

#include "../Common/SampleApp.h"
#include "../Common/SampleBase.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"

#include <algorithm>
#include <cstdio>

namespace
{
    // Upstream initial HTML control values.
    constexpr uint32_t kCanvasSize = 512;
    constexpr uint32_t kImageSize = 256;
    constexpr uint32_t kSampleCount = 4; // multisampling.checked
    constexpr uint32_t kPixels = 65536;
    constexpr uint32_t kDrawCallsSlider = 0;
    constexpr uint32_t kMultiplier = 1;

    struct LoadedImage
    {
        uint8_t *pixels = nullptr;
        int width = 0;
        int height = 0;
    };

    bool LoadImageRGBA8(const std::string &path, LoadedImage &image)
    {
        int channels = 0;
        image.pixels = stbi_load(path.c_str(), &image.width, &image.height, &channels, 4);
        if (!image.pixels)
        {
            fprintf(stderr,
                    "[WorkloadSimulator] Failed to load %s: %s\n",
                    path.c_str(),
                    stbi_failure_reason());
            return false;
        }

        return true;
    }

    class WorkloadSimulatorSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            LoadedImage image{};
            const std::string imagePath = pwgpu::test::GetAssetPath(ctx.exeDir, "webgpu.png");
            if (!LoadImageRGBA8(imagePath, image))
            {
                fprintf(stderr, "        Place webgpu.png next to the executable.\n");
                return false;
            }

            m_vertexShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "workload.vert.hlsl").c_str(), "workload_vs");
            m_pixelShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "workload.pixel.hlsl").c_str(), "workload_ps");
            if (!m_vertexShader || !m_pixelShader)
            {
                stbi_image_free(image.pixels);
                return false;
            }

            if (!CreateTextureResources(ctx, image))
            {
                stbi_image_free(image.pixels);
                return false;
            }
            stbi_image_free(image.pixels);

            m_uniformBuffer = pwgpu::test::CreateBuffer(
                ctx.device, 16, WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, "workload_ubo");
            if (!m_uniformBuffer)
                return false;

            if (!CreatePipelineResources(ctx))
                return false;

            Resize(ctx, ctx.width, ctx.height);
            return m_msaaTexture != nullptr && m_msaaView != nullptr;
        }

        void Resize(pwgpu::test::SampleContext &ctx, uint32_t width, uint32_t height) override
        {
            ReleaseMsaaTarget();
            if (width == 0 || height == 0)
                return;

            WGPUTextureDescriptor textureDesc{};
            textureDesc.label = {"workload_msaa", WGPU_STRLEN};
            textureDesc.dimension = WGPUTextureDimension_2D;
            textureDesc.size = {width, height, 1};
            textureDesc.mipLevelCount = 1;
            textureDesc.sampleCount = kSampleCount;
            textureDesc.format = ctx.surfaceFormat;
            textureDesc.usage = WGPUTextureUsage_RenderAttachment;
            m_msaaTexture = wgpuDeviceCreateTexture(ctx.device, &textureDesc);
            if (!m_msaaTexture)
                return;

            m_msaaView = pwgpu::test::CreateTextureView(m_msaaTexture, ctx.surfaceFormat);
        }

        void Update(pwgpu::test::SampleContext &ctx) override
        {
            const float size = static_cast<float>(std::max<uint32_t>(ctx.width, 1));
            const float positionX = (static_cast<float>(ctx.width) - kImageSize) * 0.5f;
            const float positionY = (static_cast<float>(ctx.height) - kImageSize) * 0.5f;
            const float uniforms[4] = {
                positionX / size * 2.0f - 1.0f,
                1.0f - positionY * 2.0f / size,
                0.0f,
                0.0f,
            };
            wgpuQueueWriteBuffer(ctx.queue, m_uniformBuffer, 0, uniforms, sizeof(uniforms));
        }

        bool Execute(pwgpu::test::SampleContext &, pwgpu::test::SampleFrame &frame) override
        {
            if (!m_msaaView)
                return true;

            WGPURenderPassColorAttachment attachment{};
            attachment.view = m_msaaView;
            attachment.resolveTarget = frame.surfaceView;
            attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            attachment.loadOp = WGPULoadOp_Clear;
            attachment.storeOp = WGPUStoreOp_Discard;
            attachment.clearValue = {1.0, 1.0, 1.0, 1.0};

            WGPURenderPassDescriptor renderPassDesc{};
            renderPassDesc.label = {"workload_render_pass", WGPU_STRLEN};
            renderPassDesc.colorAttachmentCount = 1;
            renderPassDesc.colorAttachments = &attachment;

            WGPURenderPassEncoder pass =
                wgpuCommandEncoderBeginRenderPass(frame.encoder, &renderPassDesc);
            wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, m_bindGroup, 0, nullptr);

            const uint32_t numDrawCalls = std::max(1u, kDrawCallsSlider * kMultiplier);
            const uint32_t instances = (kPixels / 64000u) * kMultiplier;
            const uint32_t instancesPerCall = std::max(1u, instances / numDrawCalls);
            const uint32_t instancesFirstCall =
                instancesPerCall + numDrawCalls % instancesPerCall;

            uint32_t instancesDrawn = instancesFirstCall;
            bool scissorEnabled = false;
            wgpuRenderPassEncoderDraw(pass, 6, instancesFirstCall, 0, 0);
            for (uint32_t i = 1; i < numDrawCalls; ++i)
            {
                if (!scissorEnabled && instancesDrawn > instances)
                {
                    scissorEnabled = true;
                    wgpuRenderPassEncoderSetScissorRect(
                        pass,
                        (kCanvasSize - kImageSize) / 2,
                        (kCanvasSize - kImageSize) / 2,
                        2,
                        2);
                }
                wgpuRenderPassEncoderDraw(pass, 6, instancesPerCall, 0, 0);
                instancesDrawn += instancesPerCall;
            }

            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            ReleaseMsaaTarget();

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
            if (m_sampler)
            {
                wgpuSamplerRelease(m_sampler);
                m_sampler = nullptr;
            }
            if (m_textureView)
            {
                wgpuTextureViewRelease(m_textureView);
                m_textureView = nullptr;
            }
            if (m_texture)
            {
                wgpuTextureRelease(m_texture);
                m_texture = nullptr;
            }
            if (m_uniformBuffer)
            {
                wgpuBufferRelease(m_uniformBuffer);
                m_uniformBuffer = nullptr;
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
        }

    private:
        bool CreateTextureResources(pwgpu::test::SampleContext &ctx, const LoadedImage &image)
        {
            WGPUTextureDescriptor textureDesc{};
            textureDesc.label = {"workload_logo_tex", WGPU_STRLEN};
            textureDesc.dimension = WGPUTextureDimension_2D;
            textureDesc.size = {static_cast<uint32_t>(image.width),
                                static_cast<uint32_t>(image.height),
                                1};
            textureDesc.mipLevelCount = 1;
            textureDesc.sampleCount = 1;
            textureDesc.format = WGPUTextureFormat_RGBA8Unorm;
            textureDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst |
                                WGPUTextureUsage_RenderAttachment;
            m_texture = wgpuDeviceCreateTexture(ctx.device, &textureDesc);
            if (!m_texture)
                return false;

            pwgpu::test::UploadRGBA8Texture(ctx.queue,
                                            m_texture,
                                            image.pixels,
                                            static_cast<uint32_t>(image.width),
                                            static_cast<uint32_t>(image.height));

            m_textureView =
                pwgpu::test::CreateTextureView(m_texture, WGPUTextureFormat_RGBA8Unorm);
            if (!m_textureView)
                return false;

            WGPUSamplerDescriptor samplerDesc{};
            samplerDesc.label = {"workload_sampler", WGPU_STRLEN};
            samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
            samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
            samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
            samplerDesc.magFilter = WGPUFilterMode_Linear;
            samplerDesc.minFilter = WGPUFilterMode_Linear;
            samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
            samplerDesc.lodMinClamp = 0.0f;
            samplerDesc.lodMaxClamp = 32.0f;
            samplerDesc.compare = WGPUCompareFunction_Undefined;
            samplerDesc.maxAnisotropy = 1;
            m_sampler = wgpuDeviceCreateSampler(ctx.device, &samplerDesc);
            return m_sampler != nullptr;
        }

        bool CreatePipelineResources(pwgpu::test::SampleContext &ctx)
        {
            WGPUBindGroupLayoutEntry layoutEntries[3] = {};
            layoutEntries[0].binding = 0;
            layoutEntries[0].visibility = WGPUShaderStage_Vertex;
            layoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
            layoutEntries[0].buffer.minBindingSize = 16;
            layoutEntries[1].binding = 1;
            layoutEntries[1].visibility = WGPUShaderStage_Fragment;
            layoutEntries[1].sampler.type = WGPUSamplerBindingType_Filtering;
            layoutEntries[2].binding = 2;
            layoutEntries[2].visibility = WGPUShaderStage_Fragment;
            layoutEntries[2].texture.sampleType = WGPUTextureSampleType_Float;
            layoutEntries[2].texture.viewDimension = WGPUTextureViewDimension_2D;
            layoutEntries[2].texture.multisampled = WGPUOptionalBool_False;

            WGPUBindGroupLayoutDescriptor layoutDesc{};
            layoutDesc.label = {"workload_bgl", WGPU_STRLEN};
            layoutDesc.entryCount = 3;
            layoutDesc.entries = layoutEntries;
            m_bindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device, &layoutDesc);
            if (!m_bindGroupLayout)
                return false;

            WGPUPipelineLayoutDescriptor pipelineLayoutDesc{};
            pipelineLayoutDesc.label = {"workload_pl", WGPU_STRLEN};
            pipelineLayoutDesc.bindGroupLayoutCount = 1;
            pipelineLayoutDesc.bindGroupLayouts = &m_bindGroupLayout;
            m_pipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device, &pipelineLayoutDesc);
            if (!m_pipelineLayout)
                return false;

            WGPUBindGroupEntry bindEntries[3] = {};
            bindEntries[0].binding = 0;
            bindEntries[0].buffer = m_uniformBuffer;
            bindEntries[0].size = 16;
            bindEntries[1].binding = 1;
            bindEntries[1].sampler = m_sampler;
            bindEntries[2].binding = 2;
            bindEntries[2].textureView = m_textureView;

            WGPUBindGroupDescriptor bindGroupDesc{};
            bindGroupDesc.label = {"workload_bg", WGPU_STRLEN};
            bindGroupDesc.layout = m_bindGroupLayout;
            bindGroupDesc.entryCount = 3;
            bindGroupDesc.entries = bindEntries;
            m_bindGroup = wgpuDeviceCreateBindGroup(ctx.device, &bindGroupDesc);
            if (!m_bindGroup)
                return false;

            WGPUColorTargetState colorTarget{};
            colorTarget.format = ctx.surfaceFormat;
            colorTarget.writeMask = WGPUColorWriteMask_All;

            WGPUFragmentState fragmentState{};
            fragmentState.module = m_pixelShader;
            fragmentState.entryPoint = {"PSMain", WGPU_STRLEN};
            fragmentState.targetCount = 1;
            fragmentState.targets = &colorTarget;

            WGPURenderPipelineDescriptor pipelineDesc{};
            pipelineDesc.label = {"workload_pipeline", WGPU_STRLEN};
            pipelineDesc.layout = m_pipelineLayout;
            pipelineDesc.vertex.module = m_vertexShader;
            pipelineDesc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            pipelineDesc.primitive.cullMode = WGPUCullMode_None;
            pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
            pipelineDesc.multisample.count = kSampleCount;
            pipelineDesc.multisample.mask = 0xFFFFFFFF;
            pipelineDesc.fragment = &fragmentState;
            m_pipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &pipelineDesc);
            return m_pipeline != nullptr;
        }

        void ReleaseMsaaTarget()
        {
            if (m_msaaView)
            {
                wgpuTextureViewRelease(m_msaaView);
                m_msaaView = nullptr;
            }
            if (m_msaaTexture)
            {
                wgpuTextureRelease(m_msaaTexture);
                m_msaaTexture = nullptr;
            }
        }

        WGPUShaderModule m_vertexShader = nullptr;
        WGPUShaderModule m_pixelShader = nullptr;
        WGPUBuffer m_uniformBuffer = nullptr;
        WGPUBindGroupLayout m_bindGroupLayout = nullptr;
        WGPUPipelineLayout m_pipelineLayout = nullptr;
        WGPUBindGroup m_bindGroup = nullptr;
        WGPURenderPipeline m_pipeline = nullptr;
        WGPUTexture m_texture = nullptr;
        WGPUTextureView m_textureView = nullptr;
        WGPUSampler m_sampler = nullptr;
        WGPUTexture m_msaaTexture = nullptr;
        WGPUTextureView m_msaaView = nullptr;
    };

    pwgpu::test::SampleAppDesc MakeWorkloadSimulatorDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "WorkloadSimulatorTest";
        desc.windowTitle = "PhasmaWebGPU WorkloadSimulator";
        desc.initialWidth = kCanvasSize;
        desc.initialHeight = kCanvasSize;
        desc.alphaMode = WGPUCompositeAlphaMode_Opaque;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    WorkloadSimulatorSample sample;
    pwgpu::test::SampleApp app(sample, MakeWorkloadSimulatorDesc());
    return app.Run(argc, argv);
}
