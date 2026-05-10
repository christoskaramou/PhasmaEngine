#include "../Common/SampleApp.h"
#include "../Common/SampleBase.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"

#include "Base/Log.h"

#include <cmath>

namespace
{
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    constexpr uint32_t kTextureSize = 300;
    constexpr float kPi = 3.14159265358979323846f;

    struct RGBA
    {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 0.0f;
    };

    // CSS-like hsl to rgb where h,s,l are in [0,1].
    RGBA HslToRgb(float h, float s, float l)
    {
        float r = l;
        float g = l;
        float b = l;
        if (s > 0.0f)
        {
            auto hue2rgb = [](float p, float q, float t)
            {
                if (t < 0.0f)
                    t += 1.0f;
                if (t > 1.0f)
                    t -= 1.0f;
                if (t < 1.0f / 6.0f)
                    return p + (q - p) * 6.0f * t;
                if (t < 1.0f / 2.0f)
                    return q;
                if (t < 2.0f / 3.0f)
                    return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
                return p;
            };

            // Normalise hue to [0,1) matching CSS behaviour (supports negatives).
            float hn = h - std::floor(h);
            float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
            float p = 2.0f * l - q;
            r = hue2rgb(p, q, hn + 1.0f / 3.0f);
            g = hue2rgb(p, q, hn);
            b = hue2rgb(p, q, hn - 1.0f / 3.0f);
        }
        return {r, g, b, 1.0f};
    }

    uint8_t ToUnorm8(float v)
    {
        if (v < 0.0f)
            v = 0.0f;
        if (v > 1.0f)
            v = 1.0f;
        return static_cast<uint8_t>(v * 255.0f + 0.5f);
    }

    // Reproduces main.ts createSourceImage: 3 circles, screen-composited, radial alpha.
    void GenerateSourceImage(uint32_t size, std::vector<uint8_t> &pixels)
    {
        pixels.assign(size * size * 4u, 0u);
        std::vector<RGBA> accum(size * size, RGBA{0.0f, 0.0f, 0.0f, 0.0f});

        const float sizef = static_cast<float>(size);
        const float cx0 = sizef * 0.5f;
        const float cy0 = sizef * 0.5f;
        const float innerR = sizef / 6.0f;
        const float outerR = sizef / 3.0f;
        const int numCircles = 3;

        // Upstream accumulates rotations: at circle i, total rotation = (i+1) * 2pi/3.
        for (int i = 0; i < numCircles; ++i)
        {
            float angle = static_cast<float>(i + 1) * (2.0f * kPi / static_cast<float>(numCircles));
            float cx = cx0 + std::cos(angle) * innerR;
            float cy = cy0 + std::sin(angle) * innerR;
            float hue = static_cast<float>(i) / static_cast<float>(numCircles);
            RGBA color = HslToRgb(hue, 1.0f, 0.5f);

            for (uint32_t y = 0; y < size; ++y)
            {
                for (uint32_t x = 0; x < size; ++x)
                {
                    float dx = static_cast<float>(x) + 0.5f - cx;
                    float dy = static_cast<float>(y) + 0.5f - cy;
                    float d = std::sqrt(dx * dx + dy * dy);
                    if (d > outerR)
                        continue;

                    // CSS radial gradient: stop at 0.5 -> alpha 1, stop at 1.0 -> alpha 0.
                    // Normalised position t in [0,1] across the gradient from 0 to outerR.
                    float t = d / outerR;
                    float alpha;
                    if (t <= 0.5f)
                        alpha = 1.0f;
                    else
                        alpha = 1.0f - (t - 0.5f) / 0.5f;

                    // 2D "screen" composite: out = 1 - (1-dst) * (1-src), applied per
                    // component with source pre-alpha-weighted.
                    float sr = color.r * alpha;
                    float sg = color.g * alpha;
                    float sb = color.b * alpha;
                    float sa = alpha;

                    RGBA &dst = accum[y * size + x];
                    dst.r = 1.0f - (1.0f - dst.r) * (1.0f - sr);
                    dst.g = 1.0f - (1.0f - dst.g) * (1.0f - sg);
                    dst.b = 1.0f - (1.0f - dst.b) * (1.0f - sb);
                    dst.a = 1.0f - (1.0f - dst.a) * (1.0f - sa);
                }
            }
        }

        // Store as premultiplied-alpha rgba8.
        for (uint32_t y = 0; y < size; ++y)
        {
            for (uint32_t x = 0; x < size; ++x)
            {
                const RGBA &c = accum[y * size + x];
                uint8_t *out = pixels.data() + (y * size + x) * 4u;
                out[0] = ToUnorm8(c.r * c.a);
                out[1] = ToUnorm8(c.g * c.a);
                out[2] = ToUnorm8(c.b * c.a);
                out[3] = ToUnorm8(c.a);
            }
        }
    }

    // Reproduces main.ts createDestinationImage: diagonal HSL gradient + -45deg stripes knockout.
    void GenerateDestinationImage(uint32_t size, std::vector<uint8_t> &pixels)
    {
        pixels.assign(size * size * 4u, 0u);
        const float sizef = static_cast<float>(size);

        for (uint32_t y = 0; y < size; ++y)
        {
            for (uint32_t x = 0; x < size; ++x)
            {
                // Linear gradient from (0,0) to (size,size): t = (x+y)/(2*size).
                float t = (static_cast<float>(x) + 0.5f + static_cast<float>(y) + 0.5f) / (2.0f * sizef);
                if (t < 0.0f)
                    t = 0.0f;
                if (t > 1.0f)
                    t = 1.0f;

                float segF = t * 6.0f;
                int idx = static_cast<int>(std::floor(segF));
                if (idx < 0)
                    idx = 0;
                if (idx > 5)
                    idx = 5;
                float local = segF - static_cast<float>(idx);

                float hueA = -static_cast<float>(idx) / 6.0f;
                float hueB = -static_cast<float>(idx + 1) / 6.0f;
                RGBA a = HslToRgb(hueA, 1.0f, 0.5f);
                RGBA b = HslToRgb(hueB, 1.0f, 0.5f);
                float r = a.r + (b.r - a.r) * local;
                float g = a.g + (b.g - a.g) * local;
                float bl = a.b + (b.b - a.b) * local;

                float alpha = 1.0f;

                // Knockout stripes: upstream rotates canvas -pi/4 then draws horizontal
                // stripes at y' = 0, 32, 64, ... of height 16. In the un-rotated pixel
                // frame the stripe coordinate is y' = (-x + y)/sqrt(2). Keep pixels
                // whose y' mod 32 lies in [16, 32) (the gap between stripes); knock out
                // those in [0, 16).
                float rotY = (-static_cast<float>(x) - 0.5f + static_cast<float>(y) + 0.5f) * 0.70710678f;
                float modv = std::fmod(rotY, 32.0f);
                if (modv < 0.0f)
                    modv += 32.0f;
                if (modv < 16.0f)
                {
                    alpha = 0.0f;
                    r = 0.0f;
                    g = 0.0f;
                    bl = 0.0f;
                }

                uint8_t *out = pixels.data() + (y * size + x) * 4u;
                // Premultiplied storage.
                out[0] = ToUnorm8(r * alpha);
                out[1] = ToUnorm8(g * alpha);
                out[2] = ToUnorm8(bl * alpha);
                out[3] = ToUnorm8(alpha);
            }
        }
    }

    class BlendingSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            m_vertexShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "texturedQuad.vert.hlsl").c_str(), "blend_vs");
            m_pixelShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "texturedQuad.pixel.hlsl").c_str(), "blend_ps");
            if (!m_vertexShader || !m_pixelShader)
                return false;

            if (!CreateTextures(ctx))
                return false;

            if (!CreateSampler(ctx))
                return false;

            m_srcUniformBuffer = pwgpu::test::CreateBuffer(
                ctx.device, 64, WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, "blend_src_ubo");
            m_dstUniformBuffer = pwgpu::test::CreateBuffer(
                ctx.device, 64, WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, "blend_dst_ubo");
            if (!m_srcUniformBuffer || !m_dstUniformBuffer)
                return false;

            if (!CreatePipelineResources(ctx))
                return false;

            return true;
        }

        void Resize(pwgpu::test::SampleContext &, uint32_t, uint32_t) override {}

        void Update(pwgpu::test::SampleContext &ctx) override
        {
            // mat4.ortho(0, w, h, 0, -1, 1) then scale([texW, texH, 1]).
            float w = static_cast<float>(ctx.width);
            float h = static_cast<float>(ctx.height);
            if (w <= 0.0f)
                w = 1.0f;
            if (h <= 0.0f)
                h = 1.0f;

            // Upstream uses ortho(0, w, h, 0) assuming WebGPU Y-up NDC.
            // For Vulkan (Y-down) use the standard (b=0, t=h) form; screen(0,0)
            // then maps to NDC y=-1 (framebuffer top), matching upstream visually.
            mat4 projection = glm::orthoRH_ZO(0.0f, w, 0.0f, h, -1.0f, 1.0f);

            mat4 srcMvp = glm::scale(projection,
                                     vec3(static_cast<float>(kTextureSize),
                                          static_cast<float>(kTextureSize),
                                          1.0f));
            mat4 dstMvp = srcMvp;

            wgpuQueueWriteBuffer(ctx.queue, m_srcUniformBuffer, 0, glm::value_ptr(srcMvp), 64);
            wgpuQueueWriteBuffer(ctx.queue, m_dstUniformBuffer, 0, glm::value_ptr(dstMvp), 64);
        }

        bool Execute(pwgpu::test::SampleContext &, pwgpu::test::SampleFrame &frame) override
        {
            WGPURenderPassEncoder pass =
                BeginRenderPass(frame, "blend_pass", WGPUColor{0.0, 0.0, 0.0, 0.0});

            // 1) draw destination texture (opaque, no blend).
            wgpuRenderPassEncoderSetPipeline(pass, m_dstPipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, m_dstBindGroup, 0, nullptr);
            wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);

            // 2) draw source texture with premultiplied source-over blend.
            wgpuRenderPassEncoderSetPipeline(pass, m_srcPipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, m_srcBindGroup, 0, nullptr);
            wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);

            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            if (m_srcPipeline)
            {
                wgpuRenderPipelineRelease(m_srcPipeline);
                m_srcPipeline = nullptr;
            }
            if (m_dstPipeline)
            {
                wgpuRenderPipelineRelease(m_dstPipeline);
                m_dstPipeline = nullptr;
            }
            if (m_pipelineLayout)
            {
                wgpuPipelineLayoutRelease(m_pipelineLayout);
                m_pipelineLayout = nullptr;
            }
            if (m_srcBindGroup)
            {
                wgpuBindGroupRelease(m_srcBindGroup);
                m_srcBindGroup = nullptr;
            }
            if (m_dstBindGroup)
            {
                wgpuBindGroupRelease(m_dstBindGroup);
                m_dstBindGroup = nullptr;
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
            if (m_srcTextureView)
            {
                wgpuTextureViewRelease(m_srcTextureView);
                m_srcTextureView = nullptr;
            }
            if (m_dstTextureView)
            {
                wgpuTextureViewRelease(m_dstTextureView);
                m_dstTextureView = nullptr;
            }
            if (m_srcTexture)
            {
                wgpuTextureRelease(m_srcTexture);
                m_srcTexture = nullptr;
            }
            if (m_dstTexture)
            {
                wgpuTextureRelease(m_dstTexture);
                m_dstTexture = nullptr;
            }
            if (m_srcUniformBuffer)
            {
                wgpuBufferRelease(m_srcUniformBuffer);
                m_srcUniformBuffer = nullptr;
            }
            if (m_dstUniformBuffer)
            {
                wgpuBufferRelease(m_dstUniformBuffer);
                m_dstUniformBuffer = nullptr;
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
        bool CreateTextures(pwgpu::test::SampleContext &ctx)
        {
            std::vector<uint8_t> srcPixels;
            std::vector<uint8_t> dstPixels;
            GenerateSourceImage(kTextureSize, srcPixels);
            GenerateDestinationImage(kTextureSize, dstPixels);

            auto makeTex = [&](const char *label) -> WGPUTexture
            {
                WGPUTextureDescriptor desc{};
                desc.label = {label, WGPU_STRLEN};
                desc.dimension = WGPUTextureDimension_2D;
                desc.size = {kTextureSize, kTextureSize, 1};
                desc.mipLevelCount = 1;
                desc.sampleCount = 1;
                desc.format = WGPUTextureFormat_RGBA8Unorm;
                desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
                return wgpuDeviceCreateTexture(ctx.device, &desc);
            };

            m_srcTexture = makeTex("blend_src_tex");
            m_dstTexture = makeTex("blend_dst_tex");
            if (!m_srcTexture || !m_dstTexture)
                return false;

            pwgpu::test::UploadRGBA8Texture(ctx.queue, m_srcTexture, srcPixels.data(), kTextureSize, kTextureSize);
            pwgpu::test::UploadRGBA8Texture(ctx.queue, m_dstTexture, dstPixels.data(), kTextureSize, kTextureSize);

            m_srcTextureView = pwgpu::test::CreateTextureView(m_srcTexture, WGPUTextureFormat_RGBA8Unorm);
            m_dstTextureView = pwgpu::test::CreateTextureView(m_dstTexture, WGPUTextureFormat_RGBA8Unorm);
            return m_srcTextureView != nullptr && m_dstTextureView != nullptr;
        }

        bool CreateSampler(pwgpu::test::SampleContext &ctx)
        {
            WGPUSamplerDescriptor desc{};
            desc.label = {"blend_sampler", WGPU_STRLEN};
            desc.addressModeU = WGPUAddressMode_ClampToEdge;
            desc.addressModeV = WGPUAddressMode_ClampToEdge;
            desc.addressModeW = WGPUAddressMode_ClampToEdge;
            desc.magFilter = WGPUFilterMode_Linear;
            desc.minFilter = WGPUFilterMode_Linear;
            desc.mipmapFilter = WGPUMipmapFilterMode_Linear;
            desc.lodMinClamp = 0.0f;
            desc.lodMaxClamp = 32.0f;
            desc.compare = WGPUCompareFunction_Undefined;
            desc.maxAnisotropy = 1;
            m_sampler = wgpuDeviceCreateSampler(ctx.device, &desc);
            return m_sampler != nullptr;
        }

        bool CreatePipelineResources(pwgpu::test::SampleContext &ctx)
        {
            WGPUBindGroupLayoutEntry entries[3] = {};
            entries[0].binding = 0;
            entries[0].visibility = WGPUShaderStage_Fragment;
            entries[0].sampler.type = WGPUSamplerBindingType_Filtering;
            entries[1].binding = 1;
            entries[1].visibility = WGPUShaderStage_Fragment;
            entries[1].texture.sampleType = WGPUTextureSampleType_Float;
            entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
            entries[1].texture.multisampled = WGPUOptionalBool_False;
            entries[2].binding = 2;
            entries[2].visibility = WGPUShaderStage_Vertex;
            entries[2].buffer.type = WGPUBufferBindingType_Uniform;
            entries[2].buffer.minBindingSize = 64;

            WGPUBindGroupLayoutDescriptor bglDesc{};
            bglDesc.label = {"blend_bgl", WGPU_STRLEN};
            bglDesc.entryCount = 3;
            bglDesc.entries = entries;
            m_bindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device, &bglDesc);
            if (!m_bindGroupLayout)
                return false;

            WGPUPipelineLayoutDescriptor plDesc{};
            plDesc.label = {"blend_pl", WGPU_STRLEN};
            plDesc.bindGroupLayoutCount = 1;
            plDesc.bindGroupLayouts = &m_bindGroupLayout;
            m_pipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device, &plDesc);
            if (!m_pipelineLayout)
                return false;

            auto makeBindGroup = [&](WGPUTextureView view, WGPUBuffer ubo, const char *label) -> WGPUBindGroup
            {
                WGPUBindGroupEntry e[3] = {};
                e[0].binding = 0;
                e[0].sampler = m_sampler;
                e[1].binding = 1;
                e[1].textureView = view;
                e[2].binding = 2;
                e[2].buffer = ubo;
                e[2].size = 64;

                WGPUBindGroupDescriptor d{};
                d.label = {label, WGPU_STRLEN};
                d.layout = m_bindGroupLayout;
                d.entryCount = 3;
                d.entries = e;
                return wgpuDeviceCreateBindGroup(ctx.device, &d);
            };

            m_srcBindGroup = makeBindGroup(m_srcTextureView, m_srcUniformBuffer, "blend_src_bg");
            m_dstBindGroup = makeBindGroup(m_dstTextureView, m_dstUniformBuffer, "blend_dst_bg");
            if (!m_srcBindGroup || !m_dstBindGroup)
                return false;

            // Shared fragment target (format). The src pipeline adds a blend state.
            WGPUBlendState blend{};
            blend.color.operation = WGPUBlendOperation_Add;
            blend.color.srcFactor = WGPUBlendFactor_One;
            blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
            blend.alpha.operation = WGPUBlendOperation_Add;
            blend.alpha.srcFactor = WGPUBlendFactor_One;
            blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;

            auto makePipeline = [&](bool enableBlend, const char *label) -> WGPURenderPipeline
            {
                WGPUColorTargetState target{};
                target.format = ctx.surfaceFormat;
                target.writeMask = WGPUColorWriteMask_All;
                target.blend = enableBlend ? &blend : nullptr;

                WGPUFragmentState fs{};
                fs.module = m_pixelShader;
                fs.entryPoint = {"PSMain", WGPU_STRLEN};
                fs.targetCount = 1;
                fs.targets = &target;

                WGPURenderPipelineDescriptor desc{};
                desc.label = {label, WGPU_STRLEN};
                desc.layout = m_pipelineLayout;
                desc.vertex.module = m_vertexShader;
                desc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
                desc.vertex.bufferCount = 0;
                desc.vertex.buffers = nullptr;
                desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
                desc.primitive.cullMode = WGPUCullMode_None;
                desc.primitive.frontFace = WGPUFrontFace_CCW;
                desc.depthStencil = nullptr;
                desc.multisample.count = 1;
                desc.multisample.mask = 0xFFFFFFFF;
                desc.fragment = &fs;
                return wgpuDeviceCreateRenderPipeline(ctx.device, &desc);
            };

            m_dstPipeline = makePipeline(false, "blend_pipe_dst");
            m_srcPipeline = makePipeline(true, "blend_pipe_src");
            return m_dstPipeline != nullptr && m_srcPipeline != nullptr;
        }

        WGPUShaderModule m_vertexShader = nullptr;
        WGPUShaderModule m_pixelShader = nullptr;
        WGPUBuffer m_srcUniformBuffer = nullptr;
        WGPUBuffer m_dstUniformBuffer = nullptr;
        WGPUTexture m_srcTexture = nullptr;
        WGPUTexture m_dstTexture = nullptr;
        WGPUTextureView m_srcTextureView = nullptr;
        WGPUTextureView m_dstTextureView = nullptr;
        WGPUSampler m_sampler = nullptr;
        WGPUBindGroupLayout m_bindGroupLayout = nullptr;
        WGPUPipelineLayout m_pipelineLayout = nullptr;
        WGPUBindGroup m_srcBindGroup = nullptr;
        WGPUBindGroup m_dstBindGroup = nullptr;
        WGPURenderPipeline m_srcPipeline = nullptr;
        WGPURenderPipeline m_dstPipeline = nullptr;
    };

    pwgpu::test::SampleAppDesc MakeBlendingDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "BlendingTest";
        desc.windowTitle = "PhasmaWebGPU Blending";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    pe::Log::Init();
    setvbuf(stdout, nullptr, _IONBF, 0);

    BlendingSample sample;
    pwgpu::test::SampleApp app(sample, MakeBlendingDesc());
    return app.Run(argc, argv);
}
