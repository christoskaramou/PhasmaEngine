// Cornell Box port of webgpu-samples/cornell.
//
// Ports all 7 upstream TS modules (common, scene, radiosity, rasterizer,
// raytracer, tonemapper, main) into a single .cpp. Renders a cornell box with
// software progressive radiosity baked into a 2D-array lightmap, then either
// rasterizes or path-traces the scene into an HDR framebuffer, then tonemaps
// that to the swapchain.
//
// Key structural deviation from upstream:
//   - The tonemapper here is a full-screen fragment pipeline rather than a
//     compute pipeline that storage-writes into the swapchain texture. That
//     keeps the surface configuration portable (RenderAttachment-only) and
//     avoids any dependency on bgra8unorm-storage or surface StorageBinding.
//   - The radiosity lightmap write pass uses RWTexture2DArray (HLSL read-write
//     storage) instead of the WGSL write-only storage texture, which matches
//     the engine's RWTexture2D pattern.
//   - The photon-tracing pass uses the fixed lightmap dimensions directly, so
//     only the accumulation->lightmap pass binds the lightmap as a storage image.
//
// All other pipeline shapes, RNG constants, photon counts, camera animation,
// and scene geometry are 1:1 with upstream main.ts defaults:
//   - renderer='rasterizer' (default)
//   - rotateCamera=true
//   - PhotonsPerWorkgroup=256, WorkgroupsPerFrame=1024, PhotonEnergy=100000
//   - Lightmap: 256x256 rgba16float, one array layer per quad
//   - Tonemap: Reinhard, Exposure=0.5, Gamma=2.2

#include "../Common/SampleApp.h"
#include "../Common/SampleBase.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"


#include <cmath>
#include <cstring>

namespace
{
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    constexpr float kPi = 3.14159265358979323846f;

    constexpr uint32_t kLightmapWidth = 256;
    constexpr uint32_t kLightmapHeight = 256;
    constexpr WGPUTextureFormat kLightmapFormat = WGPUTextureFormat_RGBA16Float;
    constexpr WGPUTextureFormat kFramebufferFormat = WGPUTextureFormat_RGBA16Float;

    // radiosity.ts defaults:
    constexpr uint32_t kPhotonsPerWorkgroup = 256;
    constexpr uint32_t kWorkgroupsPerFrame = 1024;
    constexpr double kPhotonEnergy = 100000.0;
    constexpr double kAccumulationMeanMax = static_cast<double>(0x10000000);

    constexpr uint32_t kAccumToLightmapWgX = 16;
    constexpr uint32_t kAccumToLightmapWgY = 16;

    constexpr uint32_t kRaytracerWgX = 16;
    constexpr uint32_t kRaytracerWgY = 16;

    // main.ts: renderer: 'rasterizer' (default).
    enum class Renderer
    {
        Rasterizer,
        Raytracer,
    };
    constexpr Renderer kDefaultRenderer = Renderer::Rasterizer;
    constexpr bool kRotateCamera = true;

    // ---------- scene.ts port ----------

    struct QuadDesc
    {
        vec3 center;
        vec3 right;
        vec3 up;
        vec3 color;
        float emissive = 0.0f;
    };

    // GPU layout (16 floats = 64 bytes), matches the WGSL Quad struct:
    //   plane:vec4f, right:vec4f, up:vec4f, color:vec3f, emissive:f32
    struct QuadGpu
    {
        float plane[4];
        float right[4];
        float up[4];
        float color[3];
        float emissive;
    };
    static_assert(sizeof(QuadGpu) == 64, "Quad GPU stride must be 64 bytes");

    struct Vertex
    {
        float pos[4];
        float uv[3]; // xy=uv, z=quad index
        float emissive[3];
    };
    static_assert(sizeof(Vertex) == 40, "Vertex stride must be 40 bytes");

    static vec3 Reciprocal(const vec3 &v)
    {
        float sq = v.x * v.x + v.y * v.y + v.z * v.z;
        float s = 1.0f / sq;
        return vec3(v.x * s, v.y * s, v.z * s);
    }

    enum CubeFace
    {
        FacePositiveX = 0,
        FacePositiveY = 1,
        FacePositiveZ = 2,
        FaceNegativeX = 3,
        FaceNegativeY = 4,
        FaceNegativeZ = 5,
    };

    struct BoxParams
    {
        vec3 center;
        float width;
        float height;
        float depth;
        float rotation;
        std::array<vec3, 6> faceColors; // Per-face color (or 6 copies of one)
        bool concave;
    };

    static void AppendBox(const BoxParams &p, std::vector<QuadDesc> &out)
    {
        vec3 x(std::cos(p.rotation) * (p.width * 0.5f),
               0.0f,
               std::sin(p.rotation) * (p.depth * 0.5f));
        vec3 y(0.0f, p.height * 0.5f, 0.0f);
        vec3 z(std::sin(p.rotation) * (p.width * 0.5f),
               0.0f,
               -std::cos(p.rotation) * (p.depth * 0.5f));

        auto sign = [&](const vec3 &v) -> vec3
        { return p.concave ? v : -v; };

        QuadDesc posX;
        posX.center = p.center + x;
        posX.right = sign(-z);
        posX.up = y;
        posX.color = p.faceColors[FacePositiveX];
        out.push_back(posX);

        QuadDesc posY;
        posY.center = p.center + y;
        posY.right = sign(x);
        posY.up = -z;
        posY.color = p.faceColors[FacePositiveY];
        out.push_back(posY);

        QuadDesc posZ;
        posZ.center = p.center + z;
        posZ.right = sign(x);
        posZ.up = y;
        posZ.color = p.faceColors[FacePositiveZ];
        out.push_back(posZ);

        QuadDesc negX;
        negX.center = p.center - x;
        negX.right = sign(z);
        negX.up = y;
        negX.color = p.faceColors[FaceNegativeX];
        out.push_back(negX);

        QuadDesc negY;
        negY.center = p.center - y;
        negY.right = sign(x);
        negY.up = z;
        negY.color = p.faceColors[FaceNegativeY];
        out.push_back(negY);

        QuadDesc negZ;
        negZ.center = p.center - z;
        negZ.right = sign(-x);
        negZ.up = y;
        negZ.color = p.faceColors[FaceNegativeZ];
        out.push_back(negZ);
    }

    struct SceneData
    {
        std::vector<QuadDesc> quads;
        std::vector<QuadGpu> quadsGpu;
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        vec3 lightCenter = vec3(0.0f);
        float lightWidth = 0.0f;
        float lightHeight = 0.0f;
    };

    static void BuildScene(SceneData &out)
    {
        // Box 0: the cornell walls (concave), per-face colors
        {
            BoxParams bp;
            bp.center = vec3(0.0f, 5.0f, 0.0f);
            bp.width = bp.height = bp.depth = 10.0f;
            bp.rotation = 0.0f;
            bp.faceColors = {
                vec3(0.0f, 0.5f, 0.0f), // +X
                vec3(0.5f, 0.5f, 0.5f), // +Y
                vec3(0.5f, 0.5f, 0.5f), // +Z
                vec3(0.5f, 0.0f, 0.0f), // -X
                vec3(0.5f, 0.5f, 0.5f), // -Y
                vec3(0.5f, 0.5f, 0.5f), // -Z
            };
            bp.concave = true;
            AppendBox(bp, out.quads);
        }
        // Box 1: small interior box (convex)
        {
            BoxParams bp;
            bp.center = vec3(1.5f, 1.5f, 1.0f);
            bp.width = bp.height = bp.depth = 3.0f;
            bp.rotation = 0.3f;
            bp.faceColors.fill(vec3(0.8f, 0.8f, 0.8f));
            bp.concave = false;
            AppendBox(bp, out.quads);
        }
        // Box 2: tall box (convex)
        {
            BoxParams bp;
            bp.center = vec3(-2.0f, 3.0f, -2.0f);
            bp.width = 3.0f;
            bp.height = 6.0f;
            bp.depth = 3.0f;
            bp.rotation = -0.4f;
            bp.faceColors.fill(vec3(0.8f, 0.8f, 0.8f));
            bp.concave = false;
            AppendBox(bp, out.quads);
        }
        // Light quad (emissive)
        {
            QuadDesc light;
            light.center = vec3(0.0f, 9.95f, 0.0f);
            light.right = vec3(1.0f, 0.0f, 0.0f);
            light.up = vec3(0.0f, 0.0f, 1.0f);
            light.color = vec3(5.0f, 5.0f, 5.0f);
            light.emissive = 1.0f;
            out.quads.push_back(light);
            out.lightCenter = light.center;
            out.lightWidth = glm::length(light.right) * 2.0f;
            out.lightHeight = glm::length(light.up) * 2.0f;
        }

        out.quadsGpu.resize(out.quads.size());
        out.vertices.reserve(out.quads.size() * 4);
        out.indices.reserve(out.quads.size() * 6);

        uint32_t vertexCount = 0;
        for (size_t qi = 0; qi < out.quads.size(); ++qi)
        {
            const QuadDesc &q = out.quads[qi];
            vec3 normal = glm::normalize(glm::cross(q.right, q.up));

            QuadGpu &g = out.quadsGpu[qi];
            g.plane[0] = normal.x;
            g.plane[1] = normal.y;
            g.plane[2] = normal.z;
            g.plane[3] = -glm::dot(normal, q.center);

            vec3 invRight = Reciprocal(q.right);
            g.right[0] = invRight.x;
            g.right[1] = invRight.y;
            g.right[2] = invRight.z;
            g.right[3] = -glm::dot(invRight, q.center);

            vec3 invUp = Reciprocal(q.up);
            g.up[0] = invUp.x;
            g.up[1] = invUp.y;
            g.up[2] = invUp.z;
            g.up[3] = -glm::dot(invUp, q.center);

            g.color[0] = q.color.x;
            g.color[1] = q.color.y;
            g.color[2] = q.color.z;
            g.emissive = q.emissive;

            // Build the 4 quad corners:
            //   a = center - right + up
            //   b = center + right + up
            //   c = center - right - up
            //   d = center + right - up
            vec3 a = q.center - q.right + q.up;
            vec3 b = q.center + q.right + q.up;
            vec3 c = q.center - q.right - q.up;
            vec3 d = q.center + q.right - q.up;

            auto pushVert = [&](const vec3 &p, float u, float v)
            {
                Vertex vx{};
                vx.pos[0] = p.x;
                vx.pos[1] = p.y;
                vx.pos[2] = p.z;
                vx.pos[3] = 1.0f;
                vx.uv[0] = u;
                vx.uv[1] = v;
                vx.uv[2] = static_cast<float>(qi);
                vx.emissive[0] = q.color.x * q.emissive;
                vx.emissive[1] = q.color.y * q.emissive;
                vx.emissive[2] = q.color.z * q.emissive;
                out.vertices.push_back(vx);
            };

            pushVert(a, 0.0f, 1.0f);
            pushVert(b, 1.0f, 1.0f);
            pushVert(c, 0.0f, 0.0f);
            pushVert(d, 1.0f, 0.0f);

            // a, c, b, b, c, d
            out.indices.push_back(vertexCount + 0);
            out.indices.push_back(vertexCount + 2);
            out.indices.push_back(vertexCount + 1);
            out.indices.push_back(vertexCount + 1);
            out.indices.push_back(vertexCount + 2);
            out.indices.push_back(vertexCount + 3);
            vertexCount += 4;
        }
    }

    // ---------- common.ts / GPU uniform layouts ----------

    // Matches CommonUniforms in common.inc.hlsl: mat4 mvp, mat4 inv_mvp, uint3 seed, uint quadCount.
    struct CommonUniformsCpu
    {
        float mvp[16];
        float invMvp[16];
        uint32_t seed[3];
        uint32_t quadCount;
    };
    static_assert(sizeof(CommonUniformsCpu) == 64 + 64 + 16, "CommonUniforms size");

    // Matches RadiosityUniforms in the compute shaders.
    struct RadiosityUniformsCpu
    {
        float accumulationToLightmapScale;
        float accumulationBufferScale;
        float lightWidth;
        float lightHeight;
        float lightCenter[3];
        float pad;
    };
    static_assert(sizeof(RadiosityUniformsCpu) == 32, "RadiosityUniforms size");

    // ---------- The sample ----------

    class CornellSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            BuildScene(m_scene);
            fprintf(stdout,
                    "[cornell] scene: %zu quads, %zu verts, %zu indices\n",
                    m_scene.quads.size(),
                    m_scene.vertices.size(),
                    m_scene.indices.size());

            if (!LoadShaders(ctx))
                return false;
            if (!CreateBuffers(ctx))
                return false;
            if (!CreateSamplersAndTextures(ctx))
                return false;
            if (!CreateBindGroupLayoutsAndPipelines(ctx))
                return false;
            if (!CreateStaticBindGroups(ctx))
                return false;
            return true;
        }

        void Resize(pwgpu::test::SampleContext &ctx, uint32_t width, uint32_t height) override
        {
            ReleaseSizedTargets();
            m_fbWidth = 0;
            m_fbHeight = 0;
            if (width == 0 || height == 0)
                return;
            m_fbWidth = width;
            m_fbHeight = height;

            // HDR framebuffer — upstream uses RENDER_ATTACHMENT|STORAGE_BINDING|TEXTURE_BINDING.
            {
                WGPUTextureDescriptor d{};
                d.label = {"cornell_hdr_fb", WGPU_STRLEN};
                d.dimension = WGPUTextureDimension_2D;
                d.size = {width, height, 1};
                d.mipLevelCount = 1;
                d.sampleCount = 1;
                d.format = kFramebufferFormat;
                d.usage = static_cast<WGPUTextureUsage>(
                    WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_StorageBinding |
                    WGPUTextureUsage_TextureBinding);
                m_framebuffer = wgpuDeviceCreateTexture(ctx.device, &d);
                if (!m_framebuffer)
                    return;
            }

            {
                WGPUTextureViewDescriptor v{};
                v.label = {"cornell_hdr_fb_view", WGPU_STRLEN};
                v.format = kFramebufferFormat;
                v.dimension = WGPUTextureViewDimension_2D;
                v.mipLevelCount = 1;
                v.arrayLayerCount = 1;
                v.aspect = WGPUTextureAspect_All;
                m_framebufferView = wgpuTextureCreateView(m_framebuffer, &v);
                if (!m_framebufferView)
                    return;
            }

            // Depth buffer for the rasterizer pass.
            {
                WGPUTextureDescriptor d{};
                d.label = {"cornell_depth", WGPU_STRLEN};
                d.dimension = WGPUTextureDimension_2D;
                d.size = {width, height, 1};
                d.mipLevelCount = 1;
                d.sampleCount = 1;
                d.format = WGPUTextureFormat_Depth24Plus;
                d.usage = WGPUTextureUsage_RenderAttachment;
                m_depthTexture = wgpuDeviceCreateTexture(ctx.device, &d);
                if (!m_depthTexture)
                    return;
            }

            {
                WGPUTextureViewDescriptor v{};
                v.label = {"cornell_depth_view", WGPU_STRLEN};
                v.format = WGPUTextureFormat_Depth24Plus;
                v.dimension = WGPUTextureViewDimension_2D;
                v.mipLevelCount = 1;
                v.arrayLayerCount = 1;
                v.aspect = WGPUTextureAspect_DepthOnly;
                m_depthView = wgpuTextureCreateView(m_depthTexture, &v);
            }

            RebuildRaytracerBindGroup(ctx.device);
            RebuildTonemapBindGroup(ctx.device);
        }

        void Update(pwgpu::test::SampleContext &ctx) override
        {
            // common.ts:
            //   projection = perspective(2*pi/8, aspect, 0.5, 100)
            //   viewRotation = rotateCamera ? frame/1000 : 0
            //   view = lookAt([sin(r)*15, 5, cos(r)*15], [0,5,0], [0,1,0])
            float aspect = ctx.height > 0
                               ? static_cast<float>(ctx.width) / static_cast<float>(ctx.height)
                               : 1.0f;
            mat4 projection = glm::perspectiveRH_ZO((2.0f * kPi) / 8.0f, aspect, 0.5f, 100.0f);
            projection[1][1] *= -1.0f; // Vulkan Y-flip for rasterizer path

            float viewRotation = kRotateCamera ? (static_cast<float>(m_frame) / 1000.0f) : 0.0f;
            vec3 eye(std::sin(viewRotation) * 15.0f, 5.0f, std::cos(viewRotation) * 15.0f);
            mat4 view = glm::lookAtRH(eye, vec3(0.0f, 5.0f, 0.0f), vec3(0.0f, 1.0f, 0.0f));

            mat4 mvp = projection * view;
            mat4 invMvp = glm::inverse(mvp);

            CommonUniformsCpu u{};
            std::memcpy(u.mvp, glm::value_ptr(mvp), 64);
            std::memcpy(u.invMvp, glm::value_ptr(invMvp), 64);
            u.seed[0] = m_rng();
            u.seed[1] = m_rng();
            u.seed[2] = m_rng();
            u.quadCount = static_cast<uint32_t>(m_scene.quads.size());
            wgpuQueueWriteBuffer(ctx.queue, m_commonUniformBuffer, 0, &u, sizeof(u));

            // Radiosity accumulation scale tracking (radiosity.ts.run).
            constexpr double kTotalLightmapTexels =
                static_cast<double>(kLightmapWidth) * kLightmapHeight;
            const double totalTexels =
                kTotalLightmapTexels * static_cast<double>(m_scene.quads.size());
            const double photonsPerFrame =
                static_cast<double>(kPhotonsPerWorkgroup) * kWorkgroupsPerFrame;
            m_accumulationMean += (photonsPerFrame * kPhotonEnergy) / totalTexels;

            float accumToLightmapScale = static_cast<float>(1.0 / m_accumulationMean);
            float accumBufferScale = 1.0f;
            if (m_accumulationMean > 2.0 * kAccumulationMeanMax)
            {
                accumBufferScale = 0.5f;
                m_accumulationMean *= 0.5;
            }

            RadiosityUniformsCpu ru{};
            ru.accumulationToLightmapScale = accumToLightmapScale;
            ru.accumulationBufferScale = accumBufferScale;
            ru.lightWidth = m_scene.lightWidth;
            ru.lightHeight = m_scene.lightHeight;
            ru.lightCenter[0] = m_scene.lightCenter.x;
            ru.lightCenter[1] = m_scene.lightCenter.y;
            ru.lightCenter[2] = m_scene.lightCenter.z;
            ru.pad = 0.0f;
            wgpuQueueWriteBuffer(ctx.queue, m_radiosityUniformBuffer, 0, &ru, sizeof(ru));

            m_frame++;
        }

        bool Execute(pwgpu::test::SampleContext &, pwgpu::test::SampleFrame &frame) override
        {
            if (!m_framebufferView || !m_depthView || !m_radiosityTraceBindGroup ||
                !m_radiosityBindGroup || !m_raytracerBindGroup || !m_tonemapBindGroup)
            {
                return true;
            }

            // Pass 1a: radiosity photon tracing writes the accumulation SSBO.
            // It intentionally does not bind the lightmap storage image; the
            // lightmap dimensions are fixed for this sample.
            {
                WGPUComputePassDescriptor pd{};
                pd.label = {"cornell_radiosity", WGPU_STRLEN};
                WGPUComputePassEncoder cp = wgpuCommandEncoderBeginComputePass(frame.encoder, &pd);
                wgpuComputePassEncoderSetBindGroup(cp, 0, m_commonBindGroup, 0, nullptr);
                wgpuComputePassEncoderSetBindGroup(cp, 1, m_radiosityTraceBindGroup, 0, nullptr);
                wgpuComputePassEncoderSetPipeline(cp, m_radiosityPipeline);
                wgpuComputePassEncoderDispatchWorkgroups(cp, kWorkgroupsPerFrame, 1, 1);
                wgpuComputePassEncoderEnd(cp);
                wgpuComputePassEncoderRelease(cp);
            }

            // Pass 1b: accumulation -> lightmap copy (reads accumulation SSBO,
            // writes lightmap storage texture).
            {
                WGPUComputePassDescriptor pd{};
                pd.label = {"cornell_accum_to_lightmap", WGPU_STRLEN};
                WGPUComputePassEncoder cp = wgpuCommandEncoderBeginComputePass(frame.encoder, &pd);
                wgpuComputePassEncoderSetBindGroup(cp, 0, m_commonBindGroup, 0, nullptr);
                wgpuComputePassEncoderSetBindGroup(cp, 1, m_radiosityBindGroup, 0, nullptr);
                wgpuComputePassEncoderSetPipeline(cp, m_accumToLightmapPipeline);
                const uint32_t wx =
                    (kLightmapWidth + kAccumToLightmapWgX - 1) / kAccumToLightmapWgX;
                const uint32_t wy =
                    (kLightmapHeight + kAccumToLightmapWgY - 1) / kAccumToLightmapWgY;
                wgpuComputePassEncoderDispatchWorkgroups(
                    cp, wx, wy, static_cast<uint32_t>(m_scene.quads.size()));
                wgpuComputePassEncoderEnd(cp);
                wgpuComputePassEncoderRelease(cp);
            }

            // Pass 2: rasterizer OR raytracer -> HDR framebuffer.
            if (kDefaultRenderer == Renderer::Rasterizer)
            {
                WGPURenderPassColorAttachment ca{};
                ca.view = m_framebufferView;
                ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
                ca.loadOp = WGPULoadOp_Clear;
                ca.storeOp = WGPUStoreOp_Store;
                ca.clearValue = {0.1, 0.2, 0.3, 1.0};

                WGPURenderPassDepthStencilAttachment da{};
                da.view = m_depthView;
                da.depthClearValue = 1.0f;
                da.depthLoadOp = WGPULoadOp_Clear;
                da.depthStoreOp = WGPUStoreOp_Store;
                da.stencilLoadOp = WGPULoadOp_Undefined;
                da.stencilStoreOp = WGPUStoreOp_Undefined;

                WGPURenderPassDescriptor pd{};
                pd.label = {"cornell_rasterizer", WGPU_STRLEN};
                pd.colorAttachmentCount = 1;
                pd.colorAttachments = &ca;
                pd.depthStencilAttachment = &da;

                WGPURenderPassEncoder rp = wgpuCommandEncoderBeginRenderPass(frame.encoder, &pd);
                wgpuRenderPassEncoderSetPipeline(rp, m_rasterizerPipeline);
                wgpuRenderPassEncoderSetBindGroup(rp, 0, m_commonBindGroup, 0, nullptr);
                wgpuRenderPassEncoderSetBindGroup(rp, 1, m_rasterizerBindGroup, 0, nullptr);
                wgpuRenderPassEncoderSetVertexBuffer(
                    rp, 0, m_vertexBuffer, 0, m_scene.vertices.size() * sizeof(Vertex));
                wgpuRenderPassEncoderSetIndexBuffer(
                    rp,
                    m_indexBuffer,
                    WGPUIndexFormat_Uint32,
                    0,
                    m_scene.indices.size() * sizeof(uint32_t));
                wgpuRenderPassEncoderDrawIndexed(
                    rp, static_cast<uint32_t>(m_scene.indices.size()), 1, 0, 0, 0);
                wgpuRenderPassEncoderEnd(rp);
                wgpuRenderPassEncoderRelease(rp);
            }
            else
            {
                WGPUComputePassDescriptor pd{};
                pd.label = {"cornell_raytracer", WGPU_STRLEN};
                WGPUComputePassEncoder cp = wgpuCommandEncoderBeginComputePass(frame.encoder, &pd);
                wgpuComputePassEncoderSetPipeline(cp, m_raytracerPipeline);
                wgpuComputePassEncoderSetBindGroup(cp, 0, m_commonBindGroup, 0, nullptr);
                wgpuComputePassEncoderSetBindGroup(cp, 1, m_raytracerBindGroup, 0, nullptr);
                const uint32_t wx = (m_fbWidth + kRaytracerWgX - 1) / kRaytracerWgX;
                const uint32_t wy = (m_fbHeight + kRaytracerWgY - 1) / kRaytracerWgY;
                wgpuComputePassEncoderDispatchWorkgroups(cp, wx, wy, 1);
                wgpuComputePassEncoderEnd(cp);
                wgpuComputePassEncoderRelease(cp);
            }

            // Pass 3: tonemap -> swapchain (full-screen triangle).
            {
                WGPURenderPassColorAttachment ca{};
                ca.view = frame.surfaceView;
                ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
                ca.loadOp = WGPULoadOp_Clear;
                ca.storeOp = WGPUStoreOp_Store;
                ca.clearValue = {0.0, 0.0, 0.0, 1.0};

                WGPURenderPassDescriptor pd{};
                pd.label = {"cornell_tonemap", WGPU_STRLEN};
                pd.colorAttachmentCount = 1;
                pd.colorAttachments = &ca;

                WGPURenderPassEncoder rp = wgpuCommandEncoderBeginRenderPass(frame.encoder, &pd);
                wgpuRenderPassEncoderSetPipeline(rp, m_tonemapPipeline);
                wgpuRenderPassEncoderSetBindGroup(rp, 0, m_tonemapBindGroup, 0, nullptr);
                wgpuRenderPassEncoderDraw(rp, 3, 1, 0, 0);
                wgpuRenderPassEncoderEnd(rp);
                wgpuRenderPassEncoderRelease(rp);
            }

            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            ReleaseSizedTargets();

            if (m_tonemapPipeline)
                wgpuRenderPipelineRelease(m_tonemapPipeline);
            if (m_raytracerPipeline)
                wgpuComputePipelineRelease(m_raytracerPipeline);
            if (m_rasterizerPipeline)
                wgpuRenderPipelineRelease(m_rasterizerPipeline);
            if (m_accumToLightmapPipeline)
                wgpuComputePipelineRelease(m_accumToLightmapPipeline);
            if (m_radiosityPipeline)
                wgpuComputePipelineRelease(m_radiosityPipeline);

            if (m_tonemapPipelineLayout)
                wgpuPipelineLayoutRelease(m_tonemapPipelineLayout);
            if (m_raytracerPipelineLayout)
                wgpuPipelineLayoutRelease(m_raytracerPipelineLayout);
            if (m_rasterizerPipelineLayout)
                wgpuPipelineLayoutRelease(m_rasterizerPipelineLayout);
            if (m_accumToLightmapPipelineLayout)
                wgpuPipelineLayoutRelease(m_accumToLightmapPipelineLayout);
            if (m_radiosityPipelineLayout)
                wgpuPipelineLayoutRelease(m_radiosityPipelineLayout);

            if (m_commonBindGroup)
                wgpuBindGroupRelease(m_commonBindGroup);
            if (m_radiosityTraceBindGroup)
                wgpuBindGroupRelease(m_radiosityTraceBindGroup);
            if (m_radiosityBindGroup)
                wgpuBindGroupRelease(m_radiosityBindGroup);
            if (m_rasterizerBindGroup)
                wgpuBindGroupRelease(m_rasterizerBindGroup);

            if (m_commonBgl)
                wgpuBindGroupLayoutRelease(m_commonBgl);
            if (m_radiosityTraceBgl)
                wgpuBindGroupLayoutRelease(m_radiosityTraceBgl);
            if (m_radiosityBgl)
                wgpuBindGroupLayoutRelease(m_radiosityBgl);
            if (m_rasterizerBgl)
                wgpuBindGroupLayoutRelease(m_rasterizerBgl);
            if (m_raytracerBgl)
                wgpuBindGroupLayoutRelease(m_raytracerBgl);
            if (m_tonemapBgl)
                wgpuBindGroupLayoutRelease(m_tonemapBgl);

            if (m_rasterizerSampler)
                wgpuSamplerRelease(m_rasterizerSampler);
            if (m_raytracerSampler)
                wgpuSamplerRelease(m_raytracerSampler);
            if (m_tonemapSampler)
                wgpuSamplerRelease(m_tonemapSampler);

            if (m_lightmapView)
                wgpuTextureViewRelease(m_lightmapView);
            if (m_lightmapStorageView)
                wgpuTextureViewRelease(m_lightmapStorageView);
            if (m_lightmap)
                wgpuTextureRelease(m_lightmap);

            if (m_accumulationBuffer)
                wgpuBufferRelease(m_accumulationBuffer);
            if (m_radiosityUniformBuffer)
                wgpuBufferRelease(m_radiosityUniformBuffer);
            if (m_commonUniformBuffer)
                wgpuBufferRelease(m_commonUniformBuffer);
            if (m_quadBuffer)
                wgpuBufferRelease(m_quadBuffer);
            if (m_indexBuffer)
                wgpuBufferRelease(m_indexBuffer);
            if (m_vertexBuffer)
                wgpuBufferRelease(m_vertexBuffer);

            if (m_tonemapVS)
                wgpuShaderModuleRelease(m_tonemapVS);
            if (m_tonemapPS)
                wgpuShaderModuleRelease(m_tonemapPS);
            if (m_raytracerCS)
                wgpuShaderModuleRelease(m_raytracerCS);
            if (m_rasterizerVS)
                wgpuShaderModuleRelease(m_rasterizerVS);
            if (m_rasterizerPS)
                wgpuShaderModuleRelease(m_rasterizerPS);
            if (m_accumToLightmapCS)
                wgpuShaderModuleRelease(m_accumToLightmapCS);
            if (m_radiosityCS)
                wgpuShaderModuleRelease(m_radiosityCS);
        }

    private:
        bool LoadShaders(pwgpu::test::SampleContext &ctx)
        {
            auto load = [&](const char *file, const char *label) -> WGPUShaderModule
            {
                return pwgpu::test::MakeRuntimeShaderModule(
                    ctx.device, (ctx.shaderDir + file).c_str(), label);
            };
            m_radiosityCS = load("radiosity.comp.hlsl", "cornell_radiosity");
            m_accumToLightmapCS = load("accumulationToLightmap.comp.hlsl", "cornell_accum2lm");
            m_rasterizerVS = load("rasterizer.vert.hlsl", "cornell_rast_vs");
            m_rasterizerPS = load("rasterizer.pixel.hlsl", "cornell_rast_ps");
            m_raytracerCS = load("raytracer.comp.hlsl", "cornell_raytracer");
            m_tonemapVS = load("tonemapper.vert.hlsl", "cornell_tonemap_vs");
            m_tonemapPS = load("tonemapper.pixel.hlsl", "cornell_tonemap_ps");
            return m_radiosityCS && m_accumToLightmapCS && m_rasterizerVS && m_rasterizerPS &&
                   m_raytracerCS && m_tonemapVS && m_tonemapPS;
        }

        bool CreateBuffers(pwgpu::test::SampleContext &ctx)
        {
            m_vertexBuffer = pwgpu::test::CreateBuffer(
                ctx.device,
                m_scene.vertices.size() * sizeof(Vertex),
                static_cast<WGPUBufferUsage>(WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst),
                "cornell_vb");
            m_indexBuffer = pwgpu::test::CreateBuffer(
                ctx.device,
                m_scene.indices.size() * sizeof(uint32_t),
                static_cast<WGPUBufferUsage>(WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst),
                "cornell_ib");
            m_quadBuffer = pwgpu::test::CreateBuffer(
                ctx.device,
                m_scene.quadsGpu.size() * sizeof(QuadGpu),
                static_cast<WGPUBufferUsage>(WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst),
                "cornell_quads");
            if (!m_vertexBuffer || !m_indexBuffer || !m_quadBuffer)
                return false;

            wgpuQueueWriteBuffer(ctx.queue,
                                 m_vertexBuffer,
                                 0,
                                 m_scene.vertices.data(),
                                 m_scene.vertices.size() * sizeof(Vertex));
            wgpuQueueWriteBuffer(ctx.queue,
                                 m_indexBuffer,
                                 0,
                                 m_scene.indices.data(),
                                 m_scene.indices.size() * sizeof(uint32_t));
            wgpuQueueWriteBuffer(ctx.queue,
                                 m_quadBuffer,
                                 0,
                                 m_scene.quadsGpu.data(),
                                 m_scene.quadsGpu.size() * sizeof(QuadGpu));

            m_commonUniformBuffer = pwgpu::test::CreateBuffer(
                ctx.device,
                sizeof(CommonUniformsCpu),
                static_cast<WGPUBufferUsage>(WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst),
                "cornell_common_u");
            m_radiosityUniformBuffer = pwgpu::test::CreateBuffer(
                ctx.device,
                sizeof(RadiosityUniformsCpu),
                static_cast<WGPUBufferUsage>(WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst),
                "cornell_rad_u");
            if (!m_commonUniformBuffer || !m_radiosityUniformBuffer)
                return false;

            // Accumulation buffer: 3 u32 per texel per quad + pad for 16-byte-aligned stride
            // like upstream (16 bytes / texel * 3 rgb + 4 pad). Upstream allocates
            // width*height*quads*16 bytes, then addresses as u32 array using
            // 3*(x + w*y + w*h*quad). We match: stride of 16 bytes per texel gives
            // width*height*quads*4 u32 slots; only 3 are used per texel (matches upstream).
            // A simpler and sufficient allocation is 12 bytes per texel per quad, but
            // we stay faithful to the upstream allocation size.
            const uint64_t accumBytes = static_cast<uint64_t>(kLightmapWidth) * kLightmapHeight *
                                        static_cast<uint64_t>(m_scene.quads.size()) * 16ull;
            m_accumulationBuffer = pwgpu::test::CreateBuffer(
                ctx.device,
                accumBytes,
                static_cast<WGPUBufferUsage>(WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst),
                "cornell_accum");
            if (!m_accumulationBuffer)
                return false;

            // Zero-init the accumulation buffer.
            std::vector<uint8_t> zeros(accumBytes, 0);
            // writeBuffer has a 4MB sweet spot; split if needed. Accum is likely 4*16=... small.
            // For our scene (16 quads * 256*256*16) = 16 MiB. Chunk it.
            const uint64_t chunk = 1ull << 22; // 4 MiB
            for (uint64_t off = 0; off < accumBytes; off += chunk)
            {
                uint64_t n = (accumBytes - off) < chunk ? (accumBytes - off) : chunk;
                wgpuQueueWriteBuffer(
                    ctx.queue, m_accumulationBuffer, off, zeros.data() + off, static_cast<size_t>(n));
            }

            return true;
        }

        bool CreateSamplersAndTextures(pwgpu::test::SampleContext &ctx)
        {
            {
                WGPUTextureDescriptor d{};
                d.label = {"cornell_lightmap", WGPU_STRLEN};
                d.dimension = WGPUTextureDimension_2D;
                d.size = {kLightmapWidth,
                          kLightmapHeight,
                          static_cast<uint32_t>(m_scene.quads.size())};
                d.mipLevelCount = 1;
                d.sampleCount = 1;
                d.format = kLightmapFormat;
                d.usage = static_cast<WGPUTextureUsage>(WGPUTextureUsage_TextureBinding |
                                                        WGPUTextureUsage_StorageBinding);
                m_lightmap = wgpuDeviceCreateTexture(ctx.device, &d);
                if (!m_lightmap)
                    return false;
            }

            {
                WGPUTextureViewDescriptor v{};
                v.label = {"cornell_lightmap_view", WGPU_STRLEN};
                v.format = kLightmapFormat;
                v.dimension = WGPUTextureViewDimension_2DArray;
                v.mipLevelCount = 1;
                v.arrayLayerCount = static_cast<uint32_t>(m_scene.quads.size());
                v.aspect = WGPUTextureAspect_All;
                m_lightmapView = wgpuTextureCreateView(m_lightmap, &v);
            }

            {
                WGPUTextureViewDescriptor v{};
                v.label = {"cornell_lightmap_storage_view", WGPU_STRLEN};
                v.format = kLightmapFormat;
                v.dimension = WGPUTextureViewDimension_2DArray;
                v.mipLevelCount = 1;
                v.arrayLayerCount = static_cast<uint32_t>(m_scene.quads.size());
                v.aspect = WGPUTextureAspect_All;
                m_lightmapStorageView = wgpuTextureCreateView(m_lightmap, &v);
            }
            if (!m_lightmapView || !m_lightmapStorageView)
                return false;

            {
                WGPUSamplerDescriptor s{};
                s.label = {"cornell_rast_sampler", WGPU_STRLEN};
                s.addressModeU = WGPUAddressMode_ClampToEdge;
                s.addressModeV = WGPUAddressMode_ClampToEdge;
                s.addressModeW = WGPUAddressMode_ClampToEdge;
                s.magFilter = WGPUFilterMode_Linear;
                s.minFilter = WGPUFilterMode_Linear;
                s.mipmapFilter = WGPUMipmapFilterMode_Nearest;
                s.maxAnisotropy = 1;
                m_rasterizerSampler = wgpuDeviceCreateSampler(ctx.device, &s);
            }
            {
                WGPUSamplerDescriptor s{};
                s.label = {"cornell_rt_sampler", WGPU_STRLEN};
                s.addressModeU = WGPUAddressMode_ClampToEdge;
                s.addressModeV = WGPUAddressMode_ClampToEdge;
                s.addressModeW = WGPUAddressMode_ClampToEdge;
                s.magFilter = WGPUFilterMode_Linear;
                s.minFilter = WGPUFilterMode_Linear;
                s.mipmapFilter = WGPUMipmapFilterMode_Nearest;
                s.maxAnisotropy = 1;
                m_raytracerSampler = wgpuDeviceCreateSampler(ctx.device, &s);
            }
            {
                WGPUSamplerDescriptor s{};
                s.label = {"cornell_tonemap_sampler", WGPU_STRLEN};
                s.addressModeU = WGPUAddressMode_ClampToEdge;
                s.addressModeV = WGPUAddressMode_ClampToEdge;
                s.addressModeW = WGPUAddressMode_ClampToEdge;
                s.magFilter = WGPUFilterMode_Nearest;
                s.minFilter = WGPUFilterMode_Nearest;
                s.mipmapFilter = WGPUMipmapFilterMode_Nearest;
                s.maxAnisotropy = 1;
                m_tonemapSampler = wgpuDeviceCreateSampler(ctx.device, &s);
            }
            return m_rasterizerSampler && m_raytracerSampler && m_tonemapSampler;
        }

        bool CreateBindGroupLayoutsAndPipelines(pwgpu::test::SampleContext &ctx)
        {
            // group 0: common — uniform + quads SSBO.
            // Visibility covers every stage whose SPIR-V may reference the binding.
            // common.inc.hlsl declares both bindings at file scope; any shader that
            // #includes it will emit a SPIR-V reference (even if the entry point
            // does not actually touch the symbol, DXC may retain the declaration
            // when an explicit vk::binding decoration is present). rasterizer.vert
            // includes common.inc.hlsl, so the Vertex stage can reference quads;
            // raytracer/radiosity CS obviously do. Cover Vertex|Fragment|Compute
            // on both entries to stay safe across stages.
            {
                WGPUBindGroupLayoutEntry e[2] = {};
                e[0].binding = 0;
                e[0].visibility = static_cast<WGPUShaderStage>(
                    WGPUShaderStage_Vertex | WGPUShaderStage_Fragment | WGPUShaderStage_Compute);
                e[0].buffer.type = WGPUBufferBindingType_Uniform;
                e[0].buffer.minBindingSize = sizeof(CommonUniformsCpu);
                e[1].binding = 1;
                e[1].visibility = static_cast<WGPUShaderStage>(
                    WGPUShaderStage_Vertex | WGPUShaderStage_Fragment | WGPUShaderStage_Compute);
                e[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
                e[1].buffer.minBindingSize = sizeof(QuadGpu);

                WGPUBindGroupLayoutDescriptor d{};
                d.label = {"cornell_common_bgl", WGPU_STRLEN};
                d.entryCount = 2;
                d.entries = e;
                m_commonBgl = wgpuDeviceCreateBindGroupLayout(ctx.device, &d);
                if (!m_commonBgl)
                    return false;
            }

            // group 1 (radiosity trace): accumulation SSBO + radiosity UBO.
            {
                WGPUBindGroupLayoutEntry e[2] = {};
                e[0].binding = 0;
                e[0].visibility = WGPUShaderStage_Compute;
                e[0].buffer.type = WGPUBufferBindingType_Storage;
                e[0].buffer.minBindingSize = 4;
                e[1].binding = 2;
                e[1].visibility = WGPUShaderStage_Compute;
                e[1].buffer.type = WGPUBufferBindingType_Uniform;
                e[1].buffer.minBindingSize = sizeof(RadiosityUniformsCpu);

                WGPUBindGroupLayoutDescriptor d{};
                d.label = {"cornell_radiosity_trace_bgl", WGPU_STRLEN};
                d.entryCount = 2;
                d.entries = e;
                m_radiosityTraceBgl = wgpuDeviceCreateBindGroupLayout(ctx.device, &d);
                if (!m_radiosityTraceBgl)
                    return false;
            }

            // group 1 (accumulation->lightmap): accumulation SSBO + lightmap storage + radiosity UBO.
            {
                WGPUBindGroupLayoutEntry e[3] = {};
                e[0].binding = 0;
                e[0].visibility = WGPUShaderStage_Compute;
                e[0].buffer.type = WGPUBufferBindingType_Storage;
                e[0].buffer.minBindingSize = 4;
                e[1].binding = 1;
                e[1].visibility = WGPUShaderStage_Compute;
                e[1].storageTexture.access = WGPUStorageTextureAccess_ReadWrite;
                e[1].storageTexture.format = kLightmapFormat;
                e[1].storageTexture.viewDimension = WGPUTextureViewDimension_2DArray;
                e[2].binding = 2;
                e[2].visibility = WGPUShaderStage_Compute;
                e[2].buffer.type = WGPUBufferBindingType_Uniform;
                e[2].buffer.minBindingSize = sizeof(RadiosityUniformsCpu);

                WGPUBindGroupLayoutDescriptor d{};
                d.label = {"cornell_radiosity_bgl", WGPU_STRLEN};
                d.entryCount = 3;
                d.entries = e;
                m_radiosityBgl = wgpuDeviceCreateBindGroupLayout(ctx.device, &d);
                if (!m_radiosityBgl)
                    return false;
            }

            // group 1 (rasterizer): lightmap tex + sampler.
            {
                WGPUBindGroupLayoutEntry e[2] = {};
                e[0].binding = 0;
                e[0].visibility = WGPUShaderStage_Fragment;
                e[0].texture.sampleType = WGPUTextureSampleType_Float;
                e[0].texture.viewDimension = WGPUTextureViewDimension_2DArray;
                e[1].binding = 1;
                e[1].visibility = WGPUShaderStage_Fragment;
                e[1].sampler.type = WGPUSamplerBindingType_Filtering;

                WGPUBindGroupLayoutDescriptor d{};
                d.label = {"cornell_rast_bgl", WGPU_STRLEN};
                d.entryCount = 2;
                d.entries = e;
                m_rasterizerBgl = wgpuDeviceCreateBindGroupLayout(ctx.device, &d);
                if (!m_rasterizerBgl)
                    return false;
            }

            // group 1 (raytracer): lightmap tex + sampler + framebuffer RWTexture.
            {
                WGPUBindGroupLayoutEntry e[3] = {};
                e[0].binding = 0;
                e[0].visibility = WGPUShaderStage_Compute;
                e[0].texture.sampleType = WGPUTextureSampleType_Float;
                e[0].texture.viewDimension = WGPUTextureViewDimension_2DArray;
                e[1].binding = 1;
                e[1].visibility = WGPUShaderStage_Compute;
                e[1].sampler.type = WGPUSamplerBindingType_Filtering;
                e[2].binding = 2;
                e[2].visibility = WGPUShaderStage_Compute;
                e[2].storageTexture.access = WGPUStorageTextureAccess_ReadWrite;
                e[2].storageTexture.format = kFramebufferFormat;
                e[2].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

                WGPUBindGroupLayoutDescriptor d{};
                d.label = {"cornell_rt_bgl", WGPU_STRLEN};
                d.entryCount = 3;
                d.entries = e;
                m_raytracerBgl = wgpuDeviceCreateBindGroupLayout(ctx.device, &d);
                if (!m_raytracerBgl)
                    return false;
            }

            // group 0 (tonemap): input tex — bgl only used by the fragment.
            // The PS uses Load (textureLoad), so sampleType=UnfilterableFloat would
            // normally suffice; we set Float for simplicity since we created the HDR
            // texture with RENDER_ATTACHMENT|STORAGE|TEXTURE_BINDING.
            {
                WGPUBindGroupLayoutEntry e[1] = {};
                e[0].binding = 0;
                e[0].visibility = WGPUShaderStage_Fragment;
                e[0].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
                e[0].texture.viewDimension = WGPUTextureViewDimension_2D;

                WGPUBindGroupLayoutDescriptor d{};
                d.label = {"cornell_tonemap_bgl", WGPU_STRLEN};
                d.entryCount = 1;
                d.entries = e;
                m_tonemapBgl = wgpuDeviceCreateBindGroupLayout(ctx.device, &d);
                if (!m_tonemapBgl)
                    return false;
            }

            // Pipeline layouts.
            auto makePl = [&](const char *label, WGPUBindGroupLayout *bgls, uint32_t n)
            {
                WGPUPipelineLayoutDescriptor d{};
                d.label = {label, WGPU_STRLEN};
                d.bindGroupLayoutCount = n;
                d.bindGroupLayouts = bgls;
                return wgpuDeviceCreatePipelineLayout(ctx.device, &d);
            };
            {
                WGPUBindGroupLayout bgls[2] = {m_commonBgl, m_radiosityTraceBgl};
                m_radiosityPipelineLayout = makePl("cornell_rad_pl", bgls, 2);
            }
            {
                WGPUBindGroupLayout bgls[2] = {m_commonBgl, m_radiosityBgl};
                m_accumToLightmapPipelineLayout = makePl("cornell_accum2lm_pl", bgls, 2);
            }
            {
                WGPUBindGroupLayout bgls[2] = {m_commonBgl, m_rasterizerBgl};
                m_rasterizerPipelineLayout = makePl("cornell_rast_pl", bgls, 2);
            }
            {
                WGPUBindGroupLayout bgls[2] = {m_commonBgl, m_raytracerBgl};
                m_raytracerPipelineLayout = makePl("cornell_rt_pl", bgls, 2);
            }
            {
                WGPUBindGroupLayout bgls[1] = {m_tonemapBgl};
                m_tonemapPipelineLayout = makePl("cornell_tonemap_pl", bgls, 1);
            }
            if (!m_radiosityPipelineLayout || !m_accumToLightmapPipelineLayout ||
                !m_rasterizerPipelineLayout || !m_raytracerPipelineLayout ||
                !m_tonemapPipelineLayout)
                return false;

            // Radiosity compute pipelines.
            {
                WGPUComputePipelineDescriptor d{};
                d.label = {"cornell_radiosity_cp", WGPU_STRLEN};
                d.layout = m_radiosityPipelineLayout;
                d.compute.module = m_radiosityCS;
                d.compute.entryPoint = {"CSMain", WGPU_STRLEN};
                m_radiosityPipeline = wgpuDeviceCreateComputePipeline(ctx.device, &d);
                if (!m_radiosityPipeline)
                    return false;
            }
            {
                WGPUComputePipelineDescriptor d{};
                d.label = {"cornell_accum2lm_cp", WGPU_STRLEN};
                d.layout = m_accumToLightmapPipelineLayout;
                d.compute.module = m_accumToLightmapCS;
                d.compute.entryPoint = {"CSMain", WGPU_STRLEN};
                m_accumToLightmapPipeline = wgpuDeviceCreateComputePipeline(ctx.device, &d);
                if (!m_accumToLightmapPipeline)
                    return false;
            }

            // Rasterizer render pipeline.
            {
                WGPUVertexAttribute attrs[3] = {};
                attrs[0].format = WGPUVertexFormat_Float32x4;
                attrs[0].offset = offsetof(Vertex, pos);
                attrs[0].shaderLocation = 0;
                attrs[1].format = WGPUVertexFormat_Float32x3;
                attrs[1].offset = offsetof(Vertex, uv);
                attrs[1].shaderLocation = 1;
                attrs[2].format = WGPUVertexFormat_Float32x3;
                attrs[2].offset = offsetof(Vertex, emissive);
                attrs[2].shaderLocation = 2;

                WGPUVertexBufferLayout vbl{};
                vbl.arrayStride = sizeof(Vertex);
                vbl.stepMode = WGPUVertexStepMode_Vertex;
                vbl.attributeCount = 3;
                vbl.attributes = attrs;

                WGPUColorTargetState target{};
                target.format = kFramebufferFormat;
                target.writeMask = WGPUColorWriteMask_All;

                WGPUFragmentState fs{};
                fs.module = m_rasterizerPS;
                fs.entryPoint = {"PSMain", WGPU_STRLEN};
                fs.targetCount = 1;
                fs.targets = &target;

                WGPUDepthStencilState depth{};
                depth.format = WGPUTextureFormat_Depth24Plus;
                depth.depthWriteEnabled = WGPUOptionalBool_True;
                depth.depthCompare = WGPUCompareFunction_Less;
                depth.stencilFront.compare = WGPUCompareFunction_Always;
                depth.stencilBack.compare = WGPUCompareFunction_Always;

                WGPURenderPipelineDescriptor d{};
                d.label = {"cornell_rasterizer_pipe", WGPU_STRLEN};
                d.layout = m_rasterizerPipelineLayout;
                d.vertex.module = m_rasterizerVS;
                d.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
                d.vertex.bufferCount = 1;
                d.vertex.buffers = &vbl;
                d.primitive.topology = WGPUPrimitiveTopology_TriangleList;
                d.primitive.frontFace = WGPUFrontFace_CCW;
                d.primitive.cullMode = WGPUCullMode_Back;
                d.multisample.count = 1;
                d.multisample.mask = 0xFFFFFFFF;
                d.depthStencil = &depth;
                d.fragment = &fs;
                m_rasterizerPipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &d);
                if (!m_rasterizerPipeline)
                    return false;
            }

            // Raytracer compute pipeline.
            {
                WGPUComputePipelineDescriptor d{};
                d.label = {"cornell_raytracer_cp", WGPU_STRLEN};
                d.layout = m_raytracerPipelineLayout;
                d.compute.module = m_raytracerCS;
                d.compute.entryPoint = {"CSMain", WGPU_STRLEN};
                m_raytracerPipeline = wgpuDeviceCreateComputePipeline(ctx.device, &d);
                if (!m_raytracerPipeline)
                    return false;
            }

            // Tonemap fragment pipeline (full-screen triangle).
            {
                WGPUColorTargetState target{};
                target.format = ctx.surfaceFormat;
                target.writeMask = WGPUColorWriteMask_All;

                WGPUFragmentState fs{};
                fs.module = m_tonemapPS;
                fs.entryPoint = {"PSMain", WGPU_STRLEN};
                fs.targetCount = 1;
                fs.targets = &target;

                WGPURenderPipelineDescriptor d{};
                d.label = {"cornell_tonemap_pipe", WGPU_STRLEN};
                d.layout = m_tonemapPipelineLayout;
                d.vertex.module = m_tonemapVS;
                d.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
                d.primitive.topology = WGPUPrimitiveTopology_TriangleList;
                d.primitive.frontFace = WGPUFrontFace_CCW;
                d.primitive.cullMode = WGPUCullMode_None;
                d.multisample.count = 1;
                d.multisample.mask = 0xFFFFFFFF;
                d.fragment = &fs;
                m_tonemapPipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &d);
                if (!m_tonemapPipeline)
                    return false;
            }

            return true;
        }

        bool CreateStaticBindGroups(pwgpu::test::SampleContext &ctx)
        {
            // Common bind group (group 0 for compute + rasterizer paths).
            {
                WGPUBindGroupEntry e[2] = {};
                e[0].binding = 0;
                e[0].buffer = m_commonUniformBuffer;
                e[0].size = sizeof(CommonUniformsCpu);
                e[1].binding = 1;
                e[1].buffer = m_quadBuffer;
                e[1].size = m_scene.quadsGpu.size() * sizeof(QuadGpu);

                WGPUBindGroupDescriptor d{};
                d.label = {"cornell_common_bg", WGPU_STRLEN};
                d.layout = m_commonBgl;
                d.entryCount = 2;
                d.entries = e;
                m_commonBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &d);
                if (!m_commonBindGroup)
                    return false;
            }

            // Radiosity trace bind group.
            {
                const uint64_t accumBytes = static_cast<uint64_t>(kLightmapWidth) *
                                            kLightmapHeight *
                                            static_cast<uint64_t>(m_scene.quads.size()) * 16ull;
                WGPUBindGroupEntry e[2] = {};
                e[0].binding = 0;
                e[0].buffer = m_accumulationBuffer;
                e[0].size = accumBytes;
                e[1].binding = 2;
                e[1].buffer = m_radiosityUniformBuffer;
                e[1].size = sizeof(RadiosityUniformsCpu);

                WGPUBindGroupDescriptor d{};
                d.label = {"cornell_radiosity_trace_bg", WGPU_STRLEN};
                d.layout = m_radiosityTraceBgl;
                d.entryCount = 2;
                d.entries = e;
                m_radiosityTraceBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &d);
                if (!m_radiosityTraceBindGroup)
                    return false;
            }

            // Accumulation->lightmap bind group.
            {
                const uint64_t accumBytes = static_cast<uint64_t>(kLightmapWidth) *
                                            kLightmapHeight *
                                            static_cast<uint64_t>(m_scene.quads.size()) * 16ull;
                WGPUBindGroupEntry e[3] = {};
                e[0].binding = 0;
                e[0].buffer = m_accumulationBuffer;
                e[0].size = accumBytes;
                e[1].binding = 1;
                e[1].textureView = m_lightmapStorageView;
                e[2].binding = 2;
                e[2].buffer = m_radiosityUniformBuffer;
                e[2].size = sizeof(RadiosityUniformsCpu);

                WGPUBindGroupDescriptor d{};
                d.label = {"cornell_radiosity_bg", WGPU_STRLEN};
                d.layout = m_radiosityBgl;
                d.entryCount = 3;
                d.entries = e;
                m_radiosityBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &d);
                if (!m_radiosityBindGroup)
                    return false;
            }

            // Rasterizer bind group (lightmap + sampler).
            {
                WGPUBindGroupEntry e[2] = {};
                e[0].binding = 0;
                e[0].textureView = m_lightmapView;
                e[1].binding = 1;
                e[1].sampler = m_rasterizerSampler;

                WGPUBindGroupDescriptor d{};
                d.label = {"cornell_rast_bg", WGPU_STRLEN};
                d.layout = m_rasterizerBgl;
                d.entryCount = 2;
                d.entries = e;
                m_rasterizerBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &d);
                if (!m_rasterizerBindGroup)
                    return false;
            }

            return true;
        }

        void RebuildRaytracerBindGroup(WGPUDevice device)
        {
            if (m_raytracerBindGroup)
            {
                wgpuBindGroupRelease(m_raytracerBindGroup);
                m_raytracerBindGroup = nullptr;
            }
            if (!m_raytracerBgl || !m_lightmapView || !m_framebufferView)
                return;

            WGPUBindGroupEntry e[3] = {};
            e[0].binding = 0;
            e[0].textureView = m_lightmapView;
            e[1].binding = 1;
            e[1].sampler = m_raytracerSampler;
            e[2].binding = 2;
            e[2].textureView = m_framebufferView;

            WGPUBindGroupDescriptor d{};
            d.label = {"cornell_rt_bg", WGPU_STRLEN};
            d.layout = m_raytracerBgl;
            d.entryCount = 3;
            d.entries = e;
            m_raytracerBindGroup = wgpuDeviceCreateBindGroup(device, &d);
        }

        void RebuildTonemapBindGroup(WGPUDevice device)
        {
            if (m_tonemapBindGroup)
            {
                wgpuBindGroupRelease(m_tonemapBindGroup);
                m_tonemapBindGroup = nullptr;
            }
            if (!m_tonemapBgl || !m_framebufferView)
                return;

            WGPUBindGroupEntry e[1] = {};
            e[0].binding = 0;
            e[0].textureView = m_framebufferView;

            WGPUBindGroupDescriptor d{};
            d.label = {"cornell_tonemap_bg", WGPU_STRLEN};
            d.layout = m_tonemapBgl;
            d.entryCount = 1;
            d.entries = e;
            m_tonemapBindGroup = wgpuDeviceCreateBindGroup(device, &d);
        }

        void ReleaseSizedTargets()
        {
            if (m_raytracerBindGroup)
            {
                wgpuBindGroupRelease(m_raytracerBindGroup);
                m_raytracerBindGroup = nullptr;
            }
            if (m_tonemapBindGroup)
            {
                wgpuBindGroupRelease(m_tonemapBindGroup);
                m_tonemapBindGroup = nullptr;
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
            if (m_framebufferView)
            {
                wgpuTextureViewRelease(m_framebufferView);
                m_framebufferView = nullptr;
            }
            if (m_framebuffer)
            {
                wgpuTextureRelease(m_framebuffer);
                m_framebuffer = nullptr;
            }
        }

        // Scene data.
        SceneData m_scene{};

        // Frame counter for camera rotation (upstream uses frame/1000).
        uint64_t m_frame = 0;

        // RNG for common_uniforms.seed (upstream calls Math.random()).
        std::mt19937 m_rng{0xC0FFEEu};

        // Running mean of accumulation buffer values (radiosity.ts.accumulationMean).
        double m_accumulationMean = 0.0;

        // Cached framebuffer size (for raytracer dispatch).
        uint32_t m_fbWidth = 0;
        uint32_t m_fbHeight = 0;

        // Shaders.
        WGPUShaderModule m_radiosityCS = nullptr;
        WGPUShaderModule m_accumToLightmapCS = nullptr;
        WGPUShaderModule m_rasterizerVS = nullptr;
        WGPUShaderModule m_rasterizerPS = nullptr;
        WGPUShaderModule m_raytracerCS = nullptr;
        WGPUShaderModule m_tonemapVS = nullptr;
        WGPUShaderModule m_tonemapPS = nullptr;

        // Buffers.
        WGPUBuffer m_vertexBuffer = nullptr;
        WGPUBuffer m_indexBuffer = nullptr;
        WGPUBuffer m_quadBuffer = nullptr;
        WGPUBuffer m_commonUniformBuffer = nullptr;
        WGPUBuffer m_radiosityUniformBuffer = nullptr;
        WGPUBuffer m_accumulationBuffer = nullptr;

        // Lightmap texture + views.
        WGPUTexture m_lightmap = nullptr;
        WGPUTextureView m_lightmapView = nullptr;        // sampled Texture2DArray
        WGPUTextureView m_lightmapStorageView = nullptr; // RWTexture2DArray for compute write

        // HDR framebuffer (resize-tracked).
        WGPUTexture m_framebuffer = nullptr;
        WGPUTextureView m_framebufferView = nullptr;

        // Depth texture for the rasterizer pass.
        WGPUTexture m_depthTexture = nullptr;
        WGPUTextureView m_depthView = nullptr;

        // Samplers.
        WGPUSampler m_rasterizerSampler = nullptr;
        WGPUSampler m_raytracerSampler = nullptr;
        WGPUSampler m_tonemapSampler = nullptr;

        // Bind group layouts + pipeline layouts.
        WGPUBindGroupLayout m_commonBgl = nullptr;
        WGPUBindGroupLayout m_radiosityTraceBgl = nullptr;
        WGPUBindGroupLayout m_radiosityBgl = nullptr;
        WGPUBindGroupLayout m_rasterizerBgl = nullptr;
        WGPUBindGroupLayout m_raytracerBgl = nullptr;
        WGPUBindGroupLayout m_tonemapBgl = nullptr;

        WGPUPipelineLayout m_radiosityPipelineLayout = nullptr;
        WGPUPipelineLayout m_accumToLightmapPipelineLayout = nullptr;
        WGPUPipelineLayout m_rasterizerPipelineLayout = nullptr;
        WGPUPipelineLayout m_raytracerPipelineLayout = nullptr;
        WGPUPipelineLayout m_tonemapPipelineLayout = nullptr;

        // Bind groups.
        WGPUBindGroup m_commonBindGroup = nullptr;
        WGPUBindGroup m_radiosityTraceBindGroup = nullptr;
        WGPUBindGroup m_radiosityBindGroup = nullptr;
        WGPUBindGroup m_rasterizerBindGroup = nullptr;
        WGPUBindGroup m_raytracerBindGroup = nullptr;
        WGPUBindGroup m_tonemapBindGroup = nullptr;

        // Pipelines.
        WGPUComputePipeline m_radiosityPipeline = nullptr;
        WGPUComputePipeline m_accumToLightmapPipeline = nullptr;
        WGPURenderPipeline m_rasterizerPipeline = nullptr;
        WGPUComputePipeline m_raytracerPipeline = nullptr;
        WGPURenderPipeline m_tonemapPipeline = nullptr;
    };

    pwgpu::test::SampleAppDesc MakeCornellDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "CornellTest";
        desc.windowTitle = "Phasma WebGPU Cornell";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        // Lightmap + HDR framebuffer are rgba16float + storage-binding:
        // need TextureFormatsTier2 so the engine lets us create a
        // rgba16float storage texture (same precedent as ImageBlur).
        desc.requiredFeatures = {WGPUFeatureName_TextureFormatsTier2};
        desc.surfaceUsage = WGPUTextureUsage_RenderAttachment;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    pe::Log::Init();
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    CornellSample sample;
    pwgpu::test::SampleApp app(sample, MakeCornellDesc());
    return app.Run(argc, argv);
}
