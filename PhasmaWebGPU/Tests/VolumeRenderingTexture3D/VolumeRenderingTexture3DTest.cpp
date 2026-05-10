// stb_image is included to reuse its public-domain DEFLATE decoder
// (stbi_zlib_decode_noheader_buffer). The sample parses the gzip header
// manually then hands the DEFLATE body to stb's inflater - this avoids
// depending on a linked zlib .lib in PhasmaCore.
#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb/stb_image.h"

#include "../Common/SampleApp.h"
#include "../Common/SampleBase.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"

#include "Base/Log.h"

#include <cmath>
#include <cstring>

namespace
{
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    constexpr float kPi = 3.14159265f;
    constexpr uint32_t kSampleCount = 4;
    constexpr uint32_t kRowAlignment = 256;

    // Upstream GUI defaults from main.ts: rotateCamera=true, near=4.3, far=4.4.
    constexpr bool kRotateCamera = true;
    constexpr float kNearPlane = 4.3f;
    constexpr float kFarPlane = 4.4f;

    // 180 x 216 x 180 uint8 volume (R8Unorm) from the BrainWeb dataset.
    constexpr uint32_t kVolumeWidth = 180;
    constexpr uint32_t kVolumeHeight = 216;
    constexpr uint32_t kVolumeDepth = 180;
    constexpr uint32_t kBytesPerPixel = 1;
    constexpr size_t kVolumeBytes =
        static_cast<size_t>(kVolumeWidth) * kVolumeHeight * kVolumeDepth * kBytesPerPixel;

    const char *kVolumeAssetName =
        "volume/t1_icbm_normal_1mm_pn0_rf0_180x216x180_uint8_1x1.bin-gz";

    bool ReadFile(const std::string &path, std::vector<uint8_t> &out)
    {
        FILE *file = fopen(path.c_str(), "rb");
        if (!file)
        {
            fprintf(stderr, "[Volume] Cannot open %s\n", path.c_str());
            return false;
        }
        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        fseek(file, 0, SEEK_SET);
        if (size <= 0)
        {
            fclose(file);
            return false;
        }
        out.resize(static_cast<size_t>(size));
        size_t read = fread(out.data(), 1, out.size(), file);
        fclose(file);
        return read == out.size();
    }

    // Parse an RFC-1952 gzip member and return offsets to the DEFLATE body.
    // The file ends with 4-byte CRC32 and 4-byte ISIZE - excluded from body.
    bool ParseGzipHeader(const std::vector<uint8_t> &gz,
                         size_t &bodyOffset,
                         size_t &bodyLength)
    {
        if (gz.size() < 18 || gz[0] != 0x1fu || gz[1] != 0x8bu || gz[2] != 8u)
        {
            fprintf(stderr, "[Volume] Not a gzip/DEFLATE stream\n");
            return false;
        }
        const uint8_t flags = gz[3];
        size_t offset = 10; // past fixed header

        if (flags & 0x04u) // FEXTRA
        {
            if (offset + 2 > gz.size())
                return false;
            const uint16_t xlen =
                static_cast<uint16_t>(gz[offset]) |
                static_cast<uint16_t>(static_cast<uint16_t>(gz[offset + 1]) << 8);
            offset += 2 + xlen;
        }
        if (flags & 0x08u) // FNAME
        {
            while (offset < gz.size() && gz[offset] != 0)
                offset++;
            offset++;
        }
        if (flags & 0x10u) // FCOMMENT
        {
            while (offset < gz.size() && gz[offset] != 0)
                offset++;
            offset++;
        }
        if (flags & 0x02u) // FHCRC
        {
            offset += 2;
        }

        if (offset + 8 > gz.size())
        {
            fprintf(stderr, "[Volume] Truncated gzip stream\n");
            return false;
        }

        bodyOffset = offset;
        bodyLength = gz.size() - offset - 8; // trim CRC32 + ISIZE
        return true;
    }

    bool LoadGzippedVolume(const std::string &path, std::vector<uint8_t> &out)
    {
        std::vector<uint8_t> gz;
        if (!ReadFile(path, gz))
            return false;

        size_t bodyOffset = 0;
        size_t bodyLength = 0;
        if (!ParseGzipHeader(gz, bodyOffset, bodyLength))
            return false;

        out.assign(kVolumeBytes, 0);
        const int decoded = stbi_zlib_decode_noheader_buffer(
            reinterpret_cast<char *>(out.data()),
            static_cast<int>(out.size()),
            reinterpret_cast<const char *>(gz.data() + bodyOffset),
            static_cast<int>(bodyLength));
        if (decoded < 0 || static_cast<size_t>(decoded) != kVolumeBytes)
        {
            fprintf(stderr,
                    "[Volume] DEFLATE failed (decoded=%d expected=%zu)\n",
                    decoded,
                    kVolumeBytes);
            return false;
        }
        return true;
    }

    void UploadVolumeTexture(WGPUQueue queue,
                             WGPUTexture texture,
                             const uint8_t *data)
    {
        const uint32_t unpaddedBytesPerRow = kVolumeWidth * kBytesPerPixel;
        const uint32_t paddedBytesPerRow =
            (unpaddedBytesPerRow + kRowAlignment - 1) & ~(kRowAlignment - 1);

        const void *source = data;
        size_t sourceSize = kVolumeBytes;

        std::vector<uint8_t> padded;
        if (paddedBytesPerRow != unpaddedBytesPerRow)
        {
            padded.assign(static_cast<size_t>(paddedBytesPerRow) * kVolumeHeight *
                              kVolumeDepth,
                          0);
            for (uint32_t z = 0; z < kVolumeDepth; z++)
            {
                for (uint32_t y = 0; y < kVolumeHeight; y++)
                {
                    const size_t src = (static_cast<size_t>(z) * kVolumeHeight + y) *
                                       unpaddedBytesPerRow;
                    const size_t dst = (static_cast<size_t>(z) * kVolumeHeight + y) *
                                       paddedBytesPerRow;
                    memcpy(padded.data() + dst, data + src, unpaddedBytesPerRow);
                }
            }
            source = padded.data();
            sourceSize = padded.size();
        }

        WGPUTexelCopyTextureInfo destination{};
        destination.texture = texture;
        destination.mipLevel = 0;
        destination.aspect = WGPUTextureAspect_All;

        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow = paddedBytesPerRow;
        layout.rowsPerImage = kVolumeHeight;

        WGPUExtent3D size{kVolumeWidth, kVolumeHeight, kVolumeDepth};
        wgpuQueueWriteTexture(queue, &destination, source, sourceSize, &layout, &size);
    }

    class VolumeRenderingTexture3DSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            const std::string path =
                pwgpu::test::GetAssetPath(ctx.exeDir, kVolumeAssetName);

            std::vector<uint8_t> volume;
            if (!LoadGzippedVolume(path, volume))
            {
                fprintf(stderr,
                        "        Place %s next to the executable.\n",
                        kVolumeAssetName);
                return false;
            }

            m_vertexShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "volume.vert.hlsl").c_str(), "vol_vs");
            m_pixelShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "volume.pixel.hlsl").c_str(), "vol_ps");
            if (!m_vertexShader || !m_pixelShader)
                return false;

            if (!CreateVolumeResources(ctx, volume))
                return false;

            m_uniformBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                        64,
                                                        WGPUBufferUsage_Uniform |
                                                            WGPUBufferUsage_CopyDst,
                                                        "vol_ubo");
            if (!m_uniformBuffer)
                return false;

            if (!CreatePipeline(ctx))
                return false;

            Resize(ctx, ctx.width, ctx.height);
            return m_msaaView != nullptr;
        }

        void Resize(pwgpu::test::SampleContext &ctx, uint32_t width, uint32_t height) override
        {
            ReleaseMsaaTarget();
            if (width == 0 || height == 0)
                return;

            WGPUTextureDescriptor textureDesc{};
            textureDesc.label = {"vol_msaa", WGPU_STRLEN};
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
            const float aspect =
                ctx.height > 0 ? static_cast<float>(ctx.width) / static_cast<float>(ctx.height)
                               : 1.0f;

            if (kRotateCamera)
                m_rotation += static_cast<float>(ctx.timing.deltaSeconds);

            mat4 view = glm::translate(mat4(1.f), vec3(0.f, 0.f, -4.f));
            view = glm::rotate(view,
                               1.f,
                               vec3(sinf(m_rotation), cosf(m_rotation), 0.f));

            mat4 projection =
                glm::perspectiveRH_ZO((2.f * kPi) / 5.f, aspect, kNearPlane, kFarPlane);
            projection[1][1] *= -1.f;

            const mat4 mvp = projection * view;
            const mat4 inverseMvp = glm::inverse(mvp);
            wgpuQueueWriteBuffer(ctx.queue,
                                 m_uniformBuffer,
                                 0,
                                 glm::value_ptr(inverseMvp),
                                 64);
        }

        bool Execute(pwgpu::test::SampleContext &, pwgpu::test::SampleFrame &frame) override
        {
            if (!m_msaaView)
                return true;

            WGPURenderPassColorAttachment colorAttachment{};
            colorAttachment.view = m_msaaView;
            colorAttachment.resolveTarget = frame.surfaceView;
            colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            colorAttachment.loadOp = WGPULoadOp_Clear;
            colorAttachment.storeOp = WGPUStoreOp_Discard;
            colorAttachment.clearValue = {0.0, 0.0, 0.0, 1.0};

            WGPURenderPassDescriptor renderPassDesc{};
            renderPassDesc.label = {"vol_pass", WGPU_STRLEN};
            renderPassDesc.colorAttachmentCount = 1;
            renderPassDesc.colorAttachments = &colorAttachment;

            WGPURenderPassEncoder renderPass =
                wgpuCommandEncoderBeginRenderPass(frame.encoder, &renderPassDesc);
            wgpuRenderPassEncoderSetPipeline(renderPass, m_pipeline);
            wgpuRenderPassEncoderSetBindGroup(renderPass, 0, m_bindGroup, 0, nullptr);
            wgpuRenderPassEncoderDraw(renderPass, 3, 1, 0, 0);
            wgpuRenderPassEncoderEnd(renderPass);
            wgpuRenderPassEncoderRelease(renderPass);
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

            if (m_volumeView)
            {
                wgpuTextureViewRelease(m_volumeView);
                m_volumeView = nullptr;
            }

            if (m_volumeTexture)
            {
                wgpuTextureRelease(m_volumeTexture);
                m_volumeTexture = nullptr;
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
        bool CreateVolumeResources(pwgpu::test::SampleContext &ctx,
                                   const std::vector<uint8_t> &volume)
        {
            WGPUTextureDescriptor textureDesc{};
            textureDesc.label = {"vol_tex", WGPU_STRLEN};
            textureDesc.dimension = WGPUTextureDimension_3D;
            textureDesc.size = {kVolumeWidth, kVolumeHeight, kVolumeDepth};
            textureDesc.mipLevelCount = 1;
            textureDesc.sampleCount = 1;
            textureDesc.format = WGPUTextureFormat_R8Unorm;
            textureDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
            m_volumeTexture = wgpuDeviceCreateTexture(ctx.device, &textureDesc);
            if (!m_volumeTexture)
                return false;

            UploadVolumeTexture(ctx.queue, m_volumeTexture, volume.data());

            WGPUTextureViewDescriptor viewDesc{};
            viewDesc.format = WGPUTextureFormat_R8Unorm;
            viewDesc.dimension = WGPUTextureViewDimension_3D;
            viewDesc.baseMipLevel = 0;
            viewDesc.mipLevelCount = 1;
            viewDesc.baseArrayLayer = 0;
            viewDesc.arrayLayerCount = 1;
            viewDesc.aspect = WGPUTextureAspect_All;
            m_volumeView = wgpuTextureCreateView(m_volumeTexture, &viewDesc);
            if (!m_volumeView)
                return false;

            WGPUSamplerDescriptor samplerDesc{};
            samplerDesc.label = {"vol_sampler", WGPU_STRLEN};
            samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
            samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
            samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
            samplerDesc.magFilter = WGPUFilterMode_Linear;
            samplerDesc.minFilter = WGPUFilterMode_Linear;
            samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
            samplerDesc.lodMinClamp = 0.0f;
            samplerDesc.lodMaxClamp = 32.0f;
            samplerDesc.compare = WGPUCompareFunction_Undefined;
            samplerDesc.maxAnisotropy = 16;
            m_sampler = wgpuDeviceCreateSampler(ctx.device, &samplerDesc);
            return m_sampler != nullptr;
        }

        bool CreatePipeline(pwgpu::test::SampleContext &ctx)
        {
            WGPUBindGroupLayoutEntry layoutEntries[3] = {};
            layoutEntries[0].binding = 0;
            layoutEntries[0].visibility = WGPUShaderStage_Vertex;
            layoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
            layoutEntries[0].buffer.minBindingSize = 64;
            layoutEntries[1].binding = 1;
            layoutEntries[1].visibility = WGPUShaderStage_Fragment;
            layoutEntries[1].sampler.type = WGPUSamplerBindingType_Filtering;
            layoutEntries[2].binding = 2;
            layoutEntries[2].visibility = WGPUShaderStage_Fragment;
            layoutEntries[2].texture.sampleType = WGPUTextureSampleType_Float;
            layoutEntries[2].texture.viewDimension = WGPUTextureViewDimension_3D;
            layoutEntries[2].texture.multisampled = WGPUOptionalBool_False;

            WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc{};
            bindGroupLayoutDesc.label = {"vol_bgl", WGPU_STRLEN};
            bindGroupLayoutDesc.entryCount = 3;
            bindGroupLayoutDesc.entries = layoutEntries;
            m_bindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device, &bindGroupLayoutDesc);
            if (!m_bindGroupLayout)
                return false;

            WGPUPipelineLayoutDescriptor pipelineLayoutDesc{};
            pipelineLayoutDesc.label = {"vol_pl", WGPU_STRLEN};
            pipelineLayoutDesc.bindGroupLayoutCount = 1;
            pipelineLayoutDesc.bindGroupLayouts = &m_bindGroupLayout;
            m_pipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device, &pipelineLayoutDesc);
            if (!m_pipelineLayout)
                return false;

            WGPUBindGroupEntry bindGroupEntries[3] = {};
            bindGroupEntries[0].binding = 0;
            bindGroupEntries[0].buffer = m_uniformBuffer;
            bindGroupEntries[0].size = 64;
            bindGroupEntries[1].binding = 1;
            bindGroupEntries[1].sampler = m_sampler;
            bindGroupEntries[2].binding = 2;
            bindGroupEntries[2].textureView = m_volumeView;

            WGPUBindGroupDescriptor bindGroupDesc{};
            bindGroupDesc.label = {"vol_bg", WGPU_STRLEN};
            bindGroupDesc.layout = m_bindGroupLayout;
            bindGroupDesc.entryCount = 3;
            bindGroupDesc.entries = bindGroupEntries;
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
            pipelineDesc.label = {"vol_pipe", WGPU_STRLEN};
            pipelineDesc.layout = m_pipelineLayout;
            pipelineDesc.vertex.module = m_vertexShader;
            pipelineDesc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            pipelineDesc.primitive.cullMode = WGPUCullMode_Back;
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
        WGPUTexture m_volumeTexture = nullptr;
        WGPUTextureView m_volumeView = nullptr;
        WGPUSampler m_sampler = nullptr;
        WGPUBindGroupLayout m_bindGroupLayout = nullptr;
        WGPUPipelineLayout m_pipelineLayout = nullptr;
        WGPUBindGroup m_bindGroup = nullptr;
        WGPURenderPipeline m_pipeline = nullptr;
        WGPUTexture m_msaaTexture = nullptr;
        WGPUTextureView m_msaaView = nullptr;
        float m_rotation = 0.f;
    };

    pwgpu::test::SampleAppDesc MakeVolumeRenderingTexture3DDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "VolumeRenderingTexture3DTest";
        desc.windowTitle = "PhasmaWebGPU Volume Rendering - Texture 3D";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    pe::Log::Init();
    setvbuf(stdout, nullptr, _IONBF, 0);

    VolumeRenderingTexture3DSample sample;
    pwgpu::test::SampleApp app(sample, MakeVolumeRenderingTexture3DDesc());
    return app.Run(argc, argv);
}
