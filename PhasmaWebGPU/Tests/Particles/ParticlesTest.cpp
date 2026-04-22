#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb/stb_image.h"

#include "../Common/FrameCapture.h"
#include "../Common/SampleApp.h"
#include "../Common/SampleBase.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    constexpr int kNumParticles = 50000;
    constexpr float kPi = 3.14159265f;
    constexpr uint32_t kRowAlignment = 256;

    struct Particle
    {
        float position[3];
        float lifetime;
        float color[4];
        float velocity[3];
        float pad;
    };
    static_assert(sizeof(Particle) == 48);

    struct SimParams
    {
        float deltaTime;
        float brightnessFactor;
        uint32_t numParticles;
        uint32_t pad;
        uint32_t seed[4];
    };
    static_assert(sizeof(SimParams) == 32);

    struct RenderParams
    {
        float mvp[16];
        float right[3];
        float pad0;
        float up[3];
        float pad1;
    };
    static_assert(sizeof(RenderParams) == 96);

    int MipCount(int width, int height)
    {
        int count = 0;
        int size = std::max(width, height);
        while (size > 0)
        {
            count++;
            size >>= 1;
        }
        return count;
    }

    void UploadTextureLevel(WGPUQueue queue,
                            WGPUTexture texture,
                            uint32_t level,
                            const uint8_t *data,
                            int width,
                            int height)
    {
        const uint32_t unpaddedBytesPerRow = static_cast<uint32_t>(width) * 4u;
        const uint32_t paddedBytesPerRow =
            pwgpu::test::AlignTo(unpaddedBytesPerRow, kRowAlignment);

        std::vector<uint8_t> padded;
        const void *source = data;
        size_t sourceSize = static_cast<size_t>(unpaddedBytesPerRow) * static_cast<size_t>(height);

        if (paddedBytesPerRow != unpaddedBytesPerRow)
        {
            padded.assign(static_cast<size_t>(paddedBytesPerRow) * static_cast<size_t>(height), 0);
            for (int row = 0; row < height; row++)
            {
                memcpy(padded.data() + static_cast<size_t>(row) * paddedBytesPerRow,
                       data + static_cast<size_t>(row) * unpaddedBytesPerRow,
                       unpaddedBytesPerRow);
            }
            source = padded.data();
            sourceSize = padded.size();
        }

        WGPUTexelCopyTextureInfo destination{};
        destination.texture = texture;
        destination.mipLevel = level;
        destination.aspect = WGPUTextureAspect_All;

        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow = paddedBytesPerRow;
        layout.rowsPerImage = static_cast<uint32_t>(height);

        WGPUExtent3D size{static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
        wgpuQueueWriteTexture(queue, &destination, source, sourceSize, &layout, &size);
    }

    WGPUTexture BuildLogoTexture(WGPUDevice device,
                                 WGPUQueue queue,
                                 const uint8_t *pixels,
                                 int width,
                                 int height)
    {
        const int mipCount = MipCount(width, height);

        WGPUTextureDescriptor desc{};
        desc.label = {"logo_tex", WGPU_STRLEN};
        desc.dimension = WGPUTextureDimension_2D;
        desc.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
        desc.mipLevelCount = static_cast<uint32_t>(mipCount);
        desc.sampleCount = 1;
        desc.format = WGPUTextureFormat_RGBA8Unorm;
        desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        WGPUTexture texture = wgpuDeviceCreateTexture(device, &desc);
        if (!texture)
            return nullptr;

        UploadTextureLevel(queue, texture, 0, pixels, width, height);

        std::vector<float> weights(static_cast<size_t>(width) * static_cast<size_t>(height));
        for (int i = 0; i < width * height; i++)
            weights[static_cast<size_t>(i)] = pixels[static_cast<size_t>(i) * 4u + 3u] / 255.0f;

        int previousWidth = width;
        int previousHeight = height;
        for (int level = 1; level < mipCount; level++)
        {
            const int currentWidth = previousWidth > 1 ? previousWidth >> 1 : 1;
            const int currentHeight = previousHeight > 1 ? previousHeight >> 1 : 1;

            std::vector<float> nextWeights(static_cast<size_t>(currentWidth) *
                                           static_cast<size_t>(currentHeight));
            std::vector<uint8_t> mipPixels(static_cast<size_t>(currentWidth) *
                                           static_cast<size_t>(currentHeight) * 4u);

            for (int y = 0; y < currentHeight; y++)
            {
                for (int x = 0; x < currentWidth; x++)
                {
                    const float a = weights[static_cast<size_t>(2 * x) +
                                            static_cast<size_t>(2 * y) * previousWidth];
                    const float b = weights[static_cast<size_t>(2 * x + 1) +
                                            static_cast<size_t>(2 * y) * previousWidth];
                    const float c = weights[static_cast<size_t>(2 * x) +
                                            static_cast<size_t>(2 * y + 1) * previousWidth];
                    const float d = weights[static_cast<size_t>(2 * x + 1) +
                                            static_cast<size_t>(2 * y + 1) * previousWidth];
                    const float sum = a + b + c + d;

                    nextWeights[static_cast<size_t>(x) +
                                static_cast<size_t>(y) * currentWidth] = sum / 4.0f;

                    const float denom = sum > 0.0001f ? sum : 0.0001f;
                    uint8_t *pixel = &mipPixels[(static_cast<size_t>(x) +
                                                 static_cast<size_t>(y) * currentWidth) *
                                                4u];
                    pixel[0] = static_cast<uint8_t>(a / denom * 255.0f + 0.5f);
                    pixel[1] = static_cast<uint8_t>((a + b) / denom * 255.0f + 0.5f);
                    pixel[2] =
                        static_cast<uint8_t>((a + b + c) / denom * 255.0f + 0.5f);
                    pixel[3] = static_cast<uint8_t>(sum / denom * 255.0f + 0.5f);
                }
            }

            UploadTextureLevel(queue, texture, static_cast<uint32_t>(level), mipPixels.data(), currentWidth, currentHeight);
            weights = std::move(nextWeights);
            previousWidth = currentWidth;
            previousHeight = currentHeight;
        }

        return texture;
    }

    class ParticlesSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            const std::string logoPath = pwgpu::test::GetAssetPath(ctx.exeDir, "webgpu.png");

            int logoChannels = 0;
            stbi_uc *logoPixels =
                stbi_load(logoPath.c_str(), &m_logoWidth, &m_logoHeight, &logoChannels, 4);
            if (!logoPixels)
            {
                fprintf(stderr,
                        "[Logo] Failed to load %s: %s\n",
                        logoPath.c_str(),
                        stbi_failure_reason());
                fprintf(stderr, "       Place webgpu.png next to the executable.\n");
                return false;
            }

            fprintf(stdout,
                    "[Logo] %dx%d loaded - building probability map (%d mip levels)\n",
                    m_logoWidth,
                    m_logoHeight,
                    MipCount(m_logoWidth, m_logoHeight));

            m_computeShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "particles.comp.hlsl").c_str(), "particles_cs");
            m_vertexShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "particles.vert.hlsl").c_str(), "particles_vs");
            m_pixelShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "particles.pixel.hlsl").c_str(), "particles_ps");
            if (!m_computeShader || !m_vertexShader || !m_pixelShader)
            {
                stbi_image_free(logoPixels);
                return false;
            }

            m_logoTexture =
                BuildLogoTexture(ctx.device, ctx.queue, logoPixels, m_logoWidth, m_logoHeight);
            stbi_image_free(logoPixels);
            if (!m_logoTexture)
                return false;

            WGPUTextureViewDescriptor logoViewDesc{};
            logoViewDesc.format = WGPUTextureFormat_RGBA8Unorm;
            logoViewDesc.dimension = WGPUTextureViewDimension_2D;
            logoViewDesc.baseMipLevel = 0;
            logoViewDesc.mipLevelCount = static_cast<uint32_t>(MipCount(m_logoWidth, m_logoHeight));
            logoViewDesc.arrayLayerCount = 1;
            logoViewDesc.aspect = WGPUTextureAspect_All;
            m_logoView = wgpuTextureCreateView(m_logoTexture, &logoViewDesc);
            if (!m_logoView)
                return false;

            if (!CreateBuffers(ctx) || !CreatePipelines(ctx))
                return false;

            fprintf(stdout, "[Pipelines] Compute + Render created - %d particles\n", kNumParticles);
            return true;
        }

        void Update(pwgpu::test::SampleContext &ctx) override
        {
            SimParams simParams{};
            simParams.deltaTime = static_cast<float>(ctx.timing.deltaSeconds);
            simParams.brightnessFactor = 1.0f;
            simParams.numParticles = static_cast<uint32_t>(kNumParticles);
            simParams.seed[0] = static_cast<uint32_t>((uint64_t)rand() * rand());
            simParams.seed[1] = static_cast<uint32_t>((uint64_t)rand() * rand());
            simParams.seed[2] = static_cast<uint32_t>((uint64_t)rand() * rand());
            simParams.seed[3] = static_cast<uint32_t>((uint64_t)rand() * rand());
            wgpuQueueWriteBuffer(ctx.queue, m_simUniformBuffer, 0, &simParams, sizeof(simParams));

            const float aspect =
                ctx.height > 0 ? static_cast<float>(ctx.width) / static_cast<float>(ctx.height)
                               : 1.0f;
            mat4 projection =
                glm::perspectiveRH_ZO(2.f * kPi / 5.f, aspect, 1.f, 100.f);
            projection[1][1] *= -1.f;

            float theta = -kPi * 0.2f;
            mat4 view = glm::translate(mat4(1.f), vec3(0.f, 0.f, -3.f)) *
                        glm::rotate(mat4(1.f), theta, vec3(1.f, 0.f, 0.f));
            mat4 mvp = projection * view;
            vec3 cameraRight(view[0][0], view[1][0], view[2][0]);
            vec3 cameraUp(view[0][1], view[1][1], view[2][1]);

            RenderParams renderParams{};
            memcpy(renderParams.mvp, glm::value_ptr(mvp), sizeof(renderParams.mvp));
            renderParams.right[0] = cameraRight.x;
            renderParams.right[1] = cameraRight.y;
            renderParams.right[2] = cameraRight.z;
            renderParams.up[0] = cameraUp.x;
            renderParams.up[1] = cameraUp.y;
            renderParams.up[2] = cameraUp.z;
            wgpuQueueWriteBuffer(ctx.queue,
                                 m_renderUniformBuffer,
                                 0,
                                 &renderParams,
                                 sizeof(renderParams));
        }

        bool Execute(pwgpu::test::SampleContext &,
                     pwgpu::test::SampleFrame &frame) override
        {
            if (!m_computePipeline || !m_renderPipeline)
                return true;

            WGPUComputePassDescriptor computePassDesc{};
            computePassDesc.label = {"sim_pass", WGPU_STRLEN};
            WGPUComputePassEncoder computePass =
                wgpuCommandEncoderBeginComputePass(frame.encoder, &computePassDesc);
            wgpuComputePassEncoderSetPipeline(computePass, m_computePipeline);
            wgpuComputePassEncoderSetBindGroup(computePass, 0, m_computeBindGroup, 0, nullptr);
            const uint32_t groups = (static_cast<uint32_t>(kNumParticles) + 63u) / 64u;
            wgpuComputePassEncoderDispatchWorkgroups(computePass, groups, 1, 1);
            wgpuComputePassEncoderEnd(computePass);
            wgpuComputePassEncoderRelease(computePass);

            WGPURenderPassEncoder renderPass =
                BeginRenderPass(frame, "render_pass", {0.0, 0.0, 0.0, 1.0});
            wgpuRenderPassEncoderSetPipeline(renderPass, m_renderPipeline);
            wgpuRenderPassEncoderSetBindGroup(renderPass, 0, m_renderBindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetVertexBuffer(renderPass,
                                                 0,
                                                 m_particleBuffer,
                                                 0,
                                                 static_cast<uint64_t>(kNumParticles) *
                                                     sizeof(Particle));
            wgpuRenderPassEncoderSetVertexBuffer(
                renderPass, 1, m_quadVertexBuffer, 0, sizeof(kQuadVertices));
            wgpuRenderPassEncoderDraw(renderPass, 6, static_cast<uint32_t>(kNumParticles), 0, 0);
            wgpuRenderPassEncoderEnd(renderPass);
            wgpuRenderPassEncoderRelease(renderPass);
            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            ReleaseResources();
        }

    private:
        bool CreateBuffers(pwgpu::test::SampleContext &ctx)
        {
            m_particleBuffer = pwgpu::test::CreateBuffer(
                ctx.device,
                static_cast<uint64_t>(kNumParticles) * sizeof(Particle),
                WGPUBufferUsage_Vertex | WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
                "particles");
            if (!m_particleBuffer)
                return false;

            std::vector<Particle> initialParticles(kNumParticles);
            for (Particle &particle : initialParticles)
            {
                particle.position[0] = 0.f;
                particle.position[1] = 0.f;
                particle.position[2] = 0.f;
                particle.lifetime = 0.f;
                particle.color[0] = 1.f;
                particle.color[1] = 1.f;
                particle.color[2] = 1.f;
                particle.color[3] = 1.f;
                particle.velocity[0] = 0.f;
                particle.velocity[1] = 0.f;
                particle.velocity[2] = 0.f;
                particle.pad = 0.f;
            }
            wgpuQueueWriteBuffer(ctx.queue,
                                 m_particleBuffer,
                                 0,
                                 initialParticles.data(),
                                 initialParticles.size() * sizeof(Particle));

            m_quadVertexBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                           sizeof(kQuadVertices),
                                                           WGPUBufferUsage_Vertex |
                                                               WGPUBufferUsage_CopyDst,
                                                           "quad_vb");
            m_simUniformBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                           sizeof(SimParams),
                                                           WGPUBufferUsage_Uniform |
                                                               WGPUBufferUsage_CopyDst,
                                                           "sim_ubo");
            m_renderUniformBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                              sizeof(RenderParams),
                                                              WGPUBufferUsage_Uniform |
                                                                  WGPUBufferUsage_CopyDst,
                                                              "ren_ubo");
            if (!m_quadVertexBuffer || !m_simUniformBuffer || !m_renderUniformBuffer)
                return false;

            wgpuQueueWriteBuffer(
                ctx.queue, m_quadVertexBuffer, 0, kQuadVertices, sizeof(kQuadVertices));
            return true;
        }

        bool CreatePipelines(pwgpu::test::SampleContext &ctx)
        {
            WGPUBindGroupLayoutEntry computeEntries[3] = {};
            computeEntries[0].binding = 0;
            computeEntries[0].visibility = WGPUShaderStage_Compute;
            computeEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
            computeEntries[0].buffer.minBindingSize = sizeof(SimParams);
            computeEntries[1].binding = 1;
            computeEntries[1].visibility = WGPUShaderStage_Compute;
            computeEntries[1].buffer.type = WGPUBufferBindingType_Storage;
            computeEntries[1].buffer.minBindingSize = sizeof(Particle);
            computeEntries[2].binding = 2;
            computeEntries[2].visibility = WGPUShaderStage_Compute;
            computeEntries[2].texture.sampleType = WGPUTextureSampleType_Float;
            computeEntries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

            WGPUBindGroupLayoutDescriptor computeLayoutDesc{};
            computeLayoutDesc.label = {"bgl_comp", WGPU_STRLEN};
            computeLayoutDesc.entryCount = 3;
            computeLayoutDesc.entries = computeEntries;
            m_computeBindGroupLayout =
                wgpuDeviceCreateBindGroupLayout(ctx.device, &computeLayoutDesc);
            if (!m_computeBindGroupLayout)
                return false;

            WGPUBindGroupLayoutEntry renderEntries[1] = {};
            renderEntries[0].binding = 0;
            renderEntries[0].visibility = WGPUShaderStage_Vertex;
            renderEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
            renderEntries[0].buffer.minBindingSize = sizeof(RenderParams);

            WGPUBindGroupLayoutDescriptor renderLayoutDesc{};
            renderLayoutDesc.label = {"bgl_ren", WGPU_STRLEN};
            renderLayoutDesc.entryCount = 1;
            renderLayoutDesc.entries = renderEntries;
            m_renderBindGroupLayout =
                wgpuDeviceCreateBindGroupLayout(ctx.device, &renderLayoutDesc);
            if (!m_renderBindGroupLayout)
                return false;

            WGPUPipelineLayoutDescriptor computePipelineLayoutDesc{};
            computePipelineLayoutDesc.label = {"pl_comp", WGPU_STRLEN};
            computePipelineLayoutDesc.bindGroupLayoutCount = 1;
            computePipelineLayoutDesc.bindGroupLayouts = &m_computeBindGroupLayout;
            m_computePipelineLayout =
                wgpuDeviceCreatePipelineLayout(ctx.device, &computePipelineLayoutDesc);
            if (!m_computePipelineLayout)
                return false;

            WGPUPipelineLayoutDescriptor renderPipelineLayoutDesc{};
            renderPipelineLayoutDesc.label = {"pl_ren", WGPU_STRLEN};
            renderPipelineLayoutDesc.bindGroupLayoutCount = 1;
            renderPipelineLayoutDesc.bindGroupLayouts = &m_renderBindGroupLayout;
            m_renderPipelineLayout =
                wgpuDeviceCreatePipelineLayout(ctx.device, &renderPipelineLayoutDesc);
            if (!m_renderPipelineLayout)
                return false;

            WGPUBindGroupEntry computeBindGroupEntries[3] = {};
            computeBindGroupEntries[0].binding = 0;
            computeBindGroupEntries[0].buffer = m_simUniformBuffer;
            computeBindGroupEntries[0].size = sizeof(SimParams);
            computeBindGroupEntries[1].binding = 1;
            computeBindGroupEntries[1].buffer = m_particleBuffer;
            computeBindGroupEntries[1].size =
                static_cast<uint64_t>(kNumParticles) * sizeof(Particle);
            computeBindGroupEntries[2].binding = 2;
            computeBindGroupEntries[2].textureView = m_logoView;

            WGPUBindGroupDescriptor computeBindGroupDesc{};
            computeBindGroupDesc.label = {"bg_comp", WGPU_STRLEN};
            computeBindGroupDesc.layout = m_computeBindGroupLayout;
            computeBindGroupDesc.entryCount = 3;
            computeBindGroupDesc.entries = computeBindGroupEntries;
            m_computeBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &computeBindGroupDesc);
            if (!m_computeBindGroup)
                return false;

            WGPUBindGroupEntry renderBindGroupEntries[1] = {};
            renderBindGroupEntries[0].binding = 0;
            renderBindGroupEntries[0].buffer = m_renderUniformBuffer;
            renderBindGroupEntries[0].size = sizeof(RenderParams);

            WGPUBindGroupDescriptor renderBindGroupDesc{};
            renderBindGroupDesc.label = {"bg_ren", WGPU_STRLEN};
            renderBindGroupDesc.layout = m_renderBindGroupLayout;
            renderBindGroupDesc.entryCount = 1;
            renderBindGroupDesc.entries = renderBindGroupEntries;
            m_renderBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &renderBindGroupDesc);
            if (!m_renderBindGroup)
                return false;

            WGPUComputePipelineDescriptor computePipelineDesc{};
            computePipelineDesc.label = {"pipe_comp", WGPU_STRLEN};
            computePipelineDesc.layout = m_computePipelineLayout;
            computePipelineDesc.compute.module = m_computeShader;
            computePipelineDesc.compute.entryPoint = {"CSMain", WGPU_STRLEN};
            m_computePipeline = wgpuDeviceCreateComputePipeline(ctx.device, &computePipelineDesc);
            if (!m_computePipeline)
                return false;

            WGPUVertexAttribute particleAttributes[2] = {};
            particleAttributes[0].format = WGPUVertexFormat_Float32x3;
            particleAttributes[0].offset = offsetof(Particle, position);
            particleAttributes[0].shaderLocation = 0;
            particleAttributes[1].format = WGPUVertexFormat_Float32x4;
            particleAttributes[1].offset = offsetof(Particle, color);
            particleAttributes[1].shaderLocation = 1;

            WGPUVertexBufferLayout particleLayout{};
            particleLayout.arrayStride = sizeof(Particle);
            particleLayout.stepMode = WGPUVertexStepMode_Instance;
            particleLayout.attributeCount = 2;
            particleLayout.attributes = particleAttributes;

            WGPUVertexAttribute quadAttribute{};
            quadAttribute.format = WGPUVertexFormat_Float32x2;
            quadAttribute.offset = 0;
            quadAttribute.shaderLocation = 2;

            WGPUVertexBufferLayout quadLayout{};
            quadLayout.arrayStride = 2 * sizeof(float);
            quadLayout.stepMode = WGPUVertexStepMode_Vertex;
            quadLayout.attributeCount = 1;
            quadLayout.attributes = &quadAttribute;

            WGPUVertexBufferLayout layouts[2] = {particleLayout, quadLayout};

            WGPUBlendState blend{};
            blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
            blend.color.dstFactor = WGPUBlendFactor_One;
            blend.color.operation = WGPUBlendOperation_Add;
            blend.alpha.srcFactor = WGPUBlendFactor_Zero;
            blend.alpha.dstFactor = WGPUBlendFactor_One;
            blend.alpha.operation = WGPUBlendOperation_Add;

            WGPUColorTargetState colorTarget{};
            colorTarget.format = ctx.surfaceFormat;
            colorTarget.blend = &blend;
            colorTarget.writeMask = WGPUColorWriteMask_All;

            WGPUFragmentState fragmentState{};
            fragmentState.module = m_pixelShader;
            fragmentState.entryPoint = {"PSMain", WGPU_STRLEN};
            fragmentState.targetCount = 1;
            fragmentState.targets = &colorTarget;

            WGPURenderPipelineDescriptor renderPipelineDesc{};
            renderPipelineDesc.label = {"pipe_ren", WGPU_STRLEN};
            renderPipelineDesc.layout = m_renderPipelineLayout;
            renderPipelineDesc.vertex.module = m_vertexShader;
            renderPipelineDesc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            renderPipelineDesc.vertex.bufferCount = 2;
            renderPipelineDesc.vertex.buffers = layouts;
            renderPipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            renderPipelineDesc.primitive.cullMode = WGPUCullMode_None;
            renderPipelineDesc.multisample.count = 1;
            renderPipelineDesc.multisample.mask = 0xFFFFFFFF;
            renderPipelineDesc.fragment = &fragmentState;
            m_renderPipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &renderPipelineDesc);
            return m_renderPipeline != nullptr;
        }

        void ReleaseResources()
        {
            if (m_renderPipeline)
                wgpuRenderPipelineRelease(m_renderPipeline);
            if (m_computePipeline)
                wgpuComputePipelineRelease(m_computePipeline);
            if (m_renderPipelineLayout)
                wgpuPipelineLayoutRelease(m_renderPipelineLayout);
            if (m_computePipelineLayout)
                wgpuPipelineLayoutRelease(m_computePipelineLayout);
            if (m_renderBindGroup)
                wgpuBindGroupRelease(m_renderBindGroup);
            if (m_computeBindGroup)
                wgpuBindGroupRelease(m_computeBindGroup);
            if (m_renderBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_renderBindGroupLayout);
            if (m_computeBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_computeBindGroupLayout);
            if (m_logoView)
                wgpuTextureViewRelease(m_logoView);
            if (m_logoTexture)
                wgpuTextureRelease(m_logoTexture);
            if (m_pixelShader)
                wgpuShaderModuleRelease(m_pixelShader);
            if (m_vertexShader)
                wgpuShaderModuleRelease(m_vertexShader);
            if (m_computeShader)
                wgpuShaderModuleRelease(m_computeShader);
            if (m_renderUniformBuffer)
                wgpuBufferRelease(m_renderUniformBuffer);
            if (m_simUniformBuffer)
                wgpuBufferRelease(m_simUniformBuffer);
            if (m_quadVertexBuffer)
                wgpuBufferRelease(m_quadVertexBuffer);
            if (m_particleBuffer)
                wgpuBufferRelease(m_particleBuffer);

            m_renderPipeline = nullptr;
            m_computePipeline = nullptr;
            m_renderPipelineLayout = nullptr;
            m_computePipelineLayout = nullptr;
            m_renderBindGroup = nullptr;
            m_computeBindGroup = nullptr;
            m_renderBindGroupLayout = nullptr;
            m_computeBindGroupLayout = nullptr;
            m_logoView = nullptr;
            m_logoTexture = nullptr;
            m_pixelShader = nullptr;
            m_vertexShader = nullptr;
            m_computeShader = nullptr;
            m_renderUniformBuffer = nullptr;
            m_simUniformBuffer = nullptr;
            m_quadVertexBuffer = nullptr;
            m_particleBuffer = nullptr;
        }

        static constexpr float kQuadVertices[12] = {
            -1.f,
            -1.f,
            +1.f,
            -1.f,
            -1.f,
            +1.f,
            -1.f,
            +1.f,
            +1.f,
            -1.f,
            +1.f,
            +1.f,
        };

        int m_logoWidth = 0;
        int m_logoHeight = 0;
        WGPUShaderModule m_computeShader = nullptr;
        WGPUShaderModule m_vertexShader = nullptr;
        WGPUShaderModule m_pixelShader = nullptr;
        WGPUTexture m_logoTexture = nullptr;
        WGPUTextureView m_logoView = nullptr;
        WGPUBuffer m_particleBuffer = nullptr;
        WGPUBuffer m_quadVertexBuffer = nullptr;
        WGPUBuffer m_simUniformBuffer = nullptr;
        WGPUBuffer m_renderUniformBuffer = nullptr;
        WGPUBindGroupLayout m_computeBindGroupLayout = nullptr;
        WGPUBindGroupLayout m_renderBindGroupLayout = nullptr;
        WGPUPipelineLayout m_computePipelineLayout = nullptr;
        WGPUPipelineLayout m_renderPipelineLayout = nullptr;
        WGPUBindGroup m_computeBindGroup = nullptr;
        WGPUBindGroup m_renderBindGroup = nullptr;
        WGPUComputePipeline m_computePipeline = nullptr;
        WGPURenderPipeline m_renderPipeline = nullptr;
    };

    pwgpu::test::SampleAppDesc MakeParticlesDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "ParticlesTest";
        desc.windowTitle = "PhasmaWebGPU Particles";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    ParticlesSample sample;
    pwgpu::test::SampleApp app(sample, MakeParticlesDesc());
    return app.Run(argc, argv);
}
