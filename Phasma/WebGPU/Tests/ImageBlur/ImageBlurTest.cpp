#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb/stb_image.h"

#include "../Common/FrameCapture.h"
#include "../Common/SampleApp.h"
#include "../Common/SampleBase.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"

#include <cstring>

namespace
{
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    constexpr uint32_t kTileDim = 128;
    constexpr uint32_t kBatchY = 4;
    constexpr uint32_t kFilterSize = 15;
    constexpr uint32_t kIterations = 2;

    struct CaptureOptions
    {
        bool dumpFirstFrame = false;
        bool exitAfterDump = false;
    };

    bool HasArg(int argc, char *argv[], const char *arg)
    {
        for (int i = 1; i < argc; i++)
        {
            if (strcmp(argv[i], arg) == 0)
                return true;
        }

        return false;
    }

    CaptureOptions ParseCaptureOptions(int argc, char *argv[])
    {
        CaptureOptions options{};
        options.dumpFirstFrame = HasArg(argc, argv, "--dump-first-frame");
        options.exitAfterDump = HasArg(argc, argv, "--exit-after-dump");
        return options;
    }

    class ImageBlurSample final : public pwgpu::test::SampleBase
    {
    public:
        explicit ImageBlurSample(CaptureOptions captureOptions)
            : m_captureOptions(captureOptions)
        {
        }

        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            const std::string imagePath = pwgpu::test::GetAssetPath(ctx.exeDir, "Di-3d.png");
            int imageChannels = 0;
            stbi_uc *pixels =
                stbi_load(imagePath.c_str(), &m_imageWidth, &m_imageHeight, &imageChannels, 4);
            if (!pixels)
            {
                fprintf(stderr,
                        "[Image] Failed to load %s: %s\n",
                        imagePath.c_str(),
                        stbi_failure_reason());
                return false;
            }

            fprintf(stdout, "[Image] %dx%d loaded\n", m_imageWidth, m_imageHeight);

            m_blurShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "blur.comp.hlsl").c_str(), "blur_cs");
            m_quadVertexShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "quad.vert.hlsl").c_str(), "quad_vs");
            m_quadPixelShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "quad.pixel.hlsl").c_str(), "quad_ps");
            if (!m_blurShader || !m_quadVertexShader || !m_quadPixelShader)
            {
                stbi_image_free(pixels);
                return false;
            }

            m_sourceTexture = CreateImageTexture(ctx.device, m_imageWidth, m_imageHeight, "src_tex");
            if (!m_sourceTexture)
            {
                stbi_image_free(pixels);
                return false;
            }

            pwgpu::test::UploadRGBA8Texture(ctx.queue,
                                            m_sourceTexture,
                                            pixels,
                                            static_cast<uint32_t>(m_imageWidth),
                                            static_cast<uint32_t>(m_imageHeight));
            stbi_image_free(pixels);

            m_sourceView =
                pwgpu::test::CreateTextureView(m_sourceTexture, WGPUTextureFormat_RGBA8Unorm);
            if (!m_sourceView)
                return false;

            WGPUTextureUsage pingUsage = static_cast<WGPUTextureUsage>(
                WGPUTextureUsage_CopyDst | WGPUTextureUsage_StorageBinding |
                WGPUTextureUsage_TextureBinding);
            if (m_captureOptions.dumpFirstFrame)
            {
                pingUsage =
                    static_cast<WGPUTextureUsage>(pingUsage | WGPUTextureUsage_CopySrc);
            }

            for (int i = 0; i < 2; i++)
            {
                m_pingTextures[i] =
                    CreatePingTexture(ctx.device, m_imageWidth, m_imageHeight, pingUsage, i);
                if (!m_pingTextures[i])
                    return false;

                m_pingViews[i] =
                    pwgpu::test::CreateTextureView(m_pingTextures[i], WGPUTextureFormat_RGBA8Unorm);
                if (!m_pingViews[i])
                    return false;
            }

            m_flipBuffers[0] = CreateFlipBuffer(ctx.device, ctx.queue, 0);
            m_flipBuffers[1] = CreateFlipBuffer(ctx.device, ctx.queue, 1);
            if (!m_flipBuffers[0] || !m_flipBuffers[1])
                return false;

            m_paramsBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                       16,
                                                       WGPUBufferUsage_Uniform |
                                                           WGPUBufferUsage_CopyDst,
                                                       "blur_params");
            if (!m_paramsBuffer)
                return false;

            const uint32_t blockDim = kTileDim - kFilterSize;
            uint32_t paramsData[4] = {kFilterSize + 1u, blockDim, 0, 0};
            wgpuQueueWriteBuffer(ctx.queue, m_paramsBuffer, 0, paramsData, sizeof(paramsData));

            WGPUSamplerDescriptor samplerDesc{};
            samplerDesc.label = {"blur_sampler", WGPU_STRLEN};
            samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
            samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
            samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
            samplerDesc.magFilter = WGPUFilterMode_Linear;
            samplerDesc.minFilter = WGPUFilterMode_Linear;
            samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
            samplerDesc.lodMinClamp = 0.0f;
            samplerDesc.lodMaxClamp = 32.0f;
            samplerDesc.maxAnisotropy = 1;
            m_sampler = wgpuDeviceCreateSampler(ctx.device, &samplerDesc);
            if (!m_sampler)
                return false;

            if (!CreateComputeResources(ctx) || !CreateRenderResources(ctx))
                return false;

            fprintf(stdout, "[Pipeline] Blur CS + fullscreen quad created\n");
            return true;
        }

        bool Execute(pwgpu::test::SampleContext &ctx,
                     pwgpu::test::SampleFrame &frame) override
        {
            if (!m_blurPipeline || !m_renderPipeline || !m_showBindGroup)
                return true;

            const uint32_t blockDim = kTileDim - kFilterSize;
            auto ceilDiv = [](uint32_t a, uint32_t b) -> uint32_t
            { return (a + b - 1u) / b; };

            WGPUComputePassDescriptor computePassDesc{};
            computePassDesc.label = {"blur_pass", WGPU_STRLEN};
            WGPUComputePassEncoder computePass =
                wgpuCommandEncoderBeginComputePass(frame.encoder, &computePassDesc);
            wgpuComputePassEncoderSetPipeline(computePass, m_blurPipeline);
            wgpuComputePassEncoderSetBindGroup(computePass, 0, m_computeConstants, 0, nullptr);

            wgpuComputePassEncoderSetBindGroup(computePass, 1, m_blurBindGroups[0], 0, nullptr);
            wgpuComputePassEncoderDispatchWorkgroups(computePass,
                                                     ceilDiv(static_cast<uint32_t>(m_imageWidth),
                                                             blockDim),
                                                     ceilDiv(static_cast<uint32_t>(m_imageHeight),
                                                             kBatchY),
                                                     1);

            wgpuComputePassEncoderSetBindGroup(computePass, 1, m_blurBindGroups[1], 0, nullptr);
            wgpuComputePassEncoderDispatchWorkgroups(computePass,
                                                     ceilDiv(static_cast<uint32_t>(m_imageHeight),
                                                             blockDim),
                                                     ceilDiv(static_cast<uint32_t>(m_imageWidth),
                                                             kBatchY),
                                                     1);

            for (uint32_t i = 0; i + 1 < kIterations; i++)
            {
                wgpuComputePassEncoderSetBindGroup(computePass, 1, m_blurBindGroups[2], 0, nullptr);
                wgpuComputePassEncoderDispatchWorkgroups(computePass,
                                                         ceilDiv(static_cast<uint32_t>(m_imageWidth),
                                                                 blockDim),
                                                         ceilDiv(static_cast<uint32_t>(m_imageHeight),
                                                                 kBatchY),
                                                         1);

                wgpuComputePassEncoderSetBindGroup(computePass, 1, m_blurBindGroups[1], 0, nullptr);
                wgpuComputePassEncoderDispatchWorkgroups(computePass,
                                                         ceilDiv(static_cast<uint32_t>(m_imageHeight),
                                                                 blockDim),
                                                         ceilDiv(static_cast<uint32_t>(m_imageWidth),
                                                                 kBatchY),
                                                         1);
            }

            wgpuComputePassEncoderEnd(computePass);
            wgpuComputePassEncoderRelease(computePass);

            WGPURenderPassEncoder renderPass =
                BeginRenderPass(frame, "render_pass", {0.0, 0.0, 0.0, 1.0});
            wgpuRenderPassEncoderSetPipeline(renderPass, m_renderPipeline);
            wgpuRenderPassEncoderSetBindGroup(renderPass, 0, m_showBindGroup, 0, nullptr);
            wgpuRenderPassEncoderDraw(renderPass, 6, 1, 0, 0);
            wgpuRenderPassEncoderEnd(renderPass);
            wgpuRenderPassEncoderRelease(renderPass);
            return true;
        }

        bool AfterSubmit(pwgpu::test::SampleContext &ctx,
                         pwgpu::test::SampleFrame &frame) override
        {
            if (!m_captureOptions.dumpFirstFrame || m_firstFrameCaptureDone)
                return true;

            const std::string outputCapturePath =
                pwgpu::test::GetAssetPath(ctx.exeDir, "ImageBlurOutput.bmp");
            if (!pwgpu::test::DumpTextureToBmp(ctx.instance,
                                               ctx.device,
                                               ctx.queue,
                                               m_pingTextures[1],
                                               WGPUTextureFormat_RGBA8Unorm,
                                               static_cast<uint32_t>(m_imageWidth),
                                               static_cast<uint32_t>(m_imageHeight),
                                               outputCapturePath))
            {
                fprintf(stderr, "[Capture] Failed to dump first frame output\n");
            }

            const std::string surfaceCapturePath =
                pwgpu::test::GetAssetPath(ctx.exeDir, "ImageBlurSurface.bmp");
            if (!pwgpu::test::DumpTextureToBmp(ctx.instance,
                                               ctx.device,
                                               ctx.queue,
                                               frame.surfaceTexture.texture,
                                               ctx.surfaceFormat,
                                               ctx.width,
                                               ctx.height,
                                               surfaceCapturePath))
            {
                fprintf(stderr, "[Capture] Failed to dump first frame surface\n");
            }

            m_firstFrameCaptureDone = true;
            return !(m_captureOptions.dumpFirstFrame && m_captureOptions.exitAfterDump);
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            ReleaseResources();
        }

    private:
        static WGPUTexture CreateImageTexture(WGPUDevice device,
                                              int width,
                                              int height,
                                              const char *label)
        {
            WGPUTextureDescriptor desc{};
            desc.label = {label, WGPU_STRLEN};
            desc.dimension = WGPUTextureDimension_2D;
            desc.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
            desc.mipLevelCount = 1;
            desc.sampleCount = 1;
            desc.format = WGPUTextureFormat_RGBA8Unorm;
            desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
            return wgpuDeviceCreateTexture(device, &desc);
        }

        static WGPUTexture CreatePingTexture(WGPUDevice device,
                                             int width,
                                             int height,
                                             WGPUTextureUsage usage,
                                             int index)
        {
            WGPUTextureDescriptor desc{};
            desc.label = {index == 0 ? "ping0" : "ping1", WGPU_STRLEN};
            desc.dimension = WGPUTextureDimension_2D;
            desc.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
            desc.mipLevelCount = 1;
            desc.sampleCount = 1;
            desc.format = WGPUTextureFormat_RGBA8Unorm;
            desc.usage = usage;
            return wgpuDeviceCreateTexture(device, &desc);
        }

        static WGPUBuffer CreateFlipBuffer(WGPUDevice device, WGPUQueue queue, uint32_t value)
        {
            WGPUBuffer buffer = pwgpu::test::CreateBuffer(device,
                                                          16,
                                                          WGPUBufferUsage_Uniform |
                                                              WGPUBufferUsage_CopyDst,
                                                          value == 0 ? "flip0" : "flip1");
            if (!buffer)
                return nullptr;

            uint32_t data[4] = {value, 0, 0, 0};
            wgpuQueueWriteBuffer(queue, buffer, 0, data, sizeof(data));
            return buffer;
        }

        bool CreateComputeResources(pwgpu::test::SampleContext &ctx)
        {
            WGPUBindGroupLayoutEntry constantsEntries[2] = {};
            constantsEntries[0].binding = 0;
            constantsEntries[0].visibility = WGPUShaderStage_Compute;
            constantsEntries[0].sampler.type = WGPUSamplerBindingType_Filtering;
            constantsEntries[1].binding = 1;
            constantsEntries[1].visibility = WGPUShaderStage_Compute;
            constantsEntries[1].buffer.type = WGPUBufferBindingType_Uniform;
            constantsEntries[1].buffer.minBindingSize = 16;

            WGPUBindGroupLayoutDescriptor constantsLayoutDesc{};
            constantsLayoutDesc.label = {"blur_bgl0", WGPU_STRLEN};
            constantsLayoutDesc.entryCount = 2;
            constantsLayoutDesc.entries = constantsEntries;
            m_computeConstantsLayout =
                wgpuDeviceCreateBindGroupLayout(ctx.device, &constantsLayoutDesc);
            if (!m_computeConstantsLayout)
                return false;

            WGPUBindGroupLayoutEntry blurEntries[3] = {};
            blurEntries[0].binding = 1;
            blurEntries[0].visibility = WGPUShaderStage_Compute;
            blurEntries[0].texture.sampleType = WGPUTextureSampleType_Float;
            blurEntries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
            blurEntries[1].binding = 2;
            blurEntries[1].visibility = WGPUShaderStage_Compute;
            blurEntries[1].storageTexture.access = WGPUStorageTextureAccess_ReadWrite;
            blurEntries[1].storageTexture.format = WGPUTextureFormat_RGBA8Unorm;
            blurEntries[1].storageTexture.viewDimension = WGPUTextureViewDimension_2D;
            blurEntries[2].binding = 3;
            blurEntries[2].visibility = WGPUShaderStage_Compute;
            blurEntries[2].buffer.type = WGPUBufferBindingType_Uniform;
            blurEntries[2].buffer.minBindingSize = 16;

            WGPUBindGroupLayoutDescriptor blurLayoutDesc{};
            blurLayoutDesc.label = {"blur_bgl1", WGPU_STRLEN};
            blurLayoutDesc.entryCount = 3;
            blurLayoutDesc.entries = blurEntries;
            m_computeBlurLayout = wgpuDeviceCreateBindGroupLayout(ctx.device, &blurLayoutDesc);
            if (!m_computeBlurLayout)
                return false;

            WGPUBindGroupLayout layouts[2] = {m_computeConstantsLayout, m_computeBlurLayout};
            WGPUPipelineLayoutDescriptor pipelineLayoutDesc{};
            pipelineLayoutDesc.label = {"blur_pl", WGPU_STRLEN};
            pipelineLayoutDesc.bindGroupLayoutCount = 2;
            pipelineLayoutDesc.bindGroupLayouts = layouts;
            m_computeLayout = wgpuDeviceCreatePipelineLayout(ctx.device, &pipelineLayoutDesc);
            if (!m_computeLayout)
                return false;

            WGPUComputePipelineDescriptor pipelineDesc{};
            pipelineDesc.label = {"blur_cp", WGPU_STRLEN};
            pipelineDesc.layout = m_computeLayout;
            pipelineDesc.compute.module = m_blurShader;
            pipelineDesc.compute.entryPoint = {"CSMain", WGPU_STRLEN};
            m_blurPipeline = wgpuDeviceCreateComputePipeline(ctx.device, &pipelineDesc);
            if (!m_blurPipeline)
                return false;

            WGPUBindGroupEntry constantsGroupEntries[2] = {};
            constantsGroupEntries[0].binding = 0;
            constantsGroupEntries[0].sampler = m_sampler;
            constantsGroupEntries[1].binding = 1;
            constantsGroupEntries[1].buffer = m_paramsBuffer;
            constantsGroupEntries[1].size = 16;

            WGPUBindGroupDescriptor constantsGroupDesc{};
            constantsGroupDesc.layout = m_computeConstantsLayout;
            constantsGroupDesc.entryCount = 2;
            constantsGroupDesc.entries = constantsGroupEntries;
            m_computeConstants = wgpuDeviceCreateBindGroup(ctx.device, &constantsGroupDesc);
            if (!m_computeConstants)
                return false;

            m_blurBindGroups[0] = CreateBlurBindGroup(ctx.device,
                                                      m_sourceView,
                                                      m_pingViews[0],
                                                      m_flipBuffers[0]);
            m_blurBindGroups[1] = CreateBlurBindGroup(ctx.device,
                                                      m_pingViews[0],
                                                      m_pingViews[1],
                                                      m_flipBuffers[1]);
            m_blurBindGroups[2] = CreateBlurBindGroup(ctx.device,
                                                      m_pingViews[1],
                                                      m_pingViews[0],
                                                      m_flipBuffers[0]);
            return m_blurBindGroups[0] && m_blurBindGroups[1] && m_blurBindGroups[2];
        }

        bool CreateRenderResources(pwgpu::test::SampleContext &ctx)
        {
            WGPUBindGroupLayoutEntry renderEntries[2] = {};
            renderEntries[0].binding = 0;
            renderEntries[0].visibility = WGPUShaderStage_Fragment;
            renderEntries[0].sampler.type = WGPUSamplerBindingType_Filtering;
            renderEntries[1].binding = 1;
            renderEntries[1].visibility = WGPUShaderStage_Fragment;
            renderEntries[1].texture.sampleType = WGPUTextureSampleType_Float;
            renderEntries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

            WGPUBindGroupLayoutDescriptor renderLayoutDesc{};
            renderLayoutDesc.label = {"quad_bgl", WGPU_STRLEN};
            renderLayoutDesc.entryCount = 2;
            renderLayoutDesc.entries = renderEntries;
            m_renderLayout = wgpuDeviceCreateBindGroupLayout(ctx.device, &renderLayoutDesc);
            if (!m_renderLayout)
                return false;

            WGPUPipelineLayoutDescriptor pipelineLayoutDesc{};
            pipelineLayoutDesc.label = {"quad_pl", WGPU_STRLEN};
            pipelineLayoutDesc.bindGroupLayoutCount = 1;
            pipelineLayoutDesc.bindGroupLayouts = &m_renderLayout;
            m_renderPipelineLayout =
                wgpuDeviceCreatePipelineLayout(ctx.device, &pipelineLayoutDesc);
            if (!m_renderPipelineLayout)
                return false;

            WGPUColorTargetState colorTarget{};
            colorTarget.format = ctx.surfaceFormat;
            colorTarget.writeMask = WGPUColorWriteMask_All;

            WGPUFragmentState fragmentState{};
            fragmentState.module = m_quadPixelShader;
            fragmentState.entryPoint = {"PSMain", WGPU_STRLEN};
            fragmentState.targetCount = 1;
            fragmentState.targets = &colorTarget;

            WGPURenderPipelineDescriptor pipelineDesc{};
            pipelineDesc.label = {"quad_pipe", WGPU_STRLEN};
            pipelineDesc.layout = m_renderPipelineLayout;
            pipelineDesc.vertex.module = m_quadVertexShader;
            pipelineDesc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            pipelineDesc.primitive.cullMode = WGPUCullMode_None;
            pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
            pipelineDesc.multisample.count = 1;
            pipelineDesc.multisample.mask = 0xFFFFFFFF;
            pipelineDesc.fragment = &fragmentState;
            m_renderPipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &pipelineDesc);
            if (!m_renderPipeline)
                return false;

            WGPUBindGroupEntry showEntries[2] = {};
            showEntries[0].binding = 0;
            showEntries[0].sampler = m_sampler;
            showEntries[1].binding = 1;
            showEntries[1].textureView = m_pingViews[1];

            WGPUBindGroupDescriptor showGroupDesc{};
            showGroupDesc.layout = m_renderLayout;
            showGroupDesc.entryCount = 2;
            showGroupDesc.entries = showEntries;
            m_showBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &showGroupDesc);
            return m_showBindGroup != nullptr;
        }

        WGPUBindGroup CreateBlurBindGroup(WGPUDevice device,
                                          WGPUTextureView inputView,
                                          WGPUTextureView outputView,
                                          WGPUBuffer flipBuffer) const
        {
            WGPUBindGroupEntry entries[3] = {};
            entries[0].binding = 1;
            entries[0].textureView = inputView;
            entries[1].binding = 2;
            entries[1].textureView = outputView;
            entries[2].binding = 3;
            entries[2].buffer = flipBuffer;
            entries[2].size = 16;

            WGPUBindGroupDescriptor desc{};
            desc.layout = m_computeBlurLayout;
            desc.entryCount = 3;
            desc.entries = entries;
            return wgpuDeviceCreateBindGroup(device, &desc);
        }

        void ReleaseResources()
        {
            if (m_showBindGroup)
                wgpuBindGroupRelease(m_showBindGroup);
            if (m_renderPipeline)
                wgpuRenderPipelineRelease(m_renderPipeline);
            if (m_renderPipelineLayout)
                wgpuPipelineLayoutRelease(m_renderPipelineLayout);
            if (m_renderLayout)
                wgpuBindGroupLayoutRelease(m_renderLayout);

            for (WGPUBindGroup &bindGroup : m_blurBindGroups)
            {
                if (bindGroup)
                    wgpuBindGroupRelease(bindGroup);
                bindGroup = nullptr;
            }

            if (m_computeConstants)
                wgpuBindGroupRelease(m_computeConstants);
            if (m_blurPipeline)
                wgpuComputePipelineRelease(m_blurPipeline);
            if (m_computeLayout)
                wgpuPipelineLayoutRelease(m_computeLayout);
            if (m_computeBlurLayout)
                wgpuBindGroupLayoutRelease(m_computeBlurLayout);
            if (m_computeConstantsLayout)
                wgpuBindGroupLayoutRelease(m_computeConstantsLayout);

            if (m_sampler)
                wgpuSamplerRelease(m_sampler);
            if (m_paramsBuffer)
                wgpuBufferRelease(m_paramsBuffer);
            for (WGPUBuffer &buffer : m_flipBuffers)
            {
                if (buffer)
                    wgpuBufferRelease(buffer);
                buffer = nullptr;
            }

            for (int i = 0; i < 2; i++)
            {
                if (m_pingViews[i])
                    wgpuTextureViewRelease(m_pingViews[i]);
                if (m_pingTextures[i])
                    wgpuTextureRelease(m_pingTextures[i]);
                m_pingViews[i] = nullptr;
                m_pingTextures[i] = nullptr;
            }

            if (m_sourceView)
                wgpuTextureViewRelease(m_sourceView);
            if (m_sourceTexture)
                wgpuTextureRelease(m_sourceTexture);

            if (m_quadPixelShader)
                wgpuShaderModuleRelease(m_quadPixelShader);
            if (m_quadVertexShader)
                wgpuShaderModuleRelease(m_quadVertexShader);
            if (m_blurShader)
                wgpuShaderModuleRelease(m_blurShader);

            m_showBindGroup = nullptr;
            m_renderPipeline = nullptr;
            m_renderPipelineLayout = nullptr;
            m_renderLayout = nullptr;
            m_computeConstants = nullptr;
            m_blurPipeline = nullptr;
            m_computeLayout = nullptr;
            m_computeBlurLayout = nullptr;
            m_computeConstantsLayout = nullptr;
            m_sampler = nullptr;
            m_paramsBuffer = nullptr;
            m_sourceView = nullptr;
            m_sourceTexture = nullptr;
            m_quadPixelShader = nullptr;
            m_quadVertexShader = nullptr;
            m_blurShader = nullptr;
        }

        CaptureOptions m_captureOptions{};
        int m_imageWidth = 0;
        int m_imageHeight = 0;
        bool m_firstFrameCaptureDone = false;
        WGPUShaderModule m_blurShader = nullptr;
        WGPUShaderModule m_quadVertexShader = nullptr;
        WGPUShaderModule m_quadPixelShader = nullptr;
        WGPUTexture m_sourceTexture = nullptr;
        WGPUTextureView m_sourceView = nullptr;
        WGPUTexture m_pingTextures[2] = {nullptr, nullptr};
        WGPUTextureView m_pingViews[2] = {nullptr, nullptr};
        WGPUBuffer m_flipBuffers[2] = {nullptr, nullptr};
        WGPUBuffer m_paramsBuffer = nullptr;
        WGPUSampler m_sampler = nullptr;
        WGPUBindGroupLayout m_computeConstantsLayout = nullptr;
        WGPUBindGroupLayout m_computeBlurLayout = nullptr;
        WGPUPipelineLayout m_computeLayout = nullptr;
        WGPUComputePipeline m_blurPipeline = nullptr;
        WGPUBindGroup m_computeConstants = nullptr;
        WGPUBindGroup m_blurBindGroups[3] = {nullptr, nullptr, nullptr};
        WGPUBindGroupLayout m_renderLayout = nullptr;
        WGPUPipelineLayout m_renderPipelineLayout = nullptr;
        WGPURenderPipeline m_renderPipeline = nullptr;
        WGPUBindGroup m_showBindGroup = nullptr;
    };

    pwgpu::test::SampleAppDesc MakeImageBlurDesc(const CaptureOptions &captureOptions)
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "ImageBlurTest";
        desc.windowTitle = "Phasma WebGPU Image Blur";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        desc.requiredFeatures = {WGPUFeatureName_TextureFormatsTier2};
        desc.surfaceUsage = WGPUTextureUsage_RenderAttachment;
        if (captureOptions.dumpFirstFrame)
        {
            desc.surfaceUsage =
                static_cast<WGPUTextureUsage>(desc.surfaceUsage | WGPUTextureUsage_CopySrc);
        }
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    CaptureOptions captureOptions = ParseCaptureOptions(argc, argv);
    ImageBlurSample sample(captureOptions);
    pwgpu::test::SampleApp app(sample, MakeImageBlurDesc(captureOptions));
    return app.Run(argc, argv);
}
