#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb/stb_image.h"

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
    constexpr float kPi = 3.14159265f;

    // 1 planet + 1000 asteroids. Upstream uses 5000; we cap at 1000 to keep the
    // per-asteroid uniform buffer/bind-group count inside our VkBuffer budget.
    constexpr uint32_t kAsteroidCount = 1000;

    constexpr uint64_t kSceneBufferSize = 64; // one mat4
    constexpr uint64_t kModelBufferSize = 64; // one mat4

    // Sphere vertex layout matches webgpu-samples/meshes/sphere.ts:
    //   pos (3f) + normal (3f) + uv (2f) = 32B stride.
    constexpr uint32_t kSphereVertexFloatCount = 8;
    constexpr uint32_t kSphereVertexStride = kSphereVertexFloatCount * sizeof(float);

    struct SphereMesh
    {
        std::vector<float> vertices;
        std::vector<uint16_t> indices;
    };

    // Uniform pseudo-random (deterministic so runs look the same).
    float FrandUnit()
    {
        return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    }

    // Port of webgpu-samples/meshes/sphere.ts createSphereMesh().
    SphereMesh CreateSphereMesh(float radius, int widthSegments, int heightSegments, float randomness)
    {
        SphereMesh out;
        if (widthSegments < 3)
            widthSegments = 3;
        if (heightSegments < 2)
            heightSegments = 2;

        vec3 firstVertex(0.f);
        vec3 vertex(0.f);
        vec3 normal(0.f);

        int index = 0;
        std::vector<std::vector<int>> grid;
        grid.reserve(static_cast<size_t>(heightSegments + 1));

        for (int iy = 0; iy <= heightSegments; ++iy)
        {
            std::vector<int> row;
            row.reserve(static_cast<size_t>(widthSegments + 1));
            const float v = static_cast<float>(iy) / static_cast<float>(heightSegments);

            float uOffset = 0.f;
            if (iy == 0)
                uOffset = 0.5f / static_cast<float>(widthSegments);
            else if (iy == heightSegments)
                uOffset = -0.5f / static_cast<float>(widthSegments);

            for (int ix = 0; ix <= widthSegments; ++ix)
            {
                const float u = static_cast<float>(ix) / static_cast<float>(widthSegments);

                // Poles: share the same position all the way around.
                if (ix == widthSegments)
                {
                    vertex = firstVertex;
                }
                else if (ix == 0 || (iy != 0 && iy != heightSegments))
                {
                    const float rr = radius + (FrandUnit() - 0.5f) * 2.f * randomness * radius;
                    vertex.x = -rr * std::cos(u * 2.f * kPi) * std::sin(v * kPi);
                    vertex.y = rr * std::cos(v * kPi);
                    vertex.z = rr * std::sin(u * 2.f * kPi) * std::sin(v * kPi);

                    if (ix == 0)
                        firstVertex = vertex;
                }

                out.vertices.push_back(vertex.x);
                out.vertices.push_back(vertex.y);
                out.vertices.push_back(vertex.z);

                // normal = normalize(vertex)
                normal = vertex;
                const float len = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
                if (len > 0.f)
                {
                    normal.x /= len;
                    normal.y /= len;
                    normal.z /= len;
                }
                out.vertices.push_back(normal.x);
                out.vertices.push_back(normal.y);
                out.vertices.push_back(normal.z);

                // uv
                out.vertices.push_back(u + uOffset);
                out.vertices.push_back(1.f - v);

                row.push_back(index++);
            }
            grid.push_back(std::move(row));
        }

        for (int iy = 0; iy < heightSegments; ++iy)
        {
            for (int ix = 0; ix < widthSegments; ++ix)
            {
                const int a = grid[iy][ix + 1];
                const int b = grid[iy][ix];
                const int c = grid[iy + 1][ix];
                const int d = grid[iy + 1][ix + 1];

                if (iy != 0)
                {
                    out.indices.push_back(static_cast<uint16_t>(a));
                    out.indices.push_back(static_cast<uint16_t>(b));
                    out.indices.push_back(static_cast<uint16_t>(d));
                }
                if (iy != heightSegments - 1)
                {
                    out.indices.push_back(static_cast<uint16_t>(b));
                    out.indices.push_back(static_cast<uint16_t>(c));
                    out.indices.push_back(static_cast<uint16_t>(d));
                }
            }
        }

        return out;
    }

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
            fprintf(stderr, "[Image] Failed to load %s: %s\n", path.c_str(), stbi_failure_reason());
            return false;
        }
        return true;
    }

    // A sphere mesh lives on the GPU as a VB+IB pair. Multiple asteroids share
    // the same mesh (one of five variants) but each renderable owns its own
    // model uniform + bind group.
    struct SphereGPUMesh
    {
        WGPUBuffer vertexBuffer = nullptr;
        uint64_t vertexBufferSize = 0;
        WGPUBuffer indexBuffer = nullptr;
        uint64_t indexBufferSize = 0;
        uint32_t indexCount = 0;
    };

    struct Renderable
    {
        const SphereGPUMesh *mesh = nullptr;
        WGPUBuffer modelBuffer = nullptr;  // 64B mat4
        WGPUBindGroup bindGroup = nullptr; // set=1 {b0 model, b1 sampler, b2 tex}
    };

    class RenderBundlesSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            std::srand(0xA57E401D);

            m_vertexShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "mesh.vert.hlsl").c_str(), "mesh_vs");
            m_pixelShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "mesh.pixel.hlsl").c_str(), "mesh_ps");
            if (!m_vertexShader || !m_pixelShader)
                return false;

            if (!LoadTextures(ctx))
                return false;

            if (!CreateSampler(ctx))
                return false;

            if (!CreateSceneResources(ctx))
                return false;

            if (!CreateMeshes(ctx))
                return false;

            if (!CreatePipeline(ctx))
                return false;

            if (!BuildRenderables(ctx))
                return false;

            Resize(ctx, ctx.width, ctx.height);
            if (!m_depthView)
                return false;

            // Encode the bundle once. Because the scene content never changes
            // shape (same draws, same buffers, same bind groups), we only need
            // to re-record the bundle when the asteroid count changes — which
            // never happens in this port.
            if (!UpdateRenderBundle(ctx))
                return false;

            fprintf(stdout,
                    "[RenderBundles] planet + %u asteroids, bundle draws=%zu\n",
                    kAsteroidCount,
                    m_renderables.size());
            return true;
        }

        void Resize(pwgpu::test::SampleContext &ctx, uint32_t width, uint32_t height) override
        {
            ReleaseDepthTarget();
            if (width == 0 || height == 0)
                return;

            WGPUTextureDescriptor depthDesc{};
            depthDesc.label = {"rb_depth", WGPU_STRLEN};
            depthDesc.dimension = WGPUTextureDimension_2D;
            depthDesc.size = {width, height, 1};
            depthDesc.mipLevelCount = 1;
            depthDesc.sampleCount = 1;
            depthDesc.format = WGPUTextureFormat_Depth24Plus;
            depthDesc.usage = WGPUTextureUsage_RenderAttachment;
            m_depthTexture = wgpuDeviceCreateTexture(ctx.device, &depthDesc);
            if (!m_depthTexture)
                return;

            WGPUTextureViewDescriptor viewDesc{};
            viewDesc.format = WGPUTextureFormat_Depth24Plus;
            viewDesc.dimension = WGPUTextureViewDimension_2D;
            viewDesc.mipLevelCount = 1;
            viewDesc.arrayLayerCount = 1;
            viewDesc.aspect = WGPUTextureAspect_All;
            m_depthView = wgpuTextureCreateView(m_depthTexture, &viewDesc);
        }

        void Update(pwgpu::test::SampleContext &ctx) override
        {
            const float aspect =
                ctx.height > 0 ? static_cast<float>(ctx.width) / static_cast<float>(ctx.height)
                               : 1.0f;
            mat4 projection = glm::perspectiveRH_ZO((2.f * kPi) / 5.f, aspect, 1.f, 100.f);
            // Vulkan's clip-space Y is flipped vs. WebGPU's; compensate on CPU.
            projection[1][1] *= -1.f;

            mat4 view(1.f);
            view = glm::translate(view, vec3(0.f, 0.f, -4.f));

            const float now = static_cast<float>(ctx.timing.elapsedSeconds);
            view = glm::rotate(view, kPi * 0.1f, vec3(0.f, 0.f, 1.f));
            view = glm::rotate(view, kPi * 0.1f, vec3(1.f, 0.f, 0.f));
            view = glm::rotate(view, now * 0.05f, vec3(0.f, 1.f, 0.f));

            mat4 viewProjection = projection * view;
            wgpuQueueWriteBuffer(ctx.queue,
                                 m_sceneBuffer,
                                 0,
                                 glm::value_ptr(viewProjection),
                                 kSceneBufferSize);
        }

        bool Execute(pwgpu::test::SampleContext &, pwgpu::test::SampleFrame &frame) override
        {
            if (!m_pipeline || !m_depthView || !m_renderBundle)
                return true;

            WGPURenderPassColorAttachment colorAttachment{};
            colorAttachment.view = frame.surfaceView;
            colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            colorAttachment.loadOp = WGPULoadOp_Clear;
            colorAttachment.storeOp = WGPUStoreOp_Store;
            colorAttachment.clearValue = {0.0, 0.0, 0.0, 1.0};

            WGPURenderPassDepthStencilAttachment depthAttachment{};
            depthAttachment.view = m_depthView;
            depthAttachment.depthClearValue = 1.0f;
            depthAttachment.depthLoadOp = WGPULoadOp_Clear;
            depthAttachment.depthStoreOp = WGPUStoreOp_Store;
            depthAttachment.depthReadOnly = WGPUOptionalBool_False;
            depthAttachment.stencilLoadOp = WGPULoadOp_Undefined;
            depthAttachment.stencilStoreOp = WGPUStoreOp_Undefined;
            depthAttachment.stencilReadOnly = WGPUOptionalBool_True;

            WGPURenderPassDescriptor passDesc{};
            passDesc.label = {"rb_pass", WGPU_STRLEN};
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments = &colorAttachment;
            passDesc.depthStencilAttachment = &depthAttachment;

            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(frame.encoder, &passDesc);
            wgpuRenderPassEncoderExecuteBundles(pass, 1, &m_renderBundle);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            ReleaseDepthTarget();

            if (m_renderBundle)
            {
                wgpuRenderBundleRelease(m_renderBundle);
                m_renderBundle = nullptr;
            }

            for (Renderable &r : m_renderables)
            {
                if (r.bindGroup)
                    wgpuBindGroupRelease(r.bindGroup);
                if (r.modelBuffer)
                    wgpuBufferRelease(r.modelBuffer);
            }
            m_renderables.clear();

            auto releaseMesh = [](SphereGPUMesh &m)
            {
                if (m.vertexBuffer)
                    wgpuBufferRelease(m.vertexBuffer);
                if (m.indexBuffer)
                    wgpuBufferRelease(m.indexBuffer);
                m = SphereGPUMesh{};
            };
            releaseMesh(m_planetMesh);
            for (SphereGPUMesh &m : m_asteroidMeshes)
                releaseMesh(m);
            m_asteroidMeshes.clear();

            if (m_pipeline)
                wgpuRenderPipelineRelease(m_pipeline);
            if (m_pipelineLayout)
                wgpuPipelineLayoutRelease(m_pipelineLayout);
            if (m_sceneBindGroup)
                wgpuBindGroupRelease(m_sceneBindGroup);
            if (m_modelBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_modelBindGroupLayout);
            if (m_sceneBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_sceneBindGroupLayout);
            if (m_sceneBuffer)
                wgpuBufferRelease(m_sceneBuffer);
            if (m_sampler)
                wgpuSamplerRelease(m_sampler);
            if (m_moonView)
                wgpuTextureViewRelease(m_moonView);
            if (m_moonTexture)
                wgpuTextureRelease(m_moonTexture);
            if (m_planetView)
                wgpuTextureViewRelease(m_planetView);
            if (m_planetTexture)
                wgpuTextureRelease(m_planetTexture);
            if (m_pixelShader)
                wgpuShaderModuleRelease(m_pixelShader);
            if (m_vertexShader)
                wgpuShaderModuleRelease(m_vertexShader);
        }

    private:
        bool LoadTextures(pwgpu::test::SampleContext &ctx)
        {
            LoadedImage saturn{};
            LoadedImage moon{};
            if (!LoadImageRGBA8(pwgpu::test::GetAssetPath(ctx.exeDir, "saturn.jpg"), saturn))
            {
                fprintf(stderr, "        Place saturn.jpg next to the executable.\n");
                return false;
            }
            if (!LoadImageRGBA8(pwgpu::test::GetAssetPath(ctx.exeDir, "moon.jpg"), moon))
            {
                fprintf(stderr, "        Place moon.jpg next to the executable.\n");
                stbi_image_free(saturn.pixels);
                return false;
            }

            auto upload = [&](const LoadedImage &img,
                              const char *label,
                              WGPUTexture &outTex,
                              WGPUTextureView &outView) -> bool
            {
                WGPUTextureDescriptor desc{};
                desc.label = {label, WGPU_STRLEN};
                desc.dimension = WGPUTextureDimension_2D;
                desc.size = {static_cast<uint32_t>(img.width), static_cast<uint32_t>(img.height), 1};
                desc.mipLevelCount = 1;
                desc.sampleCount = 1;
                desc.format = WGPUTextureFormat_RGBA8Unorm;
                desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
                outTex = wgpuDeviceCreateTexture(ctx.device, &desc);
                if (!outTex)
                    return false;

                pwgpu::test::UploadRGBA8Texture(ctx.queue,
                                                outTex,
                                                img.pixels,
                                                static_cast<uint32_t>(img.width),
                                                static_cast<uint32_t>(img.height));
                outView = pwgpu::test::CreateTextureView(outTex, WGPUTextureFormat_RGBA8Unorm);
                return outView != nullptr;
            };

            bool ok = upload(saturn, "planet_tex", m_planetTexture, m_planetView) &&
                      upload(moon, "moon_tex", m_moonTexture, m_moonView);

            stbi_image_free(saturn.pixels);
            stbi_image_free(moon.pixels);
            return ok;
        }

        bool CreateSampler(pwgpu::test::SampleContext &ctx)
        {
            WGPUSamplerDescriptor desc{};
            desc.label = {"mesh_sampler", WGPU_STRLEN};
            desc.addressModeU = WGPUAddressMode_ClampToEdge;
            desc.addressModeV = WGPUAddressMode_ClampToEdge;
            desc.addressModeW = WGPUAddressMode_ClampToEdge;
            desc.magFilter = WGPUFilterMode_Linear;
            desc.minFilter = WGPUFilterMode_Linear;
            desc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
            desc.lodMinClamp = 0.0f;
            desc.lodMaxClamp = 32.0f;
            desc.compare = WGPUCompareFunction_Undefined;
            desc.maxAnisotropy = 1;
            m_sampler = wgpuDeviceCreateSampler(ctx.device, &desc);
            return m_sampler != nullptr;
        }

        bool CreateSceneResources(pwgpu::test::SampleContext &ctx)
        {
            m_sceneBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                      kSceneBufferSize,
                                                      WGPUBufferUsage_Uniform |
                                                          WGPUBufferUsage_CopyDst,
                                                      "rb_scene_ub");
            if (!m_sceneBuffer)
                return false;

            WGPUBindGroupLayoutEntry sceneEntries[1] = {};
            sceneEntries[0].binding = 0;
            sceneEntries[0].visibility = WGPUShaderStage_Vertex;
            sceneEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
            sceneEntries[0].buffer.minBindingSize = kSceneBufferSize;

            WGPUBindGroupLayoutDescriptor sceneBglDesc{};
            sceneBglDesc.label = {"rb_g0_bgl", WGPU_STRLEN};
            sceneBglDesc.entryCount = 1;
            sceneBglDesc.entries = sceneEntries;
            m_sceneBindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device, &sceneBglDesc);
            if (!m_sceneBindGroupLayout)
                return false;

            WGPUBindGroupLayoutEntry modelEntries[3] = {};
            modelEntries[0].binding = 0;
            modelEntries[0].visibility = WGPUShaderStage_Vertex;
            modelEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
            modelEntries[0].buffer.minBindingSize = kModelBufferSize;
            modelEntries[1].binding = 1;
            modelEntries[1].visibility = WGPUShaderStage_Fragment;
            modelEntries[1].sampler.type = WGPUSamplerBindingType_Filtering;
            modelEntries[2].binding = 2;
            modelEntries[2].visibility = WGPUShaderStage_Fragment;
            modelEntries[2].texture.sampleType = WGPUTextureSampleType_Float;
            modelEntries[2].texture.viewDimension = WGPUTextureViewDimension_2D;
            modelEntries[2].texture.multisampled = WGPUOptionalBool_False;

            WGPUBindGroupLayoutDescriptor modelBglDesc{};
            modelBglDesc.label = {"rb_g1_bgl", WGPU_STRLEN};
            modelBglDesc.entryCount = 3;
            modelBglDesc.entries = modelEntries;
            m_modelBindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device, &modelBglDesc);
            if (!m_modelBindGroupLayout)
                return false;

            WGPUBindGroupEntry sceneBgEntries[1] = {};
            sceneBgEntries[0].binding = 0;
            sceneBgEntries[0].buffer = m_sceneBuffer;
            sceneBgEntries[0].size = kSceneBufferSize;

            WGPUBindGroupDescriptor sceneBgDesc{};
            sceneBgDesc.label = {"rb_g0_bg", WGPU_STRLEN};
            sceneBgDesc.layout = m_sceneBindGroupLayout;
            sceneBgDesc.entryCount = 1;
            sceneBgDesc.entries = sceneBgEntries;
            m_sceneBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &sceneBgDesc);
            return m_sceneBindGroup != nullptr;
        }

        bool UploadMesh(pwgpu::test::SampleContext &ctx,
                        const SphereMesh &cpu,
                        const char *label,
                        SphereGPUMesh &out)
        {
            const uint64_t vbBytes = static_cast<uint64_t>(cpu.vertices.size()) * sizeof(float);
            out.vertexBufferSize = vbBytes;
            out.vertexBuffer = pwgpu::test::CreateBuffer(
                ctx.device,
                vbBytes,
                WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
                label);
            if (!out.vertexBuffer)
                return false;
            wgpuQueueWriteBuffer(ctx.queue, out.vertexBuffer, 0, cpu.vertices.data(), vbBytes);

            const uint64_t rawIbBytes = static_cast<uint64_t>(cpu.indices.size()) * sizeof(uint16_t);
            const uint64_t ibBytes = (rawIbBytes + 3u) & ~uint64_t(3);
            out.indexBufferSize = ibBytes;
            out.indexCount = static_cast<uint32_t>(cpu.indices.size());
            out.indexBuffer = pwgpu::test::CreateBuffer(
                ctx.device,
                ibBytes,
                WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst,
                label);
            if (!out.indexBuffer)
                return false;

            std::vector<uint8_t> padded(static_cast<size_t>(ibBytes), 0);
            std::memcpy(padded.data(), cpu.indices.data(), static_cast<size_t>(rawIbBytes));
            wgpuQueueWriteBuffer(ctx.queue, out.indexBuffer, 0, padded.data(), ibBytes);
            return true;
        }

        bool CreateMeshes(pwgpu::test::SampleContext &ctx)
        {
            SphereMesh planet = CreateSphereMesh(1.0f, 32, 16, 0.f);
            if (!UploadMesh(ctx, planet, "planet_mesh", m_planetMesh))
                return false;

            struct AsteroidParams
            {
                float radius;
                int w;
                int h;
            };
            const AsteroidParams params[5] = {
                {0.010f, 8, 6},
                {0.013f, 8, 6},
                {0.017f, 8, 6},
                {0.020f, 8, 6},
                {0.030f, 16, 8},
            };

            m_asteroidMeshes.resize(5);
            char labelBuf[32];
            for (int i = 0; i < 5; ++i)
            {
                SphereMesh cpu = CreateSphereMesh(params[i].radius, params[i].w, params[i].h, 0.15f);
                std::snprintf(labelBuf, sizeof(labelBuf), "asteroid_mesh_%d", i);
                if (!UploadMesh(ctx, cpu, labelBuf, m_asteroidMeshes[i]))
                    return false;
            }
            return true;
        }

        bool CreatePipeline(pwgpu::test::SampleContext &ctx)
        {
            WGPUBindGroupLayout layouts[2] = {m_sceneBindGroupLayout, m_modelBindGroupLayout};
            WGPUPipelineLayoutDescriptor plDesc{};
            plDesc.label = {"rb_pl", WGPU_STRLEN};
            plDesc.bindGroupLayoutCount = 2;
            plDesc.bindGroupLayouts = layouts;
            m_pipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device, &plDesc);
            if (!m_pipelineLayout)
                return false;

            WGPUVertexAttribute attributes[3] = {};
            attributes[0].format = WGPUVertexFormat_Float32x3;
            attributes[0].offset = 0;
            attributes[0].shaderLocation = 0;
            attributes[1].format = WGPUVertexFormat_Float32x3;
            attributes[1].offset = 3 * sizeof(float);
            attributes[1].shaderLocation = 1;
            attributes[2].format = WGPUVertexFormat_Float32x2;
            attributes[2].offset = 6 * sizeof(float);
            attributes[2].shaderLocation = 2;

            WGPUVertexBufferLayout vbLayout{};
            vbLayout.arrayStride = kSphereVertexStride;
            vbLayout.stepMode = WGPUVertexStepMode_Vertex;
            vbLayout.attributeCount = 3;
            vbLayout.attributes = attributes;

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
            depthState.stencilFront.failOp = WGPUStencilOperation_Keep;
            depthState.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
            depthState.stencilFront.passOp = WGPUStencilOperation_Keep;
            depthState.stencilBack = depthState.stencilFront;

            WGPURenderPipelineDescriptor pipeDesc{};
            pipeDesc.label = {"rb_pipe", WGPU_STRLEN};
            pipeDesc.layout = m_pipelineLayout;
            pipeDesc.vertex.module = m_vertexShader;
            pipeDesc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            pipeDesc.vertex.bufferCount = 1;
            pipeDesc.vertex.buffers = &vbLayout;
            pipeDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            pipeDesc.primitive.cullMode = WGPUCullMode_Back;
            pipeDesc.primitive.frontFace = WGPUFrontFace_CCW;
            pipeDesc.multisample.count = 1;
            pipeDesc.multisample.mask = 0xFFFFFFFF;
            pipeDesc.depthStencil = &depthState;
            pipeDesc.fragment = &fragmentState;
            m_pipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &pipeDesc);
            return m_pipeline != nullptr;
        }

        bool CreateRenderable(pwgpu::test::SampleContext &ctx,
                              const SphereGPUMesh &mesh,
                              WGPUTextureView textureView,
                              const mat4 &transform,
                              const char *label,
                              Renderable &out)
        {
            out.mesh = &mesh;

            out.modelBuffer = pwgpu::test::CreateBuffer(
                ctx.device,
                kModelBufferSize,
                WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
                label);
            if (!out.modelBuffer)
                return false;
            wgpuQueueWriteBuffer(ctx.queue,
                                 out.modelBuffer,
                                 0,
                                 glm::value_ptr(transform),
                                 kModelBufferSize);

            WGPUBindGroupEntry entries[3] = {};
            entries[0].binding = 0;
            entries[0].buffer = out.modelBuffer;
            entries[0].size = kModelBufferSize;
            entries[1].binding = 1;
            entries[1].sampler = m_sampler;
            entries[2].binding = 2;
            entries[2].textureView = textureView;

            WGPUBindGroupDescriptor bgDesc{};
            bgDesc.label = {label, WGPU_STRLEN};
            bgDesc.layout = m_modelBindGroupLayout;
            bgDesc.entryCount = 3;
            bgDesc.entries = entries;
            out.bindGroup = wgpuDeviceCreateBindGroup(ctx.device, &bgDesc);
            return out.bindGroup != nullptr;
        }

        bool BuildRenderables(pwgpu::test::SampleContext &ctx)
        {
            m_renderables.clear();
            m_renderables.reserve(kAsteroidCount + 1);

            m_renderables.emplace_back();
            if (!CreateRenderable(ctx,
                                  m_planetMesh,
                                  m_planetView,
                                  mat4(1.f),
                                  "rb_planet",
                                  m_renderables.back()))
                return false;

            for (uint32_t i = 0; i < kAsteroidCount; ++i)
            {
                const float radius = FrandUnit() * 1.7f + 1.25f;
                const float angle = FrandUnit() * 2.f * kPi;
                const float x = std::sin(angle) * radius;
                const float y = (FrandUnit() - 0.5f) * 0.015f;
                const float z = std::cos(angle) * radius;

                mat4 m(1.f);
                m = glm::translate(m, vec3(x, y, z));
                m = glm::rotate(m, FrandUnit() * kPi, vec3(1.f, 0.f, 0.f));
                m = glm::rotate(m, FrandUnit() * kPi, vec3(0.f, 1.f, 0.f));

                const SphereGPUMesh &mesh = m_asteroidMeshes[i % m_asteroidMeshes.size()];
                m_renderables.emplace_back();
                if (!CreateRenderable(ctx, mesh, m_moonView, m, "rb_asteroid", m_renderables.back()))
                    return false;
            }
            return true;
        }

        bool UpdateRenderBundle(pwgpu::test::SampleContext &ctx)
        {
            if (m_renderBundle)
            {
                wgpuRenderBundleRelease(m_renderBundle);
                m_renderBundle = nullptr;
            }

            WGPUTextureFormat colorFormat = ctx.surfaceFormat;

            WGPURenderBundleEncoderDescriptor rbeDesc{};
            rbeDesc.label = {"rb_encoder", WGPU_STRLEN};
            rbeDesc.colorFormatCount = 1;
            rbeDesc.colorFormats = &colorFormat;
            rbeDesc.depthStencilFormat = WGPUTextureFormat_Depth24Plus;
            rbeDesc.sampleCount = 1;
            // The bundle must match the render pass's read-only aspects. Our
            // Depth24Plus attachment has no stencil aspect, so the pass treats
            // stencil as read-only. The descriptor's depth/stencil fields are
            // WGPUBool, so use WGPU_FALSE / WGPU_TRUE (not WGPUOptionalBool).
            rbeDesc.depthReadOnly = WGPU_FALSE;
            rbeDesc.stencilReadOnly = WGPU_TRUE;

            WGPURenderBundleEncoder rbe =
                wgpuDeviceCreateRenderBundleEncoder(ctx.device, &rbeDesc);
            if (!rbe)
                return false;

            wgpuRenderBundleEncoderSetPipeline(rbe, m_pipeline);
            wgpuRenderBundleEncoderSetBindGroup(rbe, 0, m_sceneBindGroup, 0, nullptr);

            for (const Renderable &r : m_renderables)
            {
                wgpuRenderBundleEncoderSetBindGroup(rbe, 1, r.bindGroup, 0, nullptr);
                wgpuRenderBundleEncoderSetVertexBuffer(rbe,
                                                       0,
                                                       r.mesh->vertexBuffer,
                                                       0,
                                                       r.mesh->vertexBufferSize);
                wgpuRenderBundleEncoderSetIndexBuffer(rbe,
                                                      r.mesh->indexBuffer,
                                                      WGPUIndexFormat_Uint16,
                                                      0,
                                                      r.mesh->indexBufferSize);
                wgpuRenderBundleEncoderDrawIndexed(rbe, r.mesh->indexCount, 1, 0, 0, 0);
            }

            WGPURenderBundleDescriptor bundleDesc{};
            bundleDesc.label = {"rb_bundle", WGPU_STRLEN};
            m_renderBundle = wgpuRenderBundleEncoderFinish(rbe, &bundleDesc);
            wgpuRenderBundleEncoderRelease(rbe);
            return m_renderBundle != nullptr;
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

        WGPUShaderModule m_vertexShader = nullptr;
        WGPUShaderModule m_pixelShader = nullptr;

        WGPUTexture m_planetTexture = nullptr;
        WGPUTextureView m_planetView = nullptr;
        WGPUTexture m_moonTexture = nullptr;
        WGPUTextureView m_moonView = nullptr;
        WGPUSampler m_sampler = nullptr;

        WGPUBuffer m_sceneBuffer = nullptr;
        WGPUBindGroupLayout m_sceneBindGroupLayout = nullptr;
        WGPUBindGroupLayout m_modelBindGroupLayout = nullptr;
        WGPUBindGroup m_sceneBindGroup = nullptr;

        WGPUPipelineLayout m_pipelineLayout = nullptr;
        WGPURenderPipeline m_pipeline = nullptr;

        SphereGPUMesh m_planetMesh{};
        std::vector<SphereGPUMesh> m_asteroidMeshes;
        std::vector<Renderable> m_renderables;

        WGPURenderBundle m_renderBundle = nullptr;

        WGPUTexture m_depthTexture = nullptr;
        WGPUTextureView m_depthView = nullptr;
    };

    pwgpu::test::SampleAppDesc MakeRenderBundlesDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "RenderBundlesTest";
        desc.windowTitle = "PhasmaWebGPU Render Bundles";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    RenderBundlesSample sample;
    pwgpu::test::SampleApp app(sample, MakeRenderBundlesDesc());
    return app.Run(argc, argv);
}
