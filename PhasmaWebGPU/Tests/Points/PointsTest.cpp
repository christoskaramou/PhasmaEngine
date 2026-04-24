#include "../Common/SampleApp.h"
#include "../Common/SampleBase.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"

#include "Base/Log.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    constexpr float kPi = 3.14159265358979323846f;

    // Upstream defaults (dat.GUI): fixedSize=false, textured=false, size=10.
    // Draws the distance-sized-points + orange-pixel-shader pipeline.
    constexpr bool kFixedSize = false;
    constexpr bool kTextured = false;
    constexpr float kPointSize = 10.0f;

    // Fibonacci sphere: kNumPoints points on a unit sphere of radius 1.
    constexpr uint32_t kNumPoints = 1000;

    // 64x64 RGBA8 atlas standing in for upstream's canvas2d-rendered butterfly
    // emoji (we cannot round-trip HTML5 canvas font rendering in C++, so we
    // procedurally draw an equivalent two-winged silhouette — same purpose,
    // same ~pixel budget, renders identically through textured.pixel.hlsl).
    constexpr uint32_t kTextureSize = 64;

    void BuildFibonacciSphere(std::vector<float> &outPositions, uint32_t numSamples, float radius)
    {
        outPositions.resize(static_cast<size_t>(numSamples) * 3u);
        const float increment = kPi * (3.0f - std::sqrt(5.0f));
        const float offset = 2.0f / static_cast<float>(numSamples);
        for (uint32_t i = 0; i < numSamples; ++i)
        {
            float y = static_cast<float>(i) * offset - 1.0f + offset * 0.5f;
            float r = std::sqrt(std::max(0.0f, 1.0f - y * y));
            float phi = static_cast<float>(i) * increment;
            float x = std::cos(phi) * r;
            float z = std::sin(phi) * r;
            outPositions[i * 3u + 0u] = x * radius;
            outPositions[i * 3u + 1u] = y * radius;
            outPositions[i * 3u + 2u] = z * radius;
        }
    }

    uint8_t ToUnorm8(float v)
    {
        if (v < 0.0f)
            v = 0.0f;
        if (v > 1.0f)
            v = 1.0f;
        return static_cast<uint8_t>(v * 255.0f + 0.5f);
    }

    // Procedurally draw a small butterfly silhouette centred in the texture:
    // two symmetric wing-ellipses + a thin body. Alpha is the silhouette mask
    // (textured.pixel.hlsl does alpha<0.1 discard).
    void GenerateButterflyTexture(std::vector<uint8_t> &pixels)
    {
        pixels.assign(static_cast<size_t>(kTextureSize) * kTextureSize * 4u, 0u);
        const float cx = static_cast<float>(kTextureSize) * 0.5f;
        const float cy = static_cast<float>(kTextureSize) * 0.5f;

        for (uint32_t y = 0; y < kTextureSize; ++y)
        {
            for (uint32_t x = 0; x < kTextureSize; ++x)
            {
                float px = static_cast<float>(x) + 0.5f - cx;
                float py = static_cast<float>(y) + 0.5f - cy;

                // Body: narrow vertical ellipse at the centre.
                float bodyRx = 2.5f;
                float bodyRy = 12.0f;
                float bodyV = (px / bodyRx) * (px / bodyRx) + (py / bodyRy) * (py / bodyRy);

                // Upper wings: two ovals above centre, offset left/right.
                float upperCy = -8.0f;
                float upperRx = 12.0f;
                float upperRy = 10.0f;
                float leftUpper = ((px + 10.0f) / upperRx) * ((px + 10.0f) / upperRx) +
                                  ((py - upperCy) / upperRy) * ((py - upperCy) / upperRy);
                float rightUpper = ((px - 10.0f) / upperRx) * ((px - 10.0f) / upperRx) +
                                   ((py - upperCy) / upperRy) * ((py - upperCy) / upperRy);

                // Lower wings: smaller ovals below centre.
                float lowerCy = 9.0f;
                float lowerRx = 9.0f;
                float lowerRy = 8.0f;
                float leftLower = ((px + 7.0f) / lowerRx) * ((px + 7.0f) / lowerRx) +
                                  ((py - lowerCy) / lowerRy) * ((py - lowerCy) / lowerRy);
                float rightLower = ((px - 7.0f) / lowerRx) * ((px - 7.0f) / lowerRx) +
                                   ((py - lowerCy) / lowerRy) * ((py - lowerCy) / lowerRy);

                float shape = std::min({bodyV, leftUpper, rightUpper, leftLower, rightLower});
                if (shape > 1.0f)
                    continue;

                // Soft edge: alpha = 1 inside the shape, falling to 0 at boundary.
                float alpha = 1.0f - std::max(0.0f, shape - 0.7f) / 0.3f;
                if (alpha <= 0.0f)
                    continue;

                // Orange-ish butterfly body + lighter wings for visual contrast.
                float r, g, b;
                if (bodyV < 1.0f)
                {
                    r = 0.25f;
                    g = 0.15f;
                    b = 0.10f;
                }
                else
                {
                    r = 1.0f;
                    g = 0.55f;
                    b = 0.15f;
                }

                uint8_t *out = pixels.data() + (static_cast<size_t>(y) * kTextureSize + x) * 4u;
                out[0] = ToUnorm8(r);
                out[1] = ToUnorm8(g);
                out[2] = ToUnorm8(b);
                out[3] = ToUnorm8(alpha);
            }
        }
    }

    class PointsSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            m_distanceVS = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device,
                (ctx.shaderDir + "distance-sized-points.vert.hlsl").c_str(),
                "distance_vs");
            m_fixedVS = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device,
                (ctx.shaderDir + "fixed-size-points.vert.hlsl").c_str(),
                "fixed_vs");
            m_orangePS = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "orange.pixel.hlsl").c_str(), "orange_ps");
            m_texturedPS = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "textured.pixel.hlsl").c_str(), "textured_ps");
            if (!m_distanceVS || !m_fixedVS || !m_orangePS || !m_texturedPS)
                return false;

            // Build point-cloud vertex buffer.
            std::vector<float> positions;
            BuildFibonacciSphere(positions, kNumPoints, 1.0f);
            const uint64_t vbSize = static_cast<uint64_t>(positions.size() * sizeof(float));
            m_vertexBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                       vbSize,
                                                       WGPUBufferUsage_Vertex |
                                                           WGPUBufferUsage_CopyDst,
                                                       "points_vb");
            if (!m_vertexBuffer)
                return false;
            wgpuQueueWriteBuffer(ctx.queue, m_vertexBuffer, 0, positions.data(), vbSize);

            // Uniform buffer: mat4 (64) + vec2 resolution (8) + float size (4) + pad (4) = 80.
            m_uniformBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                        kUniformSize,
                                                        WGPUBufferUsage_Uniform |
                                                            WGPUBufferUsage_CopyDst,
                                                        "points_ubo");
            if (!m_uniformBuffer)
                return false;

            if (!CreateTextureAndSampler(ctx))
                return false;

            if (!CreatePipelineResources(ctx))
                return false;

            Resize(ctx, ctx.width, ctx.height);
            return m_depthView != nullptr;
        }

        void Resize(pwgpu::test::SampleContext &ctx, uint32_t width, uint32_t height) override
        {
            ReleaseDepthTarget();
            if (width == 0 || height == 0)
                return;

            WGPUTextureDescriptor depthDesc{};
            depthDesc.label = {"points_depth", WGPU_STRLEN};
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

            // Upstream: fov=90deg, near=0.1, far=50, lookAt eye=(0,0,1.5) target=0 up=+Y.
            mat4 projection =
                glm::perspectiveRH_ZO((90.0f * kPi) / 180.0f, aspect, 0.1f, 50.0f);
            projection[1][1] *= -1.f; // Y-flip for Vulkan y-down NDC.

            mat4 view = glm::lookAtRH(vec3(0.0f, 0.0f, 1.5f),
                                      vec3(0.0f, 0.0f, 0.0f),
                                      vec3(0.0f, 1.0f, 0.0f));
            mat4 vp = projection * view;

            float time = static_cast<float>(ctx.timing.elapsedSeconds);
            mat4 mvp = glm::rotate(vp, time, vec3(0.0f, 1.0f, 0.0f));
            mvp = glm::rotate(mvp, time * 0.1f, vec3(1.0f, 0.0f, 0.0f));

            // Pack uniforms: mat4 (64B) + vec2 resolution (8B) + float size (4B) + pad (4B).
            float uniforms[20] = {};
            std::memcpy(uniforms, glm::value_ptr(mvp), 64);
            uniforms[16] = static_cast<float>(ctx.width);
            uniforms[17] = static_cast<float>(ctx.height);
            uniforms[18] = kPointSize;
            uniforms[19] = 0.0f;

            wgpuQueueWriteBuffer(ctx.queue, m_uniformBuffer, 0, uniforms, kUniformSize);
        }

        bool Execute(pwgpu::test::SampleContext &, pwgpu::test::SampleFrame &frame) override
        {
            if (!m_depthView)
                return true;

            WGPURenderPassEncoder pass = BeginRenderPass(
                frame, "points_pass", WGPUColor{0.3, 0.3, 0.3, 1.0}, m_depthView);

            // Pick pipeline from hardcoded GUI defaults:
            //   pipelines[fixedSize?1:0][textured?1:0].
            WGPURenderPipeline pipeline = m_pipelines[kFixedSize ? 1 : 0][kTextured ? 1 : 0];
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, m_bindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetVertexBuffer(pass,
                                                 0,
                                                 m_vertexBuffer,
                                                 0,
                                                 static_cast<uint64_t>(kNumPoints) * 12ull);
            wgpuRenderPassEncoderDraw(pass, 6, kNumPoints, 0, 0);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            ReleaseDepthTarget();

            for (int v = 0; v < 2; ++v)
            {
                for (int f = 0; f < 2; ++f)
                {
                    if (m_pipelines[v][f])
                    {
                        wgpuRenderPipelineRelease(m_pipelines[v][f]);
                        m_pipelines[v][f] = nullptr;
                    }
                }
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
            if (m_vertexBuffer)
            {
                wgpuBufferRelease(m_vertexBuffer);
                m_vertexBuffer = nullptr;
            }
            if (m_texturedPS)
            {
                wgpuShaderModuleRelease(m_texturedPS);
                m_texturedPS = nullptr;
            }
            if (m_orangePS)
            {
                wgpuShaderModuleRelease(m_orangePS);
                m_orangePS = nullptr;
            }
            if (m_fixedVS)
            {
                wgpuShaderModuleRelease(m_fixedVS);
                m_fixedVS = nullptr;
            }
            if (m_distanceVS)
            {
                wgpuShaderModuleRelease(m_distanceVS);
                m_distanceVS = nullptr;
            }
        }

    private:
        bool CreateTextureAndSampler(pwgpu::test::SampleContext &ctx)
        {
            std::vector<uint8_t> pixels;
            GenerateButterflyTexture(pixels);

            WGPUTextureDescriptor textureDesc{};
            textureDesc.label = {"points_tex", WGPU_STRLEN};
            textureDesc.dimension = WGPUTextureDimension_2D;
            textureDesc.size = {kTextureSize, kTextureSize, 1};
            textureDesc.mipLevelCount = 1;
            textureDesc.sampleCount = 1;
            textureDesc.format = WGPUTextureFormat_RGBA8Unorm;
            textureDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
            m_texture = wgpuDeviceCreateTexture(ctx.device, &textureDesc);
            if (!m_texture)
                return false;

            pwgpu::test::UploadRGBA8Texture(
                ctx.queue, m_texture, pixels.data(), kTextureSize, kTextureSize);

            m_textureView = pwgpu::test::CreateTextureView(m_texture, WGPUTextureFormat_RGBA8Unorm);
            if (!m_textureView)
                return false;

            WGPUSamplerDescriptor samplerDesc{};
            samplerDesc.label = {"points_sampler", WGPU_STRLEN};
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
            // Shared bind-group layout across all 4 pipelines (matches upstream).
            WGPUBindGroupLayoutEntry entries[3] = {};
            entries[0].binding = 0;
            entries[0].visibility = WGPUShaderStage_Vertex;
            entries[0].buffer.type = WGPUBufferBindingType_Uniform;
            entries[0].buffer.minBindingSize = kUniformSize;
            entries[1].binding = 1;
            entries[1].visibility = WGPUShaderStage_Fragment;
            entries[1].sampler.type = WGPUSamplerBindingType_Filtering;
            entries[2].binding = 2;
            entries[2].visibility = WGPUShaderStage_Fragment;
            entries[2].texture.sampleType = WGPUTextureSampleType_Float;
            entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;
            entries[2].texture.multisampled = WGPUOptionalBool_False;

            WGPUBindGroupLayoutDescriptor bglDesc{};
            bglDesc.label = {"points_bgl", WGPU_STRLEN};
            bglDesc.entryCount = 3;
            bglDesc.entries = entries;
            m_bindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device, &bglDesc);
            if (!m_bindGroupLayout)
                return false;

            WGPUPipelineLayoutDescriptor plDesc{};
            plDesc.label = {"points_pl", WGPU_STRLEN};
            plDesc.bindGroupLayoutCount = 1;
            plDesc.bindGroupLayouts = &m_bindGroupLayout;
            m_pipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device, &plDesc);
            if (!m_pipelineLayout)
                return false;

            WGPUBindGroupEntry bgEntries[3] = {};
            bgEntries[0].binding = 0;
            bgEntries[0].buffer = m_uniformBuffer;
            bgEntries[0].size = kUniformSize;
            bgEntries[1].binding = 1;
            bgEntries[1].sampler = m_sampler;
            bgEntries[2].binding = 2;
            bgEntries[2].textureView = m_textureView;

            WGPUBindGroupDescriptor bgDesc{};
            bgDesc.label = {"points_bg", WGPU_STRLEN};
            bgDesc.layout = m_bindGroupLayout;
            bgDesc.entryCount = 3;
            bgDesc.entries = bgEntries;
            m_bindGroup = wgpuDeviceCreateBindGroup(ctx.device, &bgDesc);
            if (!m_bindGroup)
                return false;

            // Instance-stepped vertex buffer: 3 floats per point, 1 quad (6 verts) per instance.
            WGPUVertexAttribute posAttr{};
            posAttr.format = WGPUVertexFormat_Float32x3;
            posAttr.offset = 0;
            posAttr.shaderLocation = 0;

            WGPUVertexBufferLayout vbLayout{};
            vbLayout.arrayStride = 3u * sizeof(float);
            vbLayout.stepMode = WGPUVertexStepMode_Instance;
            vbLayout.attributeCount = 1;
            vbLayout.attributes = &posAttr;

            // Premultiplied-alpha source-over blend (upstream).
            WGPUBlendState blend{};
            blend.color.operation = WGPUBlendOperation_Add;
            blend.color.srcFactor = WGPUBlendFactor_One;
            blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
            blend.alpha.operation = WGPUBlendOperation_Add;
            blend.alpha.srcFactor = WGPUBlendFactor_One;
            blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;

            WGPUShaderModule verts[2] = {m_distanceVS, m_fixedVS};
            WGPUShaderModule frags[2] = {m_orangePS, m_texturedPS};
            const char *pipeLabels[2][2] = {
                {"pipe_dist_orange", "pipe_dist_textured"},
                {"pipe_fixed_orange", "pipe_fixed_textured"},
            };

            for (int v = 0; v < 2; ++v)
            {
                for (int f = 0; f < 2; ++f)
                {
                    WGPUColorTargetState target{};
                    target.format = ctx.surfaceFormat;
                    target.writeMask = WGPUColorWriteMask_All;
                    target.blend = &blend;

                    WGPUFragmentState fragmentState{};
                    fragmentState.module = frags[f];
                    fragmentState.entryPoint = {"PSMain", WGPU_STRLEN};
                    fragmentState.targetCount = 1;
                    fragmentState.targets = &target;

                    WGPUDepthStencilState depthState{};
                    depthState.format = WGPUTextureFormat_Depth24Plus;
                    depthState.depthWriteEnabled = WGPUOptionalBool_True;
                    depthState.depthCompare = WGPUCompareFunction_Less;
                    depthState.stencilFront.compare = WGPUCompareFunction_Always;
                    depthState.stencilBack.compare = WGPUCompareFunction_Always;

                    WGPURenderPipelineDescriptor desc{};
                    desc.label = {pipeLabels[v][f], WGPU_STRLEN};
                    desc.layout = m_pipelineLayout;
                    desc.vertex.module = verts[v];
                    desc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
                    desc.vertex.bufferCount = 1;
                    desc.vertex.buffers = &vbLayout;
                    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
                    desc.primitive.cullMode = WGPUCullMode_None;
                    desc.primitive.frontFace = WGPUFrontFace_CCW;
                    desc.depthStencil = &depthState;
                    desc.multisample.count = 1;
                    desc.multisample.mask = 0xFFFFFFFF;
                    desc.fragment = &fragmentState;
                    m_pipelines[v][f] = wgpuDeviceCreateRenderPipeline(ctx.device, &desc);
                    if (!m_pipelines[v][f])
                        return false;
                }
            }
            return true;
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

        // mat4 (64) + vec2 (8) + float (4) + pad (4) = 80. Aligned to 16 for uniform buffer.
        static constexpr uint64_t kUniformSize = 80;

        WGPUShaderModule m_distanceVS = nullptr;
        WGPUShaderModule m_fixedVS = nullptr;
        WGPUShaderModule m_orangePS = nullptr;
        WGPUShaderModule m_texturedPS = nullptr;
        WGPUBuffer m_vertexBuffer = nullptr;
        WGPUBuffer m_uniformBuffer = nullptr;
        WGPUTexture m_texture = nullptr;
        WGPUTextureView m_textureView = nullptr;
        WGPUSampler m_sampler = nullptr;
        WGPUBindGroupLayout m_bindGroupLayout = nullptr;
        WGPUPipelineLayout m_pipelineLayout = nullptr;
        WGPUBindGroup m_bindGroup = nullptr;
        WGPURenderPipeline m_pipelines[2][2] = {};
        WGPUTexture m_depthTexture = nullptr;
        WGPUTextureView m_depthView = nullptr;
    };

    pwgpu::test::SampleAppDesc MakePointsDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "PointsTest";
        desc.windowTitle = "PhasmaWebGPU Points";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    pe::Log::Init();
    setvbuf(stdout, nullptr, _IONBF, 0);

    PointsSample sample;
    pwgpu::test::SampleApp app(sample, MakePointsDesc());
    return app.Run(argc, argv);
}
