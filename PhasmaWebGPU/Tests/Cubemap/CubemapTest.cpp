#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb/stb_image.h"

#include "../Common/SampleApp.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"


#include <cmath>

namespace
{
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    constexpr float kPi = 3.14159265f;
    constexpr std::array<const char *, 6> kFaceFiles = {
        "posx.jpg",
        "negx.jpg",
        "posy.jpg",
        "negy.jpg",
        "posz.jpg",
        "negz.jpg",
    };

    struct LoadedImage
    {
        uint8_t *pixels = nullptr;
        int width = 0;
        int height = 0;
    };

    void FreeLoadedImages(std::array<LoadedImage, 6> &images)
    {
        for (LoadedImage &image : images)
        {
            if (image.pixels)
            {
                stbi_image_free(image.pixels);
                image.pixels = nullptr;
            }
        }
    }

    bool LoadCubemapImages(const std::string &exeDir,
                           std::array<LoadedImage, 6> &images,
                           uint32_t &faceWidth,
                           uint32_t &faceHeight)
    {
        for (size_t i = 0; i < images.size(); i++)
        {
            std::string path = pwgpu::test::GetAssetPath(exeDir, kFaceFiles[i]);
            int channels = 0;
            images[i].pixels =
                stbi_load(path.c_str(), &images[i].width, &images[i].height, &channels, 4);
            if (!images[i].pixels)
            {
                fprintf(stderr, "[Image] Failed to load %s: %s\n", path.c_str(), stbi_failure_reason());
                FreeLoadedImages(images);
                return false;
            }

            if (i == 0)
            {
                faceWidth = static_cast<uint32_t>(images[i].width);
                faceHeight = static_cast<uint32_t>(images[i].height);
                continue;
            }

            if (images[i].width != static_cast<int>(faceWidth) ||
                images[i].height != static_cast<int>(faceHeight))
            {
                fprintf(stderr, "[Image] Face %zu size mismatch\n", i);
                FreeLoadedImages(images);
                return false;
            }
        }

        return true;
    }

    class CubemapSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            std::array<LoadedImage, 6> images{};
            uint32_t faceWidth = 0;
            uint32_t faceHeight = 0;
            if (!LoadCubemapImages(ctx.exeDir, images, faceWidth, faceHeight))
                return false;

            m_vertexShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "cubemap.vert.hlsl").c_str(), "sky_vs");
            m_pixelShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "cubemap.pixel.hlsl").c_str(), "sky_ps");
            if (!m_vertexShader || !m_pixelShader)
            {
                FreeLoadedImages(images);
                return false;
            }

            if (!CreateTextureResources(ctx, images, faceWidth, faceHeight))
            {
                FreeLoadedImages(images);
                return false;
            }
            FreeLoadedImages(images);

            m_uniformBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                        64,
                                                        WGPUBufferUsage_Uniform |
                                                            WGPUBufferUsage_CopyDst,
                                                        "sky_ubo");
            if (!m_uniformBuffer)
                return false;

            if (!CreatePipelineResources(ctx))
                return false;

            return true;
        }

        void Resize(pwgpu::test::SampleContext &, uint32_t, uint32_t) override {}

        void Update(pwgpu::test::SampleContext &ctx) override
        {
            float aspect = ctx.height > 0 ? static_cast<float>(ctx.width) /
                                                static_cast<float>(ctx.height)
                                          : 1.0f;
            mat4 projection =
                glm::perspectiveRH_ZO((2.f * kPi) / 5.f, aspect, 1.f, 3000.f);

            float time = static_cast<float>(ctx.timing.elapsedSeconds) * 1.25f;
            mat4 view = mat4(1.f);
            view = glm::rotate(view, (kPi / 10.f) * sinf(time), vec3(1.f, 0.f, 0.f));
            view = glm::rotate(view, time * 0.2f, vec3(0.f, 1.f, 0.f));
            mat4 inverseViewProjection = glm::inverse(projection * view);
            wgpuQueueWriteBuffer(ctx.queue, m_uniformBuffer, 0, glm::value_ptr(inverseViewProjection), 64);
        }

        bool Execute(pwgpu::test::SampleContext &, pwgpu::test::SampleFrame &frame) override
        {
            WGPURenderPassColorAttachment colorAttachment{};
            colorAttachment.view = frame.surfaceView;
            colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            colorAttachment.loadOp = WGPULoadOp_Clear;
            colorAttachment.storeOp = WGPUStoreOp_Store;
            colorAttachment.clearValue = {0.0, 0.0, 0.0, 1.0};

            WGPURenderPassDescriptor renderPassDesc{};
            renderPassDesc.label = {"render_pass", WGPU_STRLEN};
            renderPassDesc.colorAttachmentCount = 1;
            renderPassDesc.colorAttachments = &colorAttachment;

            WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(frame.encoder, &renderPassDesc);
            wgpuRenderPassEncoderSetPipeline(renderPass, m_pipeline);
            wgpuRenderPassEncoderSetBindGroup(renderPass, 0, m_bindGroup, 0, nullptr);
            wgpuRenderPassEncoderDraw(renderPass, 3, 1, 0, 0);
            wgpuRenderPassEncoderEnd(renderPass);
            wgpuRenderPassEncoderRelease(renderPass);
            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
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
        bool CreateTextureResources(pwgpu::test::SampleContext &ctx,
                                    const std::array<LoadedImage, 6> &images,
                                    uint32_t faceWidth,
                                    uint32_t faceHeight)
        {
            WGPUTextureDescriptor textureDesc{};
            textureDesc.label = {"cubemap_tex", WGPU_STRLEN};
            textureDesc.dimension = WGPUTextureDimension_2D;
            textureDesc.size = {faceWidth, faceHeight, 6};
            textureDesc.mipLevelCount = 1;
            textureDesc.sampleCount = 1;
            textureDesc.format = WGPUTextureFormat_RGBA8Unorm;
            textureDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
            m_texture = wgpuDeviceCreateTexture(ctx.device, &textureDesc);
            if (!m_texture)
                return false;

            for (uint32_t faceIndex = 0; faceIndex < images.size(); faceIndex++)
            {
                pwgpu::test::UploadRGBA8Texture(ctx.queue,
                                                m_texture,
                                                images[faceIndex].pixels,
                                                faceWidth,
                                                faceHeight,
                                                0,
                                                faceIndex);
            }

            WGPUTextureViewDescriptor textureViewDesc{};
            textureViewDesc.format = WGPUTextureFormat_RGBA8Unorm;
            textureViewDesc.dimension = WGPUTextureViewDimension_Cube;
            textureViewDesc.baseMipLevel = 0;
            textureViewDesc.mipLevelCount = 1;
            textureViewDesc.baseArrayLayer = 0;
            textureViewDesc.arrayLayerCount = 6;
            textureViewDesc.aspect = WGPUTextureAspect_All;
            m_textureView = wgpuTextureCreateView(m_texture, &textureViewDesc);
            if (!m_textureView)
                return false;

            WGPUSamplerDescriptor samplerDesc{};
            samplerDesc.label = {"cube_sampler", WGPU_STRLEN};
            samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
            samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
            samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
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

        bool CreatePipelineResources(pwgpu::test::SampleContext &ctx)
        {
            WGPUBindGroupLayoutEntry bindGroupEntries[3] = {};
            bindGroupEntries[0].binding = 0;
            bindGroupEntries[0].visibility = WGPUShaderStage_Fragment;
            bindGroupEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
            bindGroupEntries[0].buffer.minBindingSize = 64;
            bindGroupEntries[1].binding = 1;
            bindGroupEntries[1].visibility = WGPUShaderStage_Fragment;
            bindGroupEntries[1].sampler.type = WGPUSamplerBindingType_Filtering;
            bindGroupEntries[2].binding = 2;
            bindGroupEntries[2].visibility = WGPUShaderStage_Fragment;
            bindGroupEntries[2].texture.sampleType = WGPUTextureSampleType_Float;
            bindGroupEntries[2].texture.viewDimension = WGPUTextureViewDimension_Cube;
            bindGroupEntries[2].texture.multisampled = WGPUOptionalBool_False;

            WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc{};
            bindGroupLayoutDesc.label = {"sky_bgl", WGPU_STRLEN};
            bindGroupLayoutDesc.entryCount = 3;
            bindGroupLayoutDesc.entries = bindGroupEntries;
            m_bindGroupLayout =
                wgpuDeviceCreateBindGroupLayout(ctx.device, &bindGroupLayoutDesc);
            if (!m_bindGroupLayout)
                return false;

            WGPUPipelineLayoutDescriptor pipelineLayoutDesc{};
            pipelineLayoutDesc.label = {"sky_pl", WGPU_STRLEN};
            pipelineLayoutDesc.bindGroupLayoutCount = 1;
            pipelineLayoutDesc.bindGroupLayouts = &m_bindGroupLayout;
            m_pipelineLayout =
                wgpuDeviceCreatePipelineLayout(ctx.device, &pipelineLayoutDesc);
            if (!m_pipelineLayout)
                return false;

            WGPUBindGroupEntry bindGroupEntriesDesc[3] = {};
            bindGroupEntriesDesc[0].binding = 0;
            bindGroupEntriesDesc[0].buffer = m_uniformBuffer;
            bindGroupEntriesDesc[0].size = 64;
            bindGroupEntriesDesc[1].binding = 1;
            bindGroupEntriesDesc[1].sampler = m_sampler;
            bindGroupEntriesDesc[2].binding = 2;
            bindGroupEntriesDesc[2].textureView = m_textureView;

            WGPUBindGroupDescriptor bindGroupDesc{};
            bindGroupDesc.label = {"sky_bg", WGPU_STRLEN};
            bindGroupDesc.layout = m_bindGroupLayout;
            bindGroupDesc.entryCount = 3;
            bindGroupDesc.entries = bindGroupEntriesDesc;
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
            pipelineDesc.label = {"sky_pipe", WGPU_STRLEN};
            pipelineDesc.layout = m_pipelineLayout;
            pipelineDesc.vertex.module = m_vertexShader;
            pipelineDesc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            pipelineDesc.primitive.cullMode = WGPUCullMode_None;
            pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
            pipelineDesc.multisample.count = 1;
            pipelineDesc.multisample.mask = 0xFFFFFFFF;
            pipelineDesc.fragment = &fragmentState;
            m_pipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &pipelineDesc);
            return m_pipeline != nullptr;
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
    };

    pwgpu::test::SampleAppDesc MakeCubemapDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "CubemapTest";
        desc.windowTitle = "PhasmaWebGPU Cubemap";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    pe::Log::Init();
    setvbuf(stdout, nullptr, _IONBF, 0);

    CubemapSample sample;
    pwgpu::test::SampleApp app(sample, MakeCubemapDesc());
    return app.Run(argc, argv);
}
