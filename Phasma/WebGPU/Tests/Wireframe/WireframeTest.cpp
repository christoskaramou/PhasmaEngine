// Faithful port of webgpu-samples/wireframe.
//
// Scene: 200 animated objects randomly chosen from the upstream model set
// (teapot, sphere, jewel, rock). The sample defaults to the original
// line-list wireframe technique and can be toggled to the barycentric
// fwidth()/smoothstep() technique with the B key.

#include "../ABuffer/TeapotMesh.h"
#include "../Common/SampleApp.h"
#include "../Common/SampleBase.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"

namespace
{
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    constexpr float kPi = 3.14159265358979323846f;

    constexpr uint32_t kNumObjects = 200;
    constexpr uint32_t kNumModels = 4;
    constexpr uint32_t kVertexStrideFloats = 6; // position.xyz + normal.xyz

    // Uniform layout: worldViewProjection[64] + world[64] + color[16] = 144.
    constexpr uint32_t kUniformSize = 144;
    constexpr uint64_t kUniformSlotSize = 256;
    constexpr uint64_t kObjectUniformBufferSize = kNumObjects * kUniformSlotSize;
    // LineUniforms: stride(u32) + thickness(f32) + alphaThreshold(f32) + pad.
    constexpr uint32_t kLineUniformSize = 16;
    static_assert(kUniformSize <= kUniformSlotSize);

    constexpr int32_t kDepthBias = 1;
    constexpr float kDepthBiasSlopeScale = 0.5f;

    struct MeshData
    {
        std::vector<float> vertices; // interleaved pos3 + nrm3
        std::vector<uint32_t> indices;
    };

    struct Model
    {
        WGPUBuffer vertexBuffer = nullptr;
        WGPUBuffer indexBuffer = nullptr;
        uint32_t vertexCount = 0; // number of indices
        uint32_t vertexBufferSize = 0;
        uint32_t indexBufferSize = 0;
    };

    // ---------------------------------------------------------------------
    // Random helpers (deterministic via srand(42) in Init).
    // ---------------------------------------------------------------------
    inline float Rand01()
    {
        return static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
    }

    inline void Normalize(float v[3])
    {
        const float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (len > 0.0f)
        {
            v[0] /= len;
            v[1] /= len;
            v[2] /= len;
        }
    }

    // ---------------------------------------------------------------------
    // Teapot mesh -> interleaved pos3+nrm3, scaled by 1.5x (upstream).
    // Normals are computed via per-triangle face normals averaged at each
    // shared vertex (matches upstream's computeSurfaceNormals).
    // ---------------------------------------------------------------------
    MeshData BuildTeapot()
    {
        using namespace pwgpu::test::teapot;
        constexpr float kScale = 1.5f;
        MeshData m;
        m.vertices.assign(kVertexCount * kVertexStrideFloats, 0.0f);

        // Compute face normals and accumulate at each vertex.
        std::vector<float> accum(kVertexCount * 3u, 0.0f);
        for (uint32_t t = 0; t < kTriangleCount; ++t)
        {
            uint32_t i0 = kIndices[t * 3u + 0u];
            uint32_t i1 = kIndices[t * 3u + 1u];
            uint32_t i2 = kIndices[t * 3u + 2u];
            const float *p0 = &kPositions[i0 * 3u];
            const float *p1 = &kPositions[i1 * 3u];
            const float *p2 = &kPositions[i2 * 3u];
            float e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
            float e2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
            Normalize(e1);
            Normalize(e2);
            float n[3] = {e1[1] * e2[2] - e1[2] * e2[1],
                          e1[2] * e2[0] - e1[0] * e2[2],
                          e1[0] * e2[1] - e1[1] * e2[0]};
            for (uint32_t k = 0; k < 3; ++k)
            {
                uint32_t idx = (k == 0 ? i0 : (k == 1 ? i1 : i2)) * 3u;
                accum[idx + 0] += n[0];
                accum[idx + 1] += n[1];
                accum[idx + 2] += n[2];
            }
        }

        for (uint32_t v = 0; v < kVertexCount; ++v)
        {
            float nx = accum[v * 3u + 0u];
            float ny = accum[v * 3u + 1u];
            float nz = accum[v * 3u + 2u];
            float len = sqrtf(nx * nx + ny * ny + nz * nz);
            if (len > 0.0f)
            {
                nx /= len;
                ny /= len;
                nz /= len;
            }
            float *dst = &m.vertices[v * kVertexStrideFloats];
            dst[0] = kPositions[v * 3u + 0u] * kScale;
            dst[1] = kPositions[v * 3u + 1u] * kScale;
            dst[2] = kPositions[v * 3u + 2u] * kScale;
            dst[3] = nx;
            dst[4] = ny;
            dst[5] = nz;
        }

        m.indices.assign(kIndexCount, 0u);
        for (uint32_t i = 0; i < kIndexCount; ++i)
            m.indices[i] = static_cast<uint32_t>(kIndices[i]);
        return m;
    }

    // ---------------------------------------------------------------------
    // Sphere generator ported from webgpu-samples/meshes/sphere.ts.
    // Mirrors three.js SphereGeometry; includes poles handling and a
    // per-vertex radius jitter controlled by `randomness`.
    // ---------------------------------------------------------------------
    MeshData BuildSphere(float radius, int widthSegs, int heightSegs, float randomness)
    {
        if (widthSegs < 3)
            widthSegs = 3;
        if (heightSegs < 2)
            heightSegs = 2;

        struct V
        {
            float x, y, z;
        };
        std::vector<V> pos;
        std::vector<std::vector<uint32_t>> grid;
        grid.resize(heightSegs + 1);

        V firstVertex{0.f, 0.f, 0.f};
        V vertex{0.f, 0.f, 0.f};
        uint32_t idx = 0;

        for (int iy = 0; iy <= heightSegs; ++iy)
        {
            auto &row = grid[iy];
            float v = static_cast<float>(iy) / static_cast<float>(heightSegs);

            for (int ix = 0; ix <= widthSegs; ++ix)
            {
                float u = static_cast<float>(ix) / static_cast<float>(widthSegs);

                if (ix == widthSegs)
                {
                    vertex = firstVertex; // wrap: seam shares the row-start vertex
                }
                else if (ix == 0 || (iy != 0 && iy != heightSegs))
                {
                    float rr = radius + (Rand01() - 0.5f) * 2.0f * randomness * radius;
                    vertex.x = -rr * cosf(u * kPi * 2.0f) * sinf(v * kPi);
                    vertex.y = rr * cosf(v * kPi);
                    vertex.z = rr * sinf(u * kPi * 2.0f) * sinf(v * kPi);
                    if (ix == 0)
                        firstVertex = vertex;
                }
                // else: poles reuse last computed `vertex` (same position all the way around)

                pos.push_back(vertex);
                row.push_back(idx++);
            }
        }

        // Build indices as upstream does.
        std::vector<uint32_t> indices;
        for (int iy = 0; iy < heightSegs; ++iy)
        {
            for (int ix = 0; ix < widthSegs; ++ix)
            {
                uint32_t a = grid[iy][ix + 1];
                uint32_t b = grid[iy][ix];
                uint32_t c = grid[iy + 1][ix];
                uint32_t d = grid[iy + 1][ix + 1];
                if (iy != 0)
                {
                    indices.push_back(a);
                    indices.push_back(b);
                    indices.push_back(d);
                }
                if (iy != heightSegs - 1)
                {
                    indices.push_back(b);
                    indices.push_back(c);
                    indices.push_back(d);
                }
            }
        }

        MeshData m;
        m.vertices.assign(pos.size() * kVertexStrideFloats, 0.0f);
        for (size_t i = 0; i < pos.size(); ++i)
        {
            float normal[3] = {pos[i].x, pos[i].y, pos[i].z};
            Normalize(normal);
            float *dst = &m.vertices[i * kVertexStrideFloats];
            dst[0] = pos[i].x;
            dst[1] = pos[i].y;
            dst[2] = pos[i].z;
            dst[3] = normal[0];
            dst[4] = normal[1];
            dst[5] = normal[2];
        }
        m.indices = std::move(indices);
        return m;
    }

    MeshData FlattenNormals(const MeshData &src)
    {
        MeshData out;
        out.vertices.assign(src.indices.size() * kVertexStrideFloats, 0.0f);
        out.indices.resize(src.indices.size());

        for (size_t i = 0; i < src.indices.size(); i += 3)
        {
            const uint32_t triIndices[3] = {src.indices[i + 0], src.indices[i + 1],
                                            src.indices[i + 2]};
            const float *p[3] = {&src.vertices[triIndices[0] * kVertexStrideFloats],
                                 &src.vertices[triIndices[1] * kVertexStrideFloats],
                                 &src.vertices[triIndices[2] * kVertexStrideFloats]};

            float e0[3] = {p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2]};
            float e1[3] = {p[2][0] - p[1][0], p[2][1] - p[1][1], p[2][2] - p[1][2]};
            Normalize(e0);
            Normalize(e1);
            float normal[3] = {e0[1] * e1[2] - e0[2] * e1[1],
                               e0[2] * e1[0] - e0[0] * e1[2],
                               e0[0] * e1[1] - e0[1] * e1[0]};
            Normalize(normal);

            for (uint32_t j = 0; j < 3; ++j)
            {
                const size_t dstVertex = i + j;
                float *dst = &out.vertices[dstVertex * kVertexStrideFloats];
                dst[0] = p[j][0];
                dst[1] = p[j][1];
                dst[2] = p[j][2];
                dst[3] = normal[0];
                dst[4] = normal[1];
                dst[5] = normal[2];
                out.indices[dstVertex] = static_cast<uint32_t>(dstVertex);
            }
        }

        return out;
    }

    struct ObjectInfo
    {
        WGPUBindGroup litBindGroup = nullptr;
        WGPUBindGroup wireLineBindGroup = nullptr;
        WGPUBindGroup wireBaryBindGroup = nullptr;
        uint32_t uniformSlot = 0;
        uint32_t modelIndex = 0;
        float color[4]{1, 1, 1, 1};
    };

    class WireframeSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            // Seed PRNG for repeatable scene before any mesh/color generation.
            srand(42);

            m_litVS = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "solidColorLit.vert.hlsl").c_str(), "lit_vs");
            m_litPS = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "solidColorLit.pixel.hlsl").c_str(), "lit_ps");
            m_wireLineVS = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "wireframeLine.vert.hlsl").c_str(), "wireline_vs");
            m_wireLinePS = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "wireframeLine.pixel.hlsl").c_str(), "wireline_ps");
            m_wireBaryVS = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "wireframeBary.vert.hlsl").c_str(), "wirebary_vs");
            m_wireBaryPS = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "wireframeBary.pixel.hlsl").c_str(), "wirebary_ps");
            if (!m_litVS || !m_litPS || !m_wireLineVS || !m_wireLinePS || !m_wireBaryVS ||
                !m_wireBaryPS)
                return false;

            // Build the same model set as the upstream sample.
            MeshData meshes[kNumModels];
            meshes[0] = BuildTeapot();
            meshes[1] = BuildSphere(20.0f, 32, 16, 0.0f);
            meshes[2] = FlattenNormals(BuildSphere(20.0f, 5, 3, 0.0f));
            meshes[3] = FlattenNormals(BuildSphere(20.0f, 32, 16, 0.1f));

            for (uint32_t i = 0; i < kNumModels; ++i)
            {
                if (!UploadModel(ctx, meshes[i], m_models[i]))
                    return false;
            }

            if (!CreateLayouts(ctx))
                return false;
            if (!CreateSharedLineUniform(ctx))
                return false;
            if (!CreateObjectUniformBuffer(ctx))
                return false;
            if (!CreateObjectResources(ctx))
                return false;
            if (!CreatePipelines(ctx))
                return false;
            if (!CreateRenderBundles(ctx))
                return false;

            return true;
        }

        void HandleEvent(pwgpu::test::SampleContext &ctx,
                         const SDL_Event &event,
                         bool &running) override
        {
            pwgpu::test::SampleBase::HandleEvent(ctx, event, running);

            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_b)
                m_useBarycentric = !m_useBarycentric;
        }

        void Resize(pwgpu::test::SampleContext &ctx, uint32_t width, uint32_t height) override
        {
            ReleaseDepthTarget();
            if (width == 0 || height == 0)
                return;

            WGPUTextureDescriptor depthDesc{};
            depthDesc.label = {"wire_depth", WGPU_STRLEN};
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
            float aspect = ctx.height > 0
                               ? static_cast<float>(ctx.width) / static_cast<float>(ctx.height)
                               : 1.0f;
            mat4 projection = glm::perspectiveRH_ZO(60.f * kPi / 180.f, aspect, 0.1f, 1000.f);
            projection[1][1] *= -1.f; // Vulkan Y-down NDC

            vec3 eye(-300.f, 0.f, 300.f);
            mat4 view = glm::lookAtRH(eye, vec3(0.f, 0.f, 0.f), vec3(0.f, 1.f, 0.f));
            mat4 viewProjection = projection * view;

            float t = static_cast<float>(ctx.timing.elapsedSeconds);

            for (uint32_t i = 0; i < kNumObjects; ++i)
            {
                float fi = static_cast<float>(i);
                mat4 world(1.f);
                world = glm::translate(world, vec3(0.f, 0.f, sinf(fi * 3.721f + t * 0.1f) * 200.f));
                world = glm::rotate(world, fi * 4.567f, vec3(1.f, 0.f, 0.f));
                world = glm::rotate(world, fi * 2.967f, vec3(0.f, 1.f, 0.f));
                world = glm::translate(world, vec3(0.f, 0.f, sinf(fi * 9.721f + t * 0.1f) * 200.f));
                world = glm::rotate(world, t * 0.53f + fi, vec3(1.f, 0.f, 0.f));

                mat4 worldViewProjection = viewProjection * world;

                float uniformData[36]{};
                memcpy(uniformData + 0, glm::value_ptr(worldViewProjection), 64);
                memcpy(uniformData + 16, glm::value_ptr(world), 64);
                memcpy(uniformData + 32, m_objects[i].color, 16);
                memcpy(m_objectUniformStaging.data() + m_objects[i].uniformSlot * kUniformSlotSize,
                       uniformData,
                       kUniformSize);
            }

            if (m_objectUniformBuffer && !m_objectUniformStaging.empty())
            {
                wgpuQueueWriteBuffer(ctx.queue,
                                     m_objectUniformBuffer,
                                     0,
                                     m_objectUniformStaging.data(),
                                     m_objectUniformStaging.size());
            }
        }

        bool Execute(pwgpu::test::SampleContext &ctx, pwgpu::test::SampleFrame &frame) override
        {
            WGPURenderBundle bundle = m_useBarycentric ? m_baryBundle : m_lineBundle;
            if (!m_depthView || !bundle)
                return true;

            WGPURenderPassColorAttachment colorAttachment{};
            colorAttachment.view = frame.surfaceView;
            colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            colorAttachment.loadOp = WGPULoadOp_Clear;
            colorAttachment.storeOp = WGPUStoreOp_Store;
            colorAttachment.clearValue = {0.3, 0.3, 0.3, 1.0};

            WGPURenderPassDepthStencilAttachment depthAttachment{};
            depthAttachment.view = m_depthView;
            depthAttachment.depthClearValue = 1.0f;
            depthAttachment.depthLoadOp = WGPULoadOp_Clear;
            depthAttachment.depthStoreOp = WGPUStoreOp_Store;
            depthAttachment.depthReadOnly = WGPUOptionalBool_False;
            depthAttachment.stencilLoadOp = WGPULoadOp_Undefined;
            depthAttachment.stencilStoreOp = WGPUStoreOp_Undefined;
            depthAttachment.stencilReadOnly = WGPUOptionalBool_True;

            WGPURenderPassDescriptor renderPassDesc{};
            renderPassDesc.label = {"wire_pass", WGPU_STRLEN};
            renderPassDesc.colorAttachmentCount = 1;
            renderPassDesc.colorAttachments = &colorAttachment;
            renderPassDesc.depthStencilAttachment = &depthAttachment;

            WGPURenderPassEncoder pass =
                wgpuCommandEncoderBeginRenderPass(frame.encoder, &renderPassDesc);

            const float fullH = static_cast<float>(ctx.height);
            const float fullW = static_cast<float>(ctx.width);

            wgpuRenderPassEncoderSetViewport(pass, 0.0f, 0.0f, fullW, fullH, 0.0f, 1.0f);
            wgpuRenderPassEncoderExecuteBundles(pass, 1, &bundle);

            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            ReleaseDepthTarget();

            if (m_baryBundle)
                wgpuRenderBundleRelease(m_baryBundle);
            if (m_lineBundle)
                wgpuRenderBundleRelease(m_lineBundle);

            if (m_wireBaryPipeline)
                wgpuRenderPipelineRelease(m_wireBaryPipeline);
            if (m_wireLinePipeline)
                wgpuRenderPipelineRelease(m_wireLinePipeline);
            if (m_litPipeline)
                wgpuRenderPipelineRelease(m_litPipeline);

            if (m_wirePipelineLayout)
                wgpuPipelineLayoutRelease(m_wirePipelineLayout);
            if (m_litPipelineLayout)
                wgpuPipelineLayoutRelease(m_litPipelineLayout);

            for (uint32_t i = 0; i < kNumObjects; ++i)
            {
                if (m_objects[i].wireBaryBindGroup)
                    wgpuBindGroupRelease(m_objects[i].wireBaryBindGroup);
                if (m_objects[i].wireLineBindGroup)
                    wgpuBindGroupRelease(m_objects[i].wireLineBindGroup);
                if (m_objects[i].litBindGroup)
                    wgpuBindGroupRelease(m_objects[i].litBindGroup);
                m_objects[i] = {};
            }

            if (m_objectUniformBuffer)
                wgpuBufferRelease(m_objectUniformBuffer);
            m_objectUniformBuffer = nullptr;
            m_objectUniformStaging.clear();

            if (m_lineUniformBuffer)
                wgpuBufferRelease(m_lineUniformBuffer);
            m_lineUniformBuffer = nullptr;

            if (m_wireBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_wireBindGroupLayout);
            if (m_litBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_litBindGroupLayout);

            for (uint32_t i = 0; i < kNumModels; ++i)
            {
                if (m_models[i].indexBuffer)
                    wgpuBufferRelease(m_models[i].indexBuffer);
                if (m_models[i].vertexBuffer)
                    wgpuBufferRelease(m_models[i].vertexBuffer);
                m_models[i] = {};
            }

            if (m_wireBaryPS)
                wgpuShaderModuleRelease(m_wireBaryPS);
            if (m_wireBaryVS)
                wgpuShaderModuleRelease(m_wireBaryVS);
            if (m_wireLinePS)
                wgpuShaderModuleRelease(m_wireLinePS);
            if (m_wireLineVS)
                wgpuShaderModuleRelease(m_wireLineVS);
            if (m_litPS)
                wgpuShaderModuleRelease(m_litPS);
            if (m_litVS)
                wgpuShaderModuleRelease(m_litVS);

            m_wireBaryPipeline = nullptr;
            m_wireLinePipeline = nullptr;
            m_litPipeline = nullptr;
            m_baryBundle = nullptr;
            m_lineBundle = nullptr;
            m_wirePipelineLayout = nullptr;
            m_litPipelineLayout = nullptr;
            m_wireBindGroupLayout = nullptr;
            m_litBindGroupLayout = nullptr;
            m_wireBaryPS = nullptr;
            m_wireBaryVS = nullptr;
            m_wireLinePS = nullptr;
            m_wireLineVS = nullptr;
            m_litPS = nullptr;
            m_litVS = nullptr;
        }

    private:
        void RecordSolid(WGPURenderBundleEncoder rbe)
        {
            wgpuRenderBundleEncoderSetPipeline(rbe, m_litPipeline);
            uint32_t lastModel = UINT32_MAX;
            for (uint32_t i = 0; i < kNumObjects; ++i)
            {
                uint32_t mi = m_objects[i].modelIndex;
                if (mi != lastModel)
                {
                    const Model &m = m_models[mi];
                    wgpuRenderBundleEncoderSetVertexBuffer(
                        rbe, 0, m.vertexBuffer, 0, m.vertexBufferSize);
                    wgpuRenderBundleEncoderSetIndexBuffer(
                        rbe, m.indexBuffer, WGPUIndexFormat_Uint32, 0, m.indexBufferSize);
                    lastModel = mi;
                }
                wgpuRenderBundleEncoderSetBindGroup(
                    rbe, 0, m_objects[i].litBindGroup, 0, nullptr);
                wgpuRenderBundleEncoderDrawIndexed(rbe, m_models[mi].vertexCount, 1, 0, 0, 0);
            }
        }

        void RecordWireframe(WGPURenderBundleEncoder rbe, bool barycentric)
        {
            wgpuRenderBundleEncoderSetPipeline(
                rbe, barycentric ? m_wireBaryPipeline : m_wireLinePipeline);
            for (uint32_t i = 0; i < kNumObjects; ++i)
            {
                WGPUBindGroup bg = barycentric ? m_objects[i].wireBaryBindGroup
                                               : m_objects[i].wireLineBindGroup;
                wgpuRenderBundleEncoderSetBindGroup(rbe, 0, bg, 0, nullptr);
                // Line-list: 2 verts per index (6 per triangle). Bary: 1:1 with indices.
                uint32_t count =
                    m_models[m_objects[i].modelIndex].vertexCount * (barycentric ? 1u : 2u);
                wgpuRenderBundleEncoderDraw(rbe, count, 1, 0, 0);
            }
        }

        bool UploadModel(pwgpu::test::SampleContext &ctx, const MeshData &mesh, Model &out)
        {
            out.vertexCount = static_cast<uint32_t>(mesh.indices.size());
            out.vertexBufferSize =
                static_cast<uint32_t>(mesh.vertices.size() * sizeof(float));
            out.indexBufferSize =
                static_cast<uint32_t>(mesh.indices.size() * sizeof(uint32_t));

            out.vertexBuffer = pwgpu::test::CreateBuffer(
                ctx.device, out.vertexBufferSize,
                static_cast<WGPUBufferUsage>(WGPUBufferUsage_Vertex | WGPUBufferUsage_Storage |
                                             WGPUBufferUsage_CopyDst),
                "wire_vb");
            out.indexBuffer = pwgpu::test::CreateBuffer(
                ctx.device, out.indexBufferSize,
                static_cast<WGPUBufferUsage>(WGPUBufferUsage_Index | WGPUBufferUsage_Storage |
                                             WGPUBufferUsage_CopyDst),
                "wire_ib");
            if (!out.vertexBuffer || !out.indexBuffer)
                return false;

            wgpuQueueWriteBuffer(
                ctx.queue, out.vertexBuffer, 0, mesh.vertices.data(), out.vertexBufferSize);
            wgpuQueueWriteBuffer(
                ctx.queue, out.indexBuffer, 0, mesh.indices.data(), out.indexBufferSize);
            return true;
        }

        bool CreateLayouts(pwgpu::test::SampleContext &ctx)
        {
            WGPUBindGroupLayoutEntry litEntry{};
            litEntry.binding = 0;
            litEntry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
            litEntry.buffer.type = WGPUBufferBindingType_Uniform;
            litEntry.buffer.minBindingSize = kUniformSize;

            WGPUBindGroupLayoutDescriptor litBglDesc{};
            litBglDesc.label = {"lit_bgl", WGPU_STRLEN};
            litBglDesc.entryCount = 1;
            litBglDesc.entries = &litEntry;
            m_litBindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device, &litBglDesc);
            if (!m_litBindGroupLayout)
                return false;

            WGPUPipelineLayoutDescriptor litPlDesc{};
            litPlDesc.label = {"lit_pl", WGPU_STRLEN};
            litPlDesc.bindGroupLayoutCount = 1;
            litPlDesc.bindGroupLayouts = &m_litBindGroupLayout;
            m_litPipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device, &litPlDesc);
            if (!m_litPipelineLayout)
                return false;

            // Wireframe: uniform(0), positions storage(1), indices storage(2), lineUniform(3).
            WGPUBindGroupLayoutEntry wireEntries[4] = {};
            wireEntries[0].binding = 0;
            wireEntries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
            wireEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
            wireEntries[0].buffer.minBindingSize = kUniformSize;

            wireEntries[1].binding = 1;
            wireEntries[1].visibility = WGPUShaderStage_Vertex;
            wireEntries[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
            wireEntries[1].buffer.minBindingSize = sizeof(float) * kVertexStrideFloats;

            wireEntries[2].binding = 2;
            wireEntries[2].visibility = WGPUShaderStage_Vertex;
            wireEntries[2].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
            wireEntries[2].buffer.minBindingSize = sizeof(uint32_t) * 3;

            wireEntries[3].binding = 3;
            wireEntries[3].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
            wireEntries[3].buffer.type = WGPUBufferBindingType_Uniform;
            wireEntries[3].buffer.minBindingSize = kLineUniformSize;

            WGPUBindGroupLayoutDescriptor wireBglDesc{};
            wireBglDesc.label = {"wire_bgl", WGPU_STRLEN};
            wireBglDesc.entryCount = 4;
            wireBglDesc.entries = wireEntries;
            m_wireBindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device, &wireBglDesc);
            if (!m_wireBindGroupLayout)
                return false;

            WGPUPipelineLayoutDescriptor wirePlDesc{};
            wirePlDesc.label = {"wire_pl", WGPU_STRLEN};
            wirePlDesc.bindGroupLayoutCount = 1;
            wirePlDesc.bindGroupLayouts = &m_wireBindGroupLayout;
            m_wirePipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device, &wirePlDesc);
            return m_wirePipelineLayout != nullptr;
        }

        bool CreateSharedLineUniform(pwgpu::test::SampleContext &ctx)
        {
            m_lineUniformBuffer = pwgpu::test::CreateBuffer(
                ctx.device, kLineUniformSize,
                static_cast<WGPUBufferUsage>(WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst),
                "wire_lubo");
            if (!m_lineUniformBuffer)
                return false;

            uint32_t lineData[4]{};
            lineData[0] = kVertexStrideFloats; // stride (in floats)
            float thickness = 2.0f;
            float alphaThreshold = 0.5f;
            memcpy(&lineData[1], &thickness, 4);
            memcpy(&lineData[2], &alphaThreshold, 4);
            wgpuQueueWriteBuffer(
                ctx.queue, m_lineUniformBuffer, 0, lineData, kLineUniformSize);
            return true;
        }

        bool CreateObjectUniformBuffer(pwgpu::test::SampleContext &ctx)
        {
            m_objectUniformBuffer = pwgpu::test::CreateBuffer(
                ctx.device, kObjectUniformBufferSize,
                static_cast<WGPUBufferUsage>(WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst),
                "wire_object_ubos");
            if (!m_objectUniformBuffer)
                return false;

            m_objectUniformStaging.assign(static_cast<size_t>(kObjectUniformBufferSize), 0);
            return true;
        }

        bool CreateObjectResources(pwgpu::test::SampleContext &ctx)
        {
            for (uint32_t i = 0; i < kNumObjects; ++i)
            {
                ObjectInfo &obj = m_objects[i];
                obj.uniformSlot = i;
                obj.modelIndex = static_cast<uint32_t>(rand() % kNumModels);
                obj.color[0] = Rand01();
                obj.color[1] = Rand01();
                obj.color[2] = Rand01();
                obj.color[3] = 1.0f;

                WGPUBindGroupEntry litBinding{};
                litBinding.binding = 0;
                litBinding.buffer = m_objectUniformBuffer;
                litBinding.offset = obj.uniformSlot * kUniformSlotSize;
                litBinding.size = kUniformSize;

                WGPUBindGroupDescriptor litBgDesc{};
                litBgDesc.label = {"lit_bg", WGPU_STRLEN};
                litBgDesc.layout = m_litBindGroupLayout;
                litBgDesc.entryCount = 1;
                litBgDesc.entries = &litBinding;
                obj.litBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &litBgDesc);
                if (!obj.litBindGroup)
                    return false;

                const Model &model = m_models[obj.modelIndex];
                WGPUBindGroupEntry wireBindings[4] = {};
                wireBindings[0].binding = 0;
                wireBindings[0].buffer = m_objectUniformBuffer;
                wireBindings[0].offset = obj.uniformSlot * kUniformSlotSize;
                wireBindings[0].size = kUniformSize;
                wireBindings[1].binding = 1;
                wireBindings[1].buffer = model.vertexBuffer;
                wireBindings[1].size = model.vertexBufferSize;
                wireBindings[2].binding = 2;
                wireBindings[2].buffer = model.indexBuffer;
                wireBindings[2].size = model.indexBufferSize;
                wireBindings[3].binding = 3;
                wireBindings[3].buffer = m_lineUniformBuffer;
                wireBindings[3].size = kLineUniformSize;

                WGPUBindGroupDescriptor wireBgDesc{};
                wireBgDesc.label = {"wire_bg", WGPU_STRLEN};
                wireBgDesc.layout = m_wireBindGroupLayout;
                wireBgDesc.entryCount = 4;
                wireBgDesc.entries = wireBindings;

                // Two identical bind groups — upstream does this because the
                // pipeline `layout: 'auto'` yields two distinct BGLs. Here both
                // pipelines share m_wireBindGroupLayout, so we could reuse one,
                // but keeping two matches upstream and simplifies future tweaks.
                obj.wireLineBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &wireBgDesc);
                obj.wireBaryBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &wireBgDesc);
                if (!obj.wireLineBindGroup || !obj.wireBaryBindGroup)
                    return false;
            }
            return true;
        }

        bool CreatePipelines(pwgpu::test::SampleContext &ctx)
        {
            // ---- Lit pipeline ----
            WGPUVertexAttribute litAttribs[2] = {};
            litAttribs[0].format = WGPUVertexFormat_Float32x3;
            litAttribs[0].offset = 0;
            litAttribs[0].shaderLocation = 0;
            litAttribs[1].format = WGPUVertexFormat_Float32x3;
            litAttribs[1].offset = 12;
            litAttribs[1].shaderLocation = 1;

            WGPUVertexBufferLayout litVbl{};
            litVbl.arrayStride = kVertexStrideFloats * sizeof(float);
            litVbl.stepMode = WGPUVertexStepMode_Vertex;
            litVbl.attributeCount = 2;
            litVbl.attributes = litAttribs;

            WGPUColorTargetState litColorTarget{};
            litColorTarget.format = ctx.surfaceFormat;
            litColorTarget.writeMask = WGPUColorWriteMask_All;

            WGPUFragmentState litFragment{};
            litFragment.module = m_litPS;
            litFragment.entryPoint = {"PSMain", WGPU_STRLEN};
            litFragment.targetCount = 1;
            litFragment.targets = &litColorTarget;

            WGPUDepthStencilState litDepth{};
            litDepth.format = WGPUTextureFormat_Depth24Plus;
            litDepth.depthWriteEnabled = WGPUOptionalBool_True;
            litDepth.depthCompare = WGPUCompareFunction_Less;
            litDepth.depthBias = kDepthBias;
            litDepth.depthBiasSlopeScale = kDepthBiasSlopeScale;
            litDepth.stencilFront.compare = WGPUCompareFunction_Always;
            litDepth.stencilFront.failOp = WGPUStencilOperation_Keep;
            litDepth.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
            litDepth.stencilFront.passOp = WGPUStencilOperation_Keep;
            litDepth.stencilBack = litDepth.stencilFront;

            WGPURenderPipelineDescriptor litDesc{};
            litDesc.label = {"lit_pipe", WGPU_STRLEN};
            litDesc.layout = m_litPipelineLayout;
            litDesc.vertex.module = m_litVS;
            litDesc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            litDesc.vertex.bufferCount = 1;
            litDesc.vertex.buffers = &litVbl;
            litDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            litDesc.primitive.cullMode = WGPUCullMode_Back;
            litDesc.primitive.frontFace = WGPUFrontFace_CCW;
            litDesc.depthStencil = &litDepth;
            litDesc.multisample.count = 1;
            litDesc.multisample.mask = 0xFFFFFFFF;
            litDesc.fragment = &litFragment;
            m_litPipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &litDesc);
            if (!m_litPipeline)
                return false;

            // ---- Line-list wireframe pipeline ----
            WGPUColorTargetState wireLineColor{};
            wireLineColor.format = ctx.surfaceFormat;
            wireLineColor.writeMask = WGPUColorWriteMask_All;

            WGPUFragmentState wireLineFrag{};
            wireLineFrag.module = m_wireLinePS;
            wireLineFrag.entryPoint = {"PSMain", WGPU_STRLEN};
            wireLineFrag.targetCount = 1;
            wireLineFrag.targets = &wireLineColor;

            WGPUDepthStencilState wireDepth{};
            wireDepth.format = WGPUTextureFormat_Depth24Plus;
            wireDepth.depthWriteEnabled = WGPUOptionalBool_True;
            wireDepth.depthCompare = WGPUCompareFunction_LessEqual;
            wireDepth.stencilFront.compare = WGPUCompareFunction_Always;
            wireDepth.stencilFront.failOp = WGPUStencilOperation_Keep;
            wireDepth.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
            wireDepth.stencilFront.passOp = WGPUStencilOperation_Keep;
            wireDepth.stencilBack = wireDepth.stencilFront;

            WGPURenderPipelineDescriptor wireLineDesc{};
            wireLineDesc.label = {"wireline_pipe", WGPU_STRLEN};
            wireLineDesc.layout = m_wirePipelineLayout;
            wireLineDesc.vertex.module = m_wireLineVS;
            wireLineDesc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            wireLineDesc.vertex.bufferCount = 0;
            wireLineDesc.vertex.buffers = nullptr;
            wireLineDesc.primitive.topology = WGPUPrimitiveTopology_LineList;
            wireLineDesc.primitive.cullMode = WGPUCullMode_None;
            wireLineDesc.primitive.frontFace = WGPUFrontFace_CCW;
            wireLineDesc.depthStencil = &wireDepth;
            wireLineDesc.multisample.count = 1;
            wireLineDesc.multisample.mask = 0xFFFFFFFF;
            wireLineDesc.fragment = &wireLineFrag;
            m_wireLinePipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &wireLineDesc);
            if (!m_wireLinePipeline)
                return false;

            // ---- Barycentric wireframe pipeline (triangle-list + alpha blend) ----
            WGPUBlendState baryBlend{};
            baryBlend.color.operation = WGPUBlendOperation_Add;
            baryBlend.color.srcFactor = WGPUBlendFactor_One;
            baryBlend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
            baryBlend.alpha.operation = WGPUBlendOperation_Add;
            baryBlend.alpha.srcFactor = WGPUBlendFactor_One;
            baryBlend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;

            WGPUColorTargetState wireBaryColor{};
            wireBaryColor.format = ctx.surfaceFormat;
            wireBaryColor.writeMask = WGPUColorWriteMask_All;
            wireBaryColor.blend = &baryBlend;

            WGPUFragmentState wireBaryFrag{};
            wireBaryFrag.module = m_wireBaryPS;
            wireBaryFrag.entryPoint = {"PSMain", WGPU_STRLEN};
            wireBaryFrag.targetCount = 1;
            wireBaryFrag.targets = &wireBaryColor;

            WGPURenderPipelineDescriptor wireBaryDesc{};
            wireBaryDesc.label = {"wirebary_pipe", WGPU_STRLEN};
            wireBaryDesc.layout = m_wirePipelineLayout;
            wireBaryDesc.vertex.module = m_wireBaryVS;
            wireBaryDesc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            wireBaryDesc.vertex.bufferCount = 0;
            wireBaryDesc.vertex.buffers = nullptr;
            wireBaryDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            wireBaryDesc.primitive.cullMode = WGPUCullMode_None;
            wireBaryDesc.primitive.frontFace = WGPUFrontFace_CCW;
            wireBaryDesc.depthStencil = &wireDepth;
            wireBaryDesc.multisample.count = 1;
            wireBaryDesc.multisample.mask = 0xFFFFFFFF;
            wireBaryDesc.fragment = &wireBaryFrag;
            m_wireBaryPipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &wireBaryDesc);
            return m_wireBaryPipeline != nullptr;
        }

        bool CreateRenderBundles(pwgpu::test::SampleContext &ctx)
        {
            if (m_lineBundle)
            {
                wgpuRenderBundleRelease(m_lineBundle);
                m_lineBundle = nullptr;
            }
            if (m_baryBundle)
            {
                wgpuRenderBundleRelease(m_baryBundle);
                m_baryBundle = nullptr;
            }

            m_lineBundle = CreateRenderBundle(ctx, false, "wire_line_bundle");
            m_baryBundle = CreateRenderBundle(ctx, true, "wire_bary_bundle");
            return m_lineBundle != nullptr && m_baryBundle != nullptr;
        }

        WGPURenderBundle CreateRenderBundle(pwgpu::test::SampleContext &ctx,
                                            bool barycentric,
                                            const char *label)
        {
            WGPUTextureFormat colorFormat = ctx.surfaceFormat;

            WGPURenderBundleEncoderDescriptor rbeDesc{};
            rbeDesc.label = {label, WGPU_STRLEN};
            rbeDesc.colorFormatCount = 1;
            rbeDesc.colorFormats = &colorFormat;
            rbeDesc.depthStencilFormat = WGPUTextureFormat_Depth24Plus;
            rbeDesc.sampleCount = 1;
            rbeDesc.depthReadOnly = WGPUOptionalBool_False;
            rbeDesc.stencilReadOnly = WGPUOptionalBool_True;

            WGPURenderBundleEncoder rbe =
                wgpuDeviceCreateRenderBundleEncoder(ctx.device, &rbeDesc);
            if (!rbe)
                return nullptr;

            RecordSolid(rbe);
            RecordWireframe(rbe, barycentric);

            WGPURenderBundleDescriptor bundleDesc{};
            bundleDesc.label = {label, WGPU_STRLEN};
            WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(rbe, &bundleDesc);
            wgpuRenderBundleEncoderRelease(rbe);
            return bundle;
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

        WGPUShaderModule m_litVS = nullptr;
        WGPUShaderModule m_litPS = nullptr;
        WGPUShaderModule m_wireLineVS = nullptr;
        WGPUShaderModule m_wireLinePS = nullptr;
        WGPUShaderModule m_wireBaryVS = nullptr;
        WGPUShaderModule m_wireBaryPS = nullptr;

        Model m_models[kNumModels]{};
        WGPUBuffer m_objectUniformBuffer = nullptr;
        std::vector<uint8_t> m_objectUniformStaging;
        WGPUBuffer m_lineUniformBuffer = nullptr;

        WGPUBindGroupLayout m_litBindGroupLayout = nullptr;
        WGPUBindGroupLayout m_wireBindGroupLayout = nullptr;
        WGPUPipelineLayout m_litPipelineLayout = nullptr;
        WGPUPipelineLayout m_wirePipelineLayout = nullptr;

        WGPURenderPipeline m_litPipeline = nullptr;
        WGPURenderPipeline m_wireLinePipeline = nullptr;
        WGPURenderPipeline m_wireBaryPipeline = nullptr;
        WGPURenderBundle m_lineBundle = nullptr;
        WGPURenderBundle m_baryBundle = nullptr;

        ObjectInfo m_objects[kNumObjects]{};

        WGPUTexture m_depthTexture = nullptr;
        WGPUTextureView m_depthView = nullptr;
        bool m_useBarycentric = false;
    };

    pwgpu::test::SampleAppDesc MakeWireframeDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "WireframeTest";
        desc.windowTitle = "Phasma WebGPU Wireframe";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    WireframeSample sample;
    pwgpu::test::SampleApp app(sample, MakeWireframeDesc());
    return app.Run(argc, argv);
}
