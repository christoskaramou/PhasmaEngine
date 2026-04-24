#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb/stb_image.h"

// Port of the webgpu-samples marchingCubes external into PhasmaWebGPU's sample
// shell. This path keeps a fixed-slot generator for now, but uses the upstream
// SDF, camera scale, and triplanar material assets so the visual output matches
// the original sample closely.

#include "../Common/SampleApp.h"
#include "../Common/SampleBase.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    constexpr float kPi = 3.14159265358979323846f;

    constexpr uint32_t kGridDim = 48;
    constexpr uint32_t kCells = kGridDim * kGridDim * kGridDim;
    constexpr uint32_t kTetsPerCell = 6;
    constexpr uint32_t kVerticesPerTet = 6;
    constexpr uint32_t kVerticesPerCell = kTetsPerCell * kVerticesPerTet;
    constexpr uint32_t kMaxGeneratedVertices = kCells * kVerticesPerCell;
    constexpr uint64_t kGeneratedVertexStride = 32; // float4 position + float4 normal
    constexpr uint64_t kGeneratedVertexBufferSize =
        static_cast<uint64_t>(kMaxGeneratedVertices) * kGeneratedVertexStride;
    constexpr uint64_t kGenerateUniformSize = 16;
    constexpr uint64_t kRenderUniformSize = 144; // model, viewProj, eyeTime
    constexpr uint32_t kComputeWorkgroupSize = 128;
    constexpr uint32_t kTextureCount = 6;

    const char *kTextureAssetNames[kTextureCount] = {
        "coast_sand_rocks_02_diff_1k.jpg",
        "mud_cracked_dry_03_diff_1k.jpg",
        "rock_face_03_diff_1k.jpg",
        "coast_sand_rocks_02_nor_gl_1k.png",
        "mud_cracked_dry_03_nor_gl_1k.png",
        "rock_face_03_nor_gl_1k.png",
    };

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
                    "[MarchingCubes] Failed to load %s: %s\n",
                    path.c_str(),
                    stbi_failure_reason());
            return false;
        }
        return true;
    }

    class MarchingCubesSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            m_generateShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "generate.comp.hlsl").c_str(), "mc_generate_cs");
            m_surfaceVs = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "surface.vert.hlsl").c_str(), "mc_surface_vs");
            m_surfacePs = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "surface.pixel.hlsl").c_str(), "mc_surface_ps");
            if (!m_generateShader || !m_surfaceVs || !m_surfacePs)
                return false;

            if (!CreateBuffers(ctx) || !CreateTextures(ctx) || !CreateComputePipeline(ctx) ||
                !CreateRenderPipeline(ctx) || !CreateBindGroups(ctx))
            {
                return false;
            }

            Resize(ctx, ctx.width, ctx.height);
            return m_depthView != nullptr;
        }

        void Resize(pwgpu::test::SampleContext &ctx, uint32_t width, uint32_t height) override
        {
            ReleaseDepthTarget();
            if (width == 0 || height == 0)
                return;

            WGPUTextureDescriptor depthDesc{};
            depthDesc.label = {"mc_depth", WGPU_STRLEN};
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
            const float time = static_cast<float>(ctx.timing.elapsedSeconds);

            const float generateUniforms[4] = {
                time,
                0.43f,
                0.48f,
                time * 0.6f,
            };
            wgpuQueueWriteBuffer(ctx.queue,
                                 m_generateUniformBuffer,
                                 0,
                                 generateUniforms,
                                 sizeof(generateUniforms));

            const float aspect = ctx.height > 0 ? static_cast<float>(ctx.width) /
                                                      static_cast<float>(ctx.height)
                                                : 1.0f;
            glm::mat4 projection = glm::perspectiveRH_ZO(60.0f * kPi / 180.0f,
                                                         aspect,
                                                         0.1f,
                                                         200.0f);
            projection[1][1] *= -1.0f;

            const float yaw = kPi * 0.5f;
            const float pitch = kPi / 16.0f;
            const float dolly = 35.0f;
            const glm::vec3 eye(cosf(yaw) * dolly,
                                sinf(pitch) * dolly,
                                sinf(yaw) * dolly);
            const glm::mat4 view =
                glm::lookAtRH(eye, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::mat4 model(1.0f);
            const glm::mat4 viewProj = projection * view;

            std::array<float, 36> renderUniforms{};
            std::memcpy(renderUniforms.data() + 0, glm::value_ptr(model), 64);
            std::memcpy(renderUniforms.data() + 16, glm::value_ptr(viewProj), 64);
            renderUniforms[32] = eye.x;
            renderUniforms[33] = eye.y;
            renderUniforms[34] = eye.z;
            renderUniforms[35] = time;
            wgpuQueueWriteBuffer(ctx.queue,
                                 m_renderUniformBuffer,
                                 0,
                                 renderUniforms.data(),
                                 kRenderUniformSize);
        }

        bool Execute(pwgpu::test::SampleContext &, pwgpu::test::SampleFrame &frame) override
        {
            if (!m_depthView)
                return true;

            {
                WGPUComputePassDescriptor passDesc{};
                passDesc.label = {"mc_generate_pass", WGPU_STRLEN};
                WGPUComputePassEncoder pass =
                    wgpuCommandEncoderBeginComputePass(frame.encoder, &passDesc);
                wgpuComputePassEncoderSetPipeline(pass, m_generatePipeline);
                wgpuComputePassEncoderSetBindGroup(pass, 0, m_generateBindGroup, 0, nullptr);
                wgpuComputePassEncoderDispatchWorkgroups(
                    pass,
                    (kCells + kComputeWorkgroupSize - 1u) / kComputeWorkgroupSize,
                    1,
                    1);
                wgpuComputePassEncoderEnd(pass);
                wgpuComputePassEncoderRelease(pass);
            }

            WGPURenderPassEncoder pass =
                BeginRenderPass(frame, "mc_render_pass", {0.40, 0.38, 0.32, 1.0}, m_depthView);
            wgpuRenderPassEncoderSetPipeline(pass, m_renderPipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, m_renderBindGroup, 0, nullptr);
            wgpuRenderPassEncoderDraw(pass, kMaxGeneratedVertices, 1, 0, 0);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            ReleaseDepthTarget();

            if (m_renderPipeline)
                wgpuRenderPipelineRelease(m_renderPipeline);
            if (m_generatePipeline)
                wgpuComputePipelineRelease(m_generatePipeline);
            if (m_renderPipelineLayout)
                wgpuPipelineLayoutRelease(m_renderPipelineLayout);
            if (m_generatePipelineLayout)
                wgpuPipelineLayoutRelease(m_generatePipelineLayout);
            if (m_renderBindGroup)
                wgpuBindGroupRelease(m_renderBindGroup);
            if (m_generateBindGroup)
                wgpuBindGroupRelease(m_generateBindGroup);
            if (m_renderBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_renderBindGroupLayout);
            if (m_generateBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_generateBindGroupLayout);
            if (m_sampler)
                wgpuSamplerRelease(m_sampler);
            for (WGPUTextureView &view : m_textureViews)
            {
                if (view)
                    wgpuTextureViewRelease(view);
                view = nullptr;
            }
            for (WGPUTexture &texture : m_textures)
            {
                if (texture)
                    wgpuTextureRelease(texture);
                texture = nullptr;
            }
            if (m_generatedVertexBuffer)
                wgpuBufferRelease(m_generatedVertexBuffer);
            if (m_renderUniformBuffer)
                wgpuBufferRelease(m_renderUniformBuffer);
            if (m_generateUniformBuffer)
                wgpuBufferRelease(m_generateUniformBuffer);
            if (m_surfacePs)
                wgpuShaderModuleRelease(m_surfacePs);
            if (m_surfaceVs)
                wgpuShaderModuleRelease(m_surfaceVs);
            if (m_generateShader)
                wgpuShaderModuleRelease(m_generateShader);

            m_renderPipeline = nullptr;
            m_generatePipeline = nullptr;
            m_renderPipelineLayout = nullptr;
            m_generatePipelineLayout = nullptr;
            m_renderBindGroup = nullptr;
            m_generateBindGroup = nullptr;
            m_renderBindGroupLayout = nullptr;
            m_generateBindGroupLayout = nullptr;
            m_sampler = nullptr;
            m_generatedVertexBuffer = nullptr;
            m_renderUniformBuffer = nullptr;
            m_generateUniformBuffer = nullptr;
            m_surfacePs = nullptr;
            m_surfaceVs = nullptr;
            m_generateShader = nullptr;
        }

    private:
        bool CreateBuffers(pwgpu::test::SampleContext &ctx)
        {
            m_generateUniformBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                                kGenerateUniformSize,
                                                                WGPUBufferUsage_Uniform |
                                                                    WGPUBufferUsage_CopyDst,
                                                                "mc_generate_ubo");
            m_renderUniformBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                              kRenderUniformSize,
                                                              WGPUBufferUsage_Uniform |
                                                                  WGPUBufferUsage_CopyDst,
                                                              "mc_render_ubo");
            m_generatedVertexBuffer =
                pwgpu::test::CreateBuffer(ctx.device,
                                          kGeneratedVertexBufferSize,
                                          WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
                                          "mc_generated_vertices");
            return m_generateUniformBuffer && m_renderUniformBuffer && m_generatedVertexBuffer;
        }

        bool CreateTextures(pwgpu::test::SampleContext &ctx)
        {
            for (uint32_t i = 0; i < kTextureCount; ++i)
            {
                LoadedImage image{};
                const std::string path =
                    pwgpu::test::GetAssetPath(ctx.exeDir, kTextureAssetNames[i]);
                if (!LoadImageRGBA8(path, image))
                    return false;

                WGPUTextureDescriptor textureDesc{};
                textureDesc.label = {kTextureAssetNames[i], WGPU_STRLEN};
                textureDesc.dimension = WGPUTextureDimension_2D;
                textureDesc.size = {static_cast<uint32_t>(image.width),
                                    static_cast<uint32_t>(image.height),
                                    1};
                textureDesc.mipLevelCount = 1;
                textureDesc.sampleCount = 1;
                textureDesc.format = WGPUTextureFormat_RGBA8Unorm;
                textureDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
                m_textures[i] = wgpuDeviceCreateTexture(ctx.device, &textureDesc);
                if (!m_textures[i])
                {
                    stbi_image_free(image.pixels);
                    return false;
                }

                pwgpu::test::UploadRGBA8Texture(ctx.queue,
                                                m_textures[i],
                                                image.pixels,
                                                static_cast<uint32_t>(image.width),
                                                static_cast<uint32_t>(image.height));
                stbi_image_free(image.pixels);

                m_textureViews[i] =
                    pwgpu::test::CreateTextureView(m_textures[i], WGPUTextureFormat_RGBA8Unorm);
                if (!m_textureViews[i])
                    return false;
            }

            WGPUSamplerDescriptor samplerDesc{};
            samplerDesc.label = {"mc_linear_repeat_sampler", WGPU_STRLEN};
            samplerDesc.addressModeU = WGPUAddressMode_Repeat;
            samplerDesc.addressModeV = WGPUAddressMode_Repeat;
            samplerDesc.addressModeW = WGPUAddressMode_Repeat;
            samplerDesc.magFilter = WGPUFilterMode_Linear;
            samplerDesc.minFilter = WGPUFilterMode_Linear;
            samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
            samplerDesc.lodMinClamp = 0.0f;
            samplerDesc.lodMaxClamp = 32.0f;
            samplerDesc.compare = WGPUCompareFunction_Undefined;
            samplerDesc.maxAnisotropy = 1;
            m_sampler = wgpuDeviceCreateSampler(ctx.device, &samplerDesc);
            return m_sampler != nullptr;
        }

        bool CreateComputePipeline(pwgpu::test::SampleContext &ctx)
        {
            std::array<WGPUBindGroupLayoutEntry, 2> entries{};
            entries[0].binding = 0;
            entries[0].visibility = WGPUShaderStage_Compute;
            entries[0].buffer.type = WGPUBufferBindingType_Uniform;
            entries[0].buffer.minBindingSize = kGenerateUniformSize;
            entries[1].binding = 1;
            entries[1].visibility = WGPUShaderStage_Compute;
            entries[1].buffer.type = WGPUBufferBindingType_Storage;
            entries[1].buffer.minBindingSize = kGeneratedVertexBufferSize;

            WGPUBindGroupLayoutDescriptor bglDesc{};
            bglDesc.label = {"mc_generate_bgl", WGPU_STRLEN};
            bglDesc.entryCount = entries.size();
            bglDesc.entries = entries.data();
            m_generateBindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device, &bglDesc);
            if (!m_generateBindGroupLayout)
                return false;

            WGPUPipelineLayoutDescriptor layoutDesc{};
            layoutDesc.label = {"mc_generate_pl", WGPU_STRLEN};
            layoutDesc.bindGroupLayoutCount = 1;
            layoutDesc.bindGroupLayouts = &m_generateBindGroupLayout;
            m_generatePipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device, &layoutDesc);
            if (!m_generatePipelineLayout)
                return false;

            WGPUComputePipelineDescriptor pipelineDesc{};
            pipelineDesc.label = {"mc_generate_pipeline", WGPU_STRLEN};
            pipelineDesc.layout = m_generatePipelineLayout;
            pipelineDesc.compute.module = m_generateShader;
            pipelineDesc.compute.entryPoint = {"CSMain", WGPU_STRLEN};
            m_generatePipeline = wgpuDeviceCreateComputePipeline(ctx.device, &pipelineDesc);
            return m_generatePipeline != nullptr;
        }

        bool CreateRenderPipeline(pwgpu::test::SampleContext &ctx)
        {
            std::array<WGPUBindGroupLayoutEntry, 9> entries{};
            entries[0].binding = 0;
            entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
            entries[0].buffer.type = WGPUBufferBindingType_Uniform;
            entries[0].buffer.minBindingSize = kRenderUniformSize;
            entries[1].binding = 1;
            entries[1].visibility = WGPUShaderStage_Vertex;
            entries[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
            entries[1].buffer.minBindingSize = kGeneratedVertexBufferSize;
            entries[2].binding = 2;
            entries[2].visibility = WGPUShaderStage_Fragment;
            entries[2].sampler.type = WGPUSamplerBindingType_Filtering;
            for (uint32_t i = 0; i < kTextureCount; ++i)
            {
                WGPUBindGroupLayoutEntry &entry = entries[3 + i];
                entry.binding = 3 + i;
                entry.visibility = WGPUShaderStage_Fragment;
                entry.texture.sampleType = WGPUTextureSampleType_Float;
                entry.texture.viewDimension = WGPUTextureViewDimension_2D;
                entry.texture.multisampled = WGPUOptionalBool_False;
            }

            WGPUBindGroupLayoutDescriptor bglDesc{};
            bglDesc.label = {"mc_render_bgl", WGPU_STRLEN};
            bglDesc.entryCount = entries.size();
            bglDesc.entries = entries.data();
            m_renderBindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device, &bglDesc);
            if (!m_renderBindGroupLayout)
                return false;

            WGPUPipelineLayoutDescriptor layoutDesc{};
            layoutDesc.label = {"mc_render_pl", WGPU_STRLEN};
            layoutDesc.bindGroupLayoutCount = 1;
            layoutDesc.bindGroupLayouts = &m_renderBindGroupLayout;
            m_renderPipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device, &layoutDesc);
            if (!m_renderPipelineLayout)
                return false;

            WGPUColorTargetState colorTarget{};
            colorTarget.format = ctx.surfaceFormat;
            colorTarget.writeMask = WGPUColorWriteMask_All;

            WGPUFragmentState fragment{};
            fragment.module = m_surfacePs;
            fragment.entryPoint = {"PSMain", WGPU_STRLEN};
            fragment.targetCount = 1;
            fragment.targets = &colorTarget;

            WGPUDepthStencilState depth{};
            depth.format = WGPUTextureFormat_Depth24Plus;
            depth.depthWriteEnabled = WGPUOptionalBool_True;
            depth.depthCompare = WGPUCompareFunction_Less;
            depth.stencilFront.compare = WGPUCompareFunction_Always;
            depth.stencilFront.failOp = WGPUStencilOperation_Keep;
            depth.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
            depth.stencilFront.passOp = WGPUStencilOperation_Keep;
            depth.stencilBack = depth.stencilFront;

            WGPURenderPipelineDescriptor pipelineDesc{};
            pipelineDesc.label = {"mc_render_pipeline", WGPU_STRLEN};
            pipelineDesc.layout = m_renderPipelineLayout;
            pipelineDesc.vertex.module = m_surfaceVs;
            pipelineDesc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            pipelineDesc.vertex.bufferCount = 0;
            pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            pipelineDesc.primitive.cullMode = WGPUCullMode_None;
            pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
            pipelineDesc.depthStencil = &depth;
            pipelineDesc.multisample.count = 1;
            pipelineDesc.multisample.mask = 0xFFFFFFFF;
            pipelineDesc.fragment = &fragment;
            m_renderPipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &pipelineDesc);
            return m_renderPipeline != nullptr;
        }

        bool CreateBindGroups(pwgpu::test::SampleContext &ctx)
        {
            std::array<WGPUBindGroupEntry, 2> generateEntries{};
            generateEntries[0].binding = 0;
            generateEntries[0].buffer = m_generateUniformBuffer;
            generateEntries[0].size = kGenerateUniformSize;
            generateEntries[1].binding = 1;
            generateEntries[1].buffer = m_generatedVertexBuffer;
            generateEntries[1].size = kGeneratedVertexBufferSize;

            WGPUBindGroupDescriptor generateDesc{};
            generateDesc.label = {"mc_generate_bg", WGPU_STRLEN};
            generateDesc.layout = m_generateBindGroupLayout;
            generateDesc.entryCount = generateEntries.size();
            generateDesc.entries = generateEntries.data();
            m_generateBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &generateDesc);
            if (!m_generateBindGroup)
                return false;

            std::array<WGPUBindGroupEntry, 9> renderEntries{};
            renderEntries[0].binding = 0;
            renderEntries[0].buffer = m_renderUniformBuffer;
            renderEntries[0].size = kRenderUniformSize;
            renderEntries[1].binding = 1;
            renderEntries[1].buffer = m_generatedVertexBuffer;
            renderEntries[1].size = kGeneratedVertexBufferSize;
            renderEntries[2].binding = 2;
            renderEntries[2].sampler = m_sampler;
            for (uint32_t i = 0; i < kTextureCount; ++i)
            {
                renderEntries[3 + i].binding = 3 + i;
                renderEntries[3 + i].textureView = m_textureViews[i];
            }

            WGPUBindGroupDescriptor renderDesc{};
            renderDesc.label = {"mc_render_bg", WGPU_STRLEN};
            renderDesc.layout = m_renderBindGroupLayout;
            renderDesc.entryCount = renderEntries.size();
            renderDesc.entries = renderEntries.data();
            m_renderBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &renderDesc);
            return m_renderBindGroup != nullptr;
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

        WGPUShaderModule m_generateShader = nullptr;
        WGPUShaderModule m_surfaceVs = nullptr;
        WGPUShaderModule m_surfacePs = nullptr;
        WGPUBuffer m_generateUniformBuffer = nullptr;
        WGPUBuffer m_renderUniformBuffer = nullptr;
        WGPUBuffer m_generatedVertexBuffer = nullptr;
        std::array<WGPUTexture, kTextureCount> m_textures{};
        std::array<WGPUTextureView, kTextureCount> m_textureViews{};
        WGPUSampler m_sampler = nullptr;
        WGPUBindGroupLayout m_generateBindGroupLayout = nullptr;
        WGPUBindGroupLayout m_renderBindGroupLayout = nullptr;
        WGPUPipelineLayout m_generatePipelineLayout = nullptr;
        WGPUPipelineLayout m_renderPipelineLayout = nullptr;
        WGPUBindGroup m_generateBindGroup = nullptr;
        WGPUBindGroup m_renderBindGroup = nullptr;
        WGPUComputePipeline m_generatePipeline = nullptr;
        WGPURenderPipeline m_renderPipeline = nullptr;
        WGPUTexture m_depthTexture = nullptr;
        WGPUTextureView m_depthView = nullptr;
    };

    pwgpu::test::SampleAppDesc MakeMarchingCubesDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "MarchingCubesTest";
        desc.windowTitle = "PhasmaWebGPU Marching Cubes";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    MarchingCubesSample sample;
    pwgpu::test::SampleApp app(sample, MakeMarchingCubesDesc());
    return app.Run(argc, argv);
}
