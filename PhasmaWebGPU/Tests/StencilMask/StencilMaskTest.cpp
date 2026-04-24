#include "../Common/SampleApp.h"
#include "../Common/SampleBase.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"
#include "Primitives.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace
{
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    constexpr float kPi = 3.14159265358979323846f;
    constexpr uint32_t kNumObjectsPerScene = 100;
    constexpr uint32_t kNumMaskScenes = 6;
    constexpr uint32_t kNumObjectScenes = 7;

    // Per-object UBO: mat4 world (64) + vec4 color (16) = 80 bytes.
    constexpr uint64_t kObjectUboSize = 80;
    // Shared UBO: mat4 viewProjection (64) + vec3 lightDirection + pad = 80 bytes.
    constexpr uint64_t kSharedUboSize = 80;
    constexpr uint64_t kUniformSlotSize = 256;
    constexpr uint32_t kTotalSceneCount = kNumMaskScenes + kNumObjectScenes;
    constexpr uint32_t kTotalObjectCount =
        kNumMaskScenes + kNumObjectScenes * kNumObjectsPerScene;
    constexpr uint32_t kSharedUniformBaseSlot = kTotalObjectCount;
    constexpr uint32_t kUniformSlotsPerChunk = 256;
    constexpr uint32_t kTotalUniformSlotCount = kTotalObjectCount + kTotalSceneCount;
    constexpr uint32_t kUniformChunkCount =
        (kTotalUniformSlotCount + kUniformSlotsPerChunk - 1) / kUniformSlotsPerChunk;

    constexpr WGPUTextureFormat kDepthStencilFormat = WGPUTextureFormat_Depth24PlusStencil8;
    static_assert(kObjectUboSize <= kUniformSlotSize);
    static_assert(kSharedUboSize <= kUniformSlotSize);

    struct Geometry
    {
        WGPUBuffer vertexBuffer = nullptr;
        WGPUBuffer indexBuffer = nullptr;
        uint64_t vertexBufferSize = 0;
        uint64_t indexBufferSize = 0;
        uint32_t indexCount = 0;
    };

    struct ObjectInfo
    {
        WGPUBindGroup bindGroup = nullptr;
        const Geometry *geometry = nullptr;
        uint32_t uniformSlot = 0;
        float color[4] = {1, 1, 1, 1};
        float world[16] = {};
    };

    struct Scene
    {
        std::vector<ObjectInfo> objects;
        uint32_t uniformSlot = 0;
        float viewProjection[16] = {};
        float lightDirection[4] = {};
        WGPURenderBundle bundle = nullptr;
    };

    // ----- simple mat4 helpers (column-major, matching wgpu-matrix / glm). -----
    inline void MatIdentity(float m[16])
    {
        for (int i = 0; i < 16; ++i)
            m[i] = 0.0f;
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }

    inline void MatMul(const float a[16], const float b[16], float out[16])
    {
        float r[16];
        for (int c = 0; c < 4; ++c)
        {
            for (int row = 0; row < 4; ++row)
            {
                r[c * 4 + row] = a[0 * 4 + row] * b[c * 4 + 0] +
                                 a[1 * 4 + row] * b[c * 4 + 1] +
                                 a[2 * 4 + row] * b[c * 4 + 2] +
                                 a[3 * 4 + row] * b[c * 4 + 3];
            }
        }
        std::memcpy(out, r, sizeof(r));
    }

    inline void MatPerspectiveRH(float fovY, float aspect, float zNear, float zFar, float out[16])
    {
        // Matches glm::perspectiveRH_ZO.
        const float f = 1.0f / std::tan(fovY * 0.5f);
        for (int i = 0; i < 16; ++i)
            out[i] = 0.0f;
        out[0] = f / aspect;
        out[5] = f;
        out[10] = zFar / (zNear - zFar);
        out[11] = -1.0f;
        out[14] = (zFar * zNear) / (zNear - zFar);
    }

    inline void MatLookAtRH(const float eye[3],
                            const float target[3],
                            const float up[3],
                            float out[16])
    {
        float fx = target[0] - eye[0];
        float fy = target[1] - eye[1];
        float fz = target[2] - eye[2];
        float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
        fx /= fl;
        fy /= fl;
        fz /= fl;

        // s = normalize(cross(f, up))
        float sx = fy * up[2] - fz * up[1];
        float sy = fz * up[0] - fx * up[2];
        float sz = fx * up[1] - fy * up[0];
        float sl = std::sqrt(sx * sx + sy * sy + sz * sz);
        sx /= sl;
        sy /= sl;
        sz /= sl;

        // u = cross(s, f)
        const float ux = sy * fz - sz * fy;
        const float uy = sz * fx - sx * fz;
        const float uz = sx * fy - sy * fx;

        out[0] = sx;
        out[1] = ux;
        out[2] = -fx;
        out[3] = 0.0f;
        out[4] = sy;
        out[5] = uy;
        out[6] = -fy;
        out[7] = 0.0f;
        out[8] = sz;
        out[9] = uz;
        out[10] = -fz;
        out[11] = 0.0f;
        out[12] = -(sx * eye[0] + sy * eye[1] + sz * eye[2]);
        out[13] = -(ux * eye[0] + uy * eye[1] + uz * eye[2]);
        out[14] = (fx * eye[0] + fy * eye[1] + fz * eye[2]);
        out[15] = 1.0f;
    }

    inline void MatTranslate(float m[16], float x, float y, float z)
    {
        float t[16];
        MatIdentity(t);
        t[12] = x;
        t[13] = y;
        t[14] = z;
        float r[16];
        MatMul(m, t, r);
        std::memcpy(m, r, sizeof(r));
    }

    inline void MatRotateX(float m[16], float angle)
    {
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        float r[16];
        MatIdentity(r);
        r[5] = c;
        r[6] = s;
        r[9] = -s;
        r[10] = c;
        float out[16];
        MatMul(m, r, out);
        std::memcpy(m, out, sizeof(out));
    }

    inline void MatRotateY(float m[16], float angle)
    {
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        float r[16];
        MatIdentity(r);
        r[0] = c;
        r[2] = -s;
        r[8] = s;
        r[10] = c;
        float out[16];
        MatMul(m, r, out);
        std::memcpy(m, out, sizeof(out));
    }

    inline void MatRotateZ(float m[16], float angle)
    {
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        float r[16];
        MatIdentity(r);
        r[0] = c;
        r[1] = s;
        r[4] = -s;
        r[5] = c;
        float out[16];
        MatMul(m, r, out);
        std::memcpy(m, out, sizeof(out));
    }

    inline void MatScale(float m[16], float x, float y, float z)
    {
        float t[16];
        MatIdentity(t);
        t[0] = x;
        t[5] = y;
        t[10] = z;
        float r[16];
        MatMul(m, t, r);
        std::memcpy(m, r, sizeof(r));
    }

    // ----- HSL -> RGB, matching canvas fillStyle conversion (approximate). -----
    inline float HueToRgb(float p, float q, float t)
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
    }

    inline void HslToRgba(float h, float s, float l, float out[4])
    {
        h = h - std::floor(h);
        float r, g, b;
        if (s == 0.0f)
        {
            r = g = b = l;
        }
        else
        {
            const float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
            const float p = 2.0f * l - q;
            r = HueToRgb(p, q, h + 1.0f / 3.0f);
            g = HueToRgb(p, q, h);
            b = HueToRgb(p, q, h - 1.0f / 3.0f);
        }
        out[0] = r;
        out[1] = g;
        out[2] = b;
        out[3] = 1.0f;
    }

    class StencilMaskSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            m_vertexShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "simpleLighting.vert.hlsl").c_str(), "lighting_vs");
            m_pixelShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "simpleLighting.pixel.hlsl").c_str(), "lighting_ps");
            if (!m_vertexShader || !m_pixelShader)
                return false;

            // ------------ Build geometries --------------
            using namespace pwgpu::test::primitives;

            VertexData planeData = CreatePlaneVertices();
            float xlate[16];
            MakeTranslation(xlate, 0.0f, 0.5f, 0.0f);
            ReorientInPlace(planeData, xlate);
            if (!CreateGeometryFromData(ctx, planeData, m_planeGeo, "plane"))
                return false;

            VertexData sphereData = CreateSphereVertices();
            if (!CreateGeometryFromData(ctx, sphereData, m_sphereGeo, "sphere"))
                return false;

            VertexData torusData = CreateTorusVertices(1.0f, 0.5f);
            if (!CreateGeometryFromData(ctx, torusData, m_torusGeo, "torus"))
                return false;

            VertexData cubeData = CreateCubeVertices();
            if (!CreateGeometryFromData(ctx, cubeData, m_cubeGeo, "cube"))
                return false;

            VertexData coneData = CreateTruncatedConeVertices();
            if (!CreateGeometryFromData(ctx, coneData, m_coneGeo, "cone"))
                return false;

            VertexData cylinderData = CreateCylinderVertices();
            if (!CreateGeometryFromData(ctx, cylinderData, m_cylinderGeo, "cylinder"))
                return false;

            VertexData jemBase = CreateSphereVertices(1.0f, 6, 5);
            VertexData jemData = Facet(jemBase);
            if (!CreateGeometryFromData(ctx, jemData, m_jemGeo, "jem"))
                return false;

            VertexData diceBase = CreateTorusVertices(1.0f, 0.5f, 8, 8);
            VertexData diceData = Facet(diceBase);
            if (!CreateGeometryFromData(ctx, diceData, m_diceGeo, "dice"))
                return false;

            // ------------ Bind group layout + pipeline layout -----------
            WGPUBindGroupLayoutEntry bglEntries[2]{};
            bglEntries[0].binding = 0;
            bglEntries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
            bglEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
            bglEntries[0].buffer.minBindingSize = kObjectUboSize;
            bglEntries[1].binding = 1;
            bglEntries[1].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
            bglEntries[1].buffer.type = WGPUBufferBindingType_Uniform;
            bglEntries[1].buffer.minBindingSize = kSharedUboSize;

            WGPUBindGroupLayoutDescriptor bglDesc{};
            bglDesc.label = {"stencil_bgl", WGPU_STRLEN};
            bglDesc.entryCount = 2;
            bglDesc.entries = bglEntries;
            m_bindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device, &bglDesc);
            if (!m_bindGroupLayout)
                return false;

            WGPUPipelineLayoutDescriptor plDesc{};
            plDesc.label = {"stencil_pl", WGPU_STRLEN};
            plDesc.bindGroupLayoutCount = 1;
            plDesc.bindGroupLayouts = &m_bindGroupLayout;
            m_pipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device, &plDesc);
            if (!m_pipelineLayout)
                return false;

            // ------------ Pipelines -----------
            if (!CreatePipelines(ctx))
                return false;

            if (!CreateUniformBuffer(ctx))
                return false;

            // ------------ Build scenes -----------
            std::mt19937 rng(1337u);
            std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

            auto randf = [&](float a, float b)
            { return a + dist01(rng) * (b - a); };

            // 6 mask scenes (each 1 plane).
            m_maskScenes.resize(kNumMaskScenes);
            for (uint32_t i = 0; i < kNumMaskScenes; ++i)
            {
                const float hue = static_cast<float>(i) / 6.0f + 0.5f;
                if (!BuildScene(ctx, m_maskScenes[i], 1, hue, &m_planeGeo, randf))
                    return false;
            }

            // 7 object scenes.
            const Geometry *geos[kNumObjectScenes] = {
                &m_sphereGeo, &m_cubeGeo, &m_torusGeo, &m_coneGeo,
                &m_cylinderGeo, &m_jemGeo, &m_diceGeo};
            m_objectScenes.resize(kNumObjectScenes);
            for (uint32_t i = 0; i < kNumObjectScenes; ++i)
            {
                const float hue = static_cast<float>(i) / 7.0f;
                if (!BuildScene(ctx, m_objectScenes[i], kNumObjectsPerScene, hue, geos[i], randf))
                    return false;
            }

            if (!CreateRenderBundles(ctx))
                return false;

            return m_nextObjectUniformSlot == kTotalObjectCount &&
                   m_nextSceneUniformSlot == kTotalSceneCount;
        }

        void Resize(pwgpu::test::SampleContext &ctx, uint32_t width, uint32_t height) override
        {
            ReleaseDepthTarget();
            if (width == 0 || height == 0)
                return;

            WGPUTextureDescriptor depthDesc{};
            depthDesc.label = {"stencil_depth", WGPU_STRLEN};
            depthDesc.dimension = WGPUTextureDimension_2D;
            depthDesc.size = {width, height, 1};
            depthDesc.mipLevelCount = 1;
            depthDesc.sampleCount = 1;
            depthDesc.format = kDepthStencilFormat;
            depthDesc.usage = WGPUTextureUsage_RenderAttachment;
            m_depthTexture = wgpuDeviceCreateTexture(ctx.device, &depthDesc);
            if (!m_depthTexture)
                return;

            WGPUTextureViewDescriptor dvd{};
            dvd.format = kDepthStencilFormat;
            dvd.dimension = WGPUTextureViewDimension_2D;
            dvd.mipLevelCount = 1;
            dvd.arrayLayerCount = 1;
            dvd.aspect = WGPUTextureAspect_All;
            m_depthView = wgpuTextureCreateView(m_depthTexture, &dvd);
        }

        void Update(pwgpu::test::SampleContext &ctx) override
        {
            const float aspect =
                ctx.height > 0
                    ? static_cast<float>(ctx.width) / static_cast<float>(ctx.height)
                    : 1.0f;
            const float time = static_cast<float>(ctx.timing.elapsedSeconds);

            // Mask scenes: fixed camera at (0,0,45).
            const float rotations[kNumMaskScenes][3] = {
                {0.0f, 0.0f, 0.0f},
                {1.0f, 0.0f, 0.0f},
                {0.0f, 0.0f, 0.5f},
                {0.0f, 0.0f, -0.5f},
                {-0.5f, 0.0f, 0.0f},
                {0.5f, 0.0f, 0.0f},
            };

            // Slow circular "mouse" to keep scenes visually alive (upstream uses real mouse).
            const float mx = std::sin(time * 0.2f) * 0.08f;
            const float my = std::cos(time * 0.17f) * 0.08f;

            for (uint32_t i = 0; i < kNumMaskScenes; ++i)
            {
                UpdateMask(m_maskScenes[i], time, aspect, rotations[i], mx, my);
            }

            // Object scenes: scene0 fixed cam, scene1 orbiting, alternating.
            for (uint32_t i = 0; i < kNumObjectScenes; ++i)
            {
                if ((i % 2) == 0)
                    UpdateScene0(m_objectScenes[i], time, aspect);
                else
                    UpdateScene1(m_objectScenes[i], time, aspect);
            }

            UploadUniforms(ctx);
        }

        bool Execute(pwgpu::test::SampleContext &, pwgpu::test::SampleFrame &frame) override
        {
            if (!m_depthView)
                return true;

            DrawMaskScenesPass(frame);

            // Object scenes: scene0 at ref=0 (background), scenes 1..6 at ref 1..6.
            const uint32_t refs[kNumObjectScenes] = {0, 1, 2, 3, 4, 5, 6};
            for (uint32_t i = 0; i < kNumObjectScenes; ++i)
            {
                DrawScenePass(frame, false, m_objectScenes[i], refs[i]);
            }
            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            ReleaseDepthTarget();
            for (auto &s : m_maskScenes)
                ReleaseScene(s);
            for (auto &s : m_objectScenes)
                ReleaseScene(s);
            m_maskScenes.clear();
            m_objectScenes.clear();

            for (uint32_t i = 0; i < kUniformChunkCount; ++i)
            {
                if (m_uniformBuffers[i])
                    wgpuBufferRelease(m_uniformBuffers[i]);
                m_uniformBuffers[i] = nullptr;
                m_uniformStaging[i].clear();
            }
            m_nextObjectUniformSlot = 0;
            m_nextSceneUniformSlot = 0;

            ReleaseGeometry(m_planeGeo);
            ReleaseGeometry(m_sphereGeo);
            ReleaseGeometry(m_torusGeo);
            ReleaseGeometry(m_cubeGeo);
            ReleaseGeometry(m_coneGeo);
            ReleaseGeometry(m_cylinderGeo);
            ReleaseGeometry(m_jemGeo);
            ReleaseGeometry(m_diceGeo);

            if (m_stencilMaskPipeline)
                wgpuRenderPipelineRelease(m_stencilMaskPipeline);
            if (m_stencilSetPipeline)
                wgpuRenderPipelineRelease(m_stencilSetPipeline);
            if (m_pipelineLayout)
                wgpuPipelineLayoutRelease(m_pipelineLayout);
            if (m_bindGroupLayout)
                wgpuBindGroupLayoutRelease(m_bindGroupLayout);
            if (m_pixelShader)
                wgpuShaderModuleRelease(m_pixelShader);
            if (m_vertexShader)
                wgpuShaderModuleRelease(m_vertexShader);

            m_stencilMaskPipeline = nullptr;
            m_stencilSetPipeline = nullptr;
            m_pipelineLayout = nullptr;
            m_bindGroupLayout = nullptr;
            m_pixelShader = nullptr;
            m_vertexShader = nullptr;
        }

    private:
        bool CreatePipelines(pwgpu::test::SampleContext &ctx)
        {
            WGPUVertexAttribute attribs[3]{};
            attribs[0].format = WGPUVertexFormat_Float32x3;
            attribs[0].offset = 0;
            attribs[0].shaderLocation = 0;
            attribs[1].format = WGPUVertexFormat_Float32x3;
            attribs[1].offset = 12;
            attribs[1].shaderLocation = 1;
            attribs[2].format = WGPUVertexFormat_Float32x2;
            attribs[2].offset = 24;
            attribs[2].shaderLocation = 2;

            WGPUVertexBufferLayout vbLayout{};
            vbLayout.arrayStride = 32;
            vbLayout.stepMode = WGPUVertexStepMode_Vertex;
            vbLayout.attributeCount = 3;
            vbLayout.attributes = attribs;

            WGPUColorTargetState colorTarget{};
            colorTarget.format = ctx.surfaceFormat;
            colorTarget.writeMask = WGPUColorWriteMask_All;

            WGPUFragmentState fragmentState{};
            fragmentState.module = m_pixelShader;
            fragmentState.entryPoint = {"PSMain", WGPU_STRLEN};
            fragmentState.targetCount = 1;
            fragmentState.targets = &colorTarget;

            // Shared depth-stencil settings.
            WGPUDepthStencilState setState{};
            setState.format = kDepthStencilFormat;
            setState.depthWriteEnabled = WGPUOptionalBool_True;
            setState.depthCompare = WGPUCompareFunction_Less;
            setState.stencilReadMask = 0xFF;
            setState.stencilWriteMask = 0xFF;
            setState.stencilFront.compare = WGPUCompareFunction_Always;
            setState.stencilFront.failOp = WGPUStencilOperation_Keep;
            setState.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
            setState.stencilFront.passOp = WGPUStencilOperation_Replace;
            setState.stencilBack = setState.stencilFront;

            WGPURenderPipelineDescriptor setDesc{};
            setDesc.label = {"stencil_set_pipe", WGPU_STRLEN};
            setDesc.layout = m_pipelineLayout;
            setDesc.vertex.module = m_vertexShader;
            setDesc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            setDesc.vertex.bufferCount = 1;
            setDesc.vertex.buffers = &vbLayout;
            setDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            setDesc.primitive.cullMode = WGPUCullMode_Back;
            setDesc.primitive.frontFace = WGPUFrontFace_CCW;
            setDesc.depthStencil = &setState;
            setDesc.multisample.count = 1;
            setDesc.multisample.mask = 0xFFFFFFFF;
            setDesc.fragment = &fragmentState;
            m_stencilSetPipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &setDesc);
            if (!m_stencilSetPipeline)
                return false;

            WGPUDepthStencilState maskState = setState;
            maskState.stencilFront.compare = WGPUCompareFunction_Equal;
            maskState.stencilFront.passOp = WGPUStencilOperation_Keep;
            maskState.stencilBack = maskState.stencilFront;

            WGPURenderPipelineDescriptor maskDesc = setDesc;
            maskDesc.label = {"stencil_mask_pipe", WGPU_STRLEN};
            maskDesc.depthStencil = &maskState;
            m_stencilMaskPipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &maskDesc);
            if (!m_stencilMaskPipeline)
                return false;

            return true;
        }

        bool CreateGeometryFromData(pwgpu::test::SampleContext &ctx,
                                    const pwgpu::test::primitives::VertexData &data,
                                    Geometry &out,
                                    const char *label)
        {
            const uint64_t vbSize = data.vertices.size() * sizeof(float);
            const uint64_t ibSize = data.indices.size() * sizeof(uint16_t);
            // Pad index buffer to a multiple of 4 bytes (WebGPU requirement).
            const uint64_t ibSizePadded = (ibSize + 3) & ~uint64_t(3);

            out.vertexBuffer = pwgpu::test::CreateBuffer(
                ctx.device, vbSize,
                static_cast<WGPUBufferUsage>(WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst),
                label);
            out.indexBuffer = pwgpu::test::CreateBuffer(
                ctx.device, ibSizePadded,
                static_cast<WGPUBufferUsage>(WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst),
                label);
            if (!out.vertexBuffer || !out.indexBuffer)
                return false;

            wgpuQueueWriteBuffer(ctx.queue, out.vertexBuffer, 0, data.vertices.data(), vbSize);
            wgpuQueueWriteBuffer(ctx.queue, out.indexBuffer, 0, data.indices.data(), ibSize);
            out.vertexBufferSize = vbSize;
            out.indexBufferSize = ibSizePadded;
            out.indexCount = static_cast<uint32_t>(data.indices.size());
            return true;
        }

        bool CreateUniformBuffer(pwgpu::test::SampleContext &ctx)
        {
            for (uint32_t chunk = 0; chunk < kUniformChunkCount; ++chunk)
            {
                const uint32_t firstSlot = chunk * kUniformSlotsPerChunk;
                const uint32_t remainingSlots = kTotalUniformSlotCount - firstSlot;
                const uint32_t slotCount = remainingSlots < kUniformSlotsPerChunk
                                               ? remainingSlots
                                               : kUniformSlotsPerChunk;
                const uint64_t chunkSize = slotCount * kUniformSlotSize;

                m_uniformBuffers[chunk] = pwgpu::test::CreateBuffer(
                    ctx.device, chunkSize,
                    static_cast<WGPUBufferUsage>(WGPUBufferUsage_Uniform |
                                                 WGPUBufferUsage_CopyDst),
                    "stencil_uniforms");
                if (!m_uniformBuffers[chunk])
                    return false;

                m_uniformStaging[chunk].assign(static_cast<size_t>(chunkSize), 0);
            }
            return true;
        }

        template <typename RandF>
        bool BuildScene(pwgpu::test::SampleContext &ctx,
                        Scene &scene,
                        uint32_t numInstances,
                        float hue,
                        const Geometry *geometry,
                        RandF &&randf)
        {
            if (m_nextSceneUniformSlot >= kTotalSceneCount)
                return false;
            scene.uniformSlot = kSharedUniformBaseSlot + m_nextSceneUniformSlot++;

            scene.objects.resize(numInstances);
            for (uint32_t i = 0; i < numInstances; ++i)
            {
                if (m_nextObjectUniformSlot >= kTotalObjectCount)
                    return false;

                ObjectInfo &obj = scene.objects[i];
                obj.geometry = geometry;
                obj.uniformSlot = m_nextObjectUniformSlot++;

                const float h = hue + randf(0.0f, 0.2f);
                const float s = randf(0.7f, 1.0f);
                const float l = randf(0.5f, 0.8f);
                HslToRgba(h, s, l, obj.color);

                WGPUBindGroupEntry bgEntries[2]{};
                bgEntries[0].binding = 0;
                bgEntries[0].buffer = GetUniformBuffer(obj.uniformSlot);
                bgEntries[0].offset = GetUniformOffset(obj.uniformSlot);
                bgEntries[0].size = kObjectUboSize;
                bgEntries[1].binding = 1;
                bgEntries[1].buffer = GetUniformBuffer(scene.uniformSlot);
                bgEntries[1].offset = GetUniformOffset(scene.uniformSlot);
                bgEntries[1].size = kSharedUboSize;

                WGPUBindGroupDescriptor bgDesc{};
                bgDesc.label = {"object_bg", WGPU_STRLEN};
                bgDesc.layout = m_bindGroupLayout;
                bgDesc.entryCount = 2;
                bgDesc.entries = bgEntries;
                obj.bindGroup = wgpuDeviceCreateBindGroup(ctx.device, &bgDesc);
                if (!obj.bindGroup)
                    return false;
            }
            return true;
        }

        void UpdateMask(Scene &scene,
                        float time,
                        float aspect,
                        const float rotation[3],
                        float mouseX,
                        float mouseY)
        {
            float projection[16];
            MatPerspectiveRH(30.0f * kPi / 180.0f, aspect, 0.5f, 100.0f, projection);
            projection[5] *= -1.0f; // Vulkan Y-flip.

            const float eye[3] = {0.0f, 0.0f, 45.0f};
            const float target[3] = {0.0f, 0.0f, 0.0f};
            const float up[3] = {0.0f, 1.0f, 0.0f};
            float view[16];
            MatLookAtRH(eye, target, up, view);

            MatMul(projection, view, scene.viewProjection);
            StageSharedUniforms(scene);

            for (auto &obj : scene.objects)
            {
                float m[16];
                MatIdentity(m);
                const float worldX = mouseX * 10.0f;
                const float worldY = mouseY * 10.0f;
                MatTranslate(m, worldX, worldY, 0.0f);
                MatRotateX(m, time * 0.25f);
                MatRotateY(m, time * 0.15f);
                MatRotateX(m, rotation[0] * kPi);
                MatRotateZ(m, rotation[2] * kPi);
                MatScale(m, 10.0f, 10.0f, 10.0f);
                std::memcpy(obj.world, m, sizeof(m));
                StageObjectUniforms(obj);
            }
        }

        void UpdateScene0(Scene &scene, float time, float aspect)
        {
            float projection[16];
            MatPerspectiveRH(30.0f * kPi / 180.0f, aspect, 0.5f, 100.0f, projection);
            projection[5] *= -1.0f;

            const float eye[3] = {0.0f, 0.0f, 35.0f};
            const float target[3] = {0.0f, 0.0f, 0.0f};
            const float up[3] = {0.0f, 1.0f, 0.0f};
            float view[16];
            MatLookAtRH(eye, target, up, view);
            MatMul(projection, view, scene.viewProjection);
            StageSharedUniforms(scene);

            for (uint32_t i = 0; i < scene.objects.size(); ++i)
            {
                auto &obj = scene.objects[i];
                float m[16];
                MatIdentity(m);
                const float fi = static_cast<float>(i);
                MatTranslate(m, 0.0f, 0.0f, std::sin(fi * 3.721f + time * 0.1f) * 10.0f);
                MatRotateX(m, fi * 4.567f);
                MatRotateY(m, fi * 2.967f);
                MatTranslate(m, 0.0f, 0.0f, std::sin(fi * 9.721f + time * 0.1f) * 10.0f);
                MatRotateX(m, time * 0.53f + fi);
                std::memcpy(obj.world, m, sizeof(m));
                StageObjectUniforms(obj);
            }
        }

        void UpdateScene1(Scene &scene, float time, float aspect)
        {
            float projection[16];
            MatPerspectiveRH(30.0f * kPi / 180.0f, aspect, 0.5f, 100.0f, projection);
            projection[5] *= -1.0f;

            const float radius = 35.0f;
            const float t = time * 0.1f;
            const float eye[3] = {std::cos(t) * radius, 4.0f, std::sin(t) * radius};
            const float target[3] = {0.0f, 0.0f, 0.0f};
            const float up[3] = {0.0f, 1.0f, 0.0f};
            float view[16];
            MatLookAtRH(eye, target, up, view);
            MatMul(projection, view, scene.viewProjection);
            StageSharedUniforms(scene);

            for (uint32_t i = 0; i < scene.objects.size(); ++i)
            {
                auto &obj = scene.objects[i];
                float m[16];
                MatIdentity(m);
                const float fi = static_cast<float>(i);
                MatTranslate(m, 0.0f, 0.0f, std::sin(fi * 3.721f + time * 0.1f) * 10.0f);
                MatRotateX(m, fi * 4.567f);
                MatRotateY(m, fi * 2.967f);
                MatTranslate(m, 0.0f, 0.0f, std::sin(fi * 9.721f + time * 0.1f) * 10.0f);
                MatRotateX(m, time * 1.53f + fi);
                std::memcpy(obj.world, m, sizeof(m));
                StageObjectUniforms(obj);
            }
        }

        void StageSharedUniforms(Scene &scene)
        {
            // Normalize light direction (1, 8, 10).
            const float lx = 1.0f, ly = 8.0f, lz = 10.0f;
            const float inv = 1.0f / std::sqrt(lx * lx + ly * ly + lz * lz);
            scene.lightDirection[0] = lx * inv;
            scene.lightDirection[1] = ly * inv;
            scene.lightDirection[2] = lz * inv;
            scene.lightDirection[3] = 0.0f;

            float payload[20];
            std::memcpy(payload, scene.viewProjection, sizeof(scene.viewProjection));
            std::memcpy(&payload[16], scene.lightDirection, sizeof(scene.lightDirection));
            StageUniformSlot(scene.uniformSlot, payload, sizeof(payload));
        }

        void StageObjectUniforms(const ObjectInfo &obj)
        {
            float payload[20];
            std::memcpy(payload, obj.world, sizeof(obj.world));
            std::memcpy(&payload[16], obj.color, sizeof(obj.color));
            StageUniformSlot(obj.uniformSlot, payload, sizeof(payload));
        }

        WGPUBuffer GetUniformBuffer(uint32_t slot) const
        {
            return m_uniformBuffers[slot / kUniformSlotsPerChunk];
        }

        uint64_t GetUniformOffset(uint32_t slot) const
        {
            return (slot % kUniformSlotsPerChunk) * kUniformSlotSize;
        }

        void StageUniformSlot(uint32_t slot, const void *payload, size_t size)
        {
            const uint32_t chunk = slot / kUniformSlotsPerChunk;
            std::memcpy(m_uniformStaging[chunk].data() + GetUniformOffset(slot), payload, size);
        }

        void UploadUniforms(pwgpu::test::SampleContext &ctx)
        {
            for (uint32_t i = 0; i < kUniformChunkCount; ++i)
            {
                if (!m_uniformBuffers[i] || m_uniformStaging[i].empty())
                    continue;

                wgpuQueueWriteBuffer(ctx.queue,
                                     m_uniformBuffers[i],
                                     0,
                                     m_uniformStaging[i].data(),
                                     m_uniformStaging[i].size());
            }
        }

        void RecordSceneObjects(WGPURenderBundleEncoder bundleEncoder, Scene &scene)
        {
            const Geometry *lastGeo = nullptr;
            for (auto &obj : scene.objects)
            {
                if (obj.geometry != lastGeo)
                {
                    wgpuRenderBundleEncoderSetVertexBuffer(
                        bundleEncoder, 0, obj.geometry->vertexBuffer, 0,
                        obj.geometry->vertexBufferSize);
                    wgpuRenderBundleEncoderSetIndexBuffer(
                        bundleEncoder, obj.geometry->indexBuffer, WGPUIndexFormat_Uint16, 0,
                        obj.geometry->indexBufferSize);
                    lastGeo = obj.geometry;
                }
                wgpuRenderBundleEncoderSetBindGroup(
                    bundleEncoder, 0, obj.bindGroup, 0, nullptr);
                wgpuRenderBundleEncoderDrawIndexed(
                    bundleEncoder, obj.geometry->indexCount, 1, 0, 0, 0);
            }
        }

        bool CreateRenderBundles(pwgpu::test::SampleContext &ctx)
        {
            for (auto &scene : m_maskScenes)
            {
                scene.bundle = CreateRenderBundle(ctx, scene, m_stencilSetPipeline,
                                                  "stencil_mask_bundle");
                if (!scene.bundle)
                    return false;
            }

            for (auto &scene : m_objectScenes)
            {
                scene.bundle = CreateRenderBundle(ctx, scene, m_stencilMaskPipeline,
                                                  "stencil_scene_bundle");
                if (!scene.bundle)
                    return false;
            }

            return true;
        }

        WGPURenderBundle CreateRenderBundle(pwgpu::test::SampleContext &ctx,
                                            Scene &scene,
                                            WGPURenderPipeline pipeline,
                                            const char *label)
        {
            WGPUTextureFormat colorFormat = ctx.surfaceFormat;

            WGPURenderBundleEncoderDescriptor rbeDesc{};
            rbeDesc.label = {label, WGPU_STRLEN};
            rbeDesc.colorFormatCount = 1;
            rbeDesc.colorFormats = &colorFormat;
            rbeDesc.depthStencilFormat = kDepthStencilFormat;
            rbeDesc.sampleCount = 1;
            rbeDesc.depthReadOnly = WGPUOptionalBool_False;
            rbeDesc.stencilReadOnly = WGPUOptionalBool_False;

            WGPURenderBundleEncoder rbe =
                wgpuDeviceCreateRenderBundleEncoder(ctx.device, &rbeDesc);
            if (!rbe)
                return nullptr;

            wgpuRenderBundleEncoderSetPipeline(rbe, pipeline);
            RecordSceneObjects(rbe, scene);

            WGPURenderBundleDescriptor bundleDesc{};
            bundleDesc.label = {label, WGPU_STRLEN};
            WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(rbe, &bundleDesc);
            wgpuRenderBundleEncoderRelease(rbe);
            return bundle;
        }

        void DrawMaskScenesPass(pwgpu::test::SampleFrame &frame)
        {
            WGPURenderPassColorAttachment colorAttachment{};
            colorAttachment.view = frame.surfaceView;
            colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            colorAttachment.loadOp = WGPULoadOp_Clear;
            colorAttachment.storeOp = WGPUStoreOp_Store;
            colorAttachment.clearValue = {0.2, 0.2, 0.2, 1.0};

            WGPURenderPassDepthStencilAttachment depthAttachment{};
            depthAttachment.view = m_depthView;
            depthAttachment.depthClearValue = 1.0f;
            depthAttachment.depthLoadOp = WGPULoadOp_Clear;
            depthAttachment.depthStoreOp = WGPUStoreOp_Store;
            depthAttachment.stencilClearValue = 0;
            depthAttachment.stencilLoadOp = WGPULoadOp_Clear;
            depthAttachment.stencilStoreOp = WGPUStoreOp_Store;

            WGPURenderPassDescriptor passDesc{};
            passDesc.label = {"stencil_mask_set_pass", WGPU_STRLEN};
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments = &colorAttachment;
            passDesc.depthStencilAttachment = &depthAttachment;

            WGPURenderPassEncoder pass =
                wgpuCommandEncoderBeginRenderPass(frame.encoder, &passDesc);
            wgpuRenderPassEncoderSetPipeline(pass, m_stencilSetPipeline);

            for (uint32_t i = 0; i < kNumMaskScenes; ++i)
            {
                wgpuRenderPassEncoderSetStencilReference(pass, i + 1);
                wgpuRenderPassEncoderExecuteBundles(pass, 1, &m_maskScenes[i].bundle);
            }

            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
        }

        void DrawScenePass(pwgpu::test::SampleFrame &frame,
                           bool clearAll,
                           Scene &scene,
                           uint32_t stencilRef)
        {
            WGPURenderPassColorAttachment colorAttachment{};
            colorAttachment.view = frame.surfaceView;
            colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            colorAttachment.loadOp = clearAll ? WGPULoadOp_Clear : WGPULoadOp_Load;
            colorAttachment.storeOp = WGPUStoreOp_Store;
            if (clearAll)
                colorAttachment.clearValue = {0.2, 0.2, 0.2, 1.0};

            WGPURenderPassDepthStencilAttachment depthAttachment{};
            depthAttachment.view = m_depthView;
            depthAttachment.depthClearValue = 1.0f;
            depthAttachment.depthLoadOp = WGPULoadOp_Clear;
            depthAttachment.depthStoreOp = WGPUStoreOp_Store;
            depthAttachment.stencilClearValue = 0;
            depthAttachment.stencilLoadOp = clearAll ? WGPULoadOp_Clear : WGPULoadOp_Load;
            depthAttachment.stencilStoreOp = WGPUStoreOp_Store;

            WGPURenderPassDescriptor passDesc{};
            passDesc.label = {"stencil_pass", WGPU_STRLEN};
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments = &colorAttachment;
            passDesc.depthStencilAttachment = &depthAttachment;

            WGPURenderPassEncoder pass =
                wgpuCommandEncoderBeginRenderPass(frame.encoder, &passDesc);
            wgpuRenderPassEncoderSetStencilReference(pass, stencilRef);
            wgpuRenderPassEncoderExecuteBundles(pass, 1, &scene.bundle);

            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
        }

        void ReleaseGeometry(Geometry &g)
        {
            if (g.vertexBuffer)
                wgpuBufferRelease(g.vertexBuffer);
            if (g.indexBuffer)
                wgpuBufferRelease(g.indexBuffer);
            g.vertexBuffer = nullptr;
            g.indexBuffer = nullptr;
            g.indexCount = 0;
        }

        void ReleaseScene(Scene &scene)
        {
            if (scene.bundle)
                wgpuRenderBundleRelease(scene.bundle);
            scene.bundle = nullptr;

            for (auto &obj : scene.objects)
            {
                if (obj.bindGroup)
                    wgpuBindGroupRelease(obj.bindGroup);
                obj.bindGroup = nullptr;
            }
            scene.objects.clear();
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
        WGPUBindGroupLayout m_bindGroupLayout = nullptr;
        WGPUPipelineLayout m_pipelineLayout = nullptr;
        WGPURenderPipeline m_stencilSetPipeline = nullptr;
        WGPURenderPipeline m_stencilMaskPipeline = nullptr;
        WGPUBuffer m_uniformBuffers[kUniformChunkCount]{};
        std::vector<uint8_t> m_uniformStaging[kUniformChunkCount];
        uint32_t m_nextObjectUniformSlot = 0;
        uint32_t m_nextSceneUniformSlot = 0;
        WGPUTexture m_depthTexture = nullptr;
        WGPUTextureView m_depthView = nullptr;

        Geometry m_planeGeo{};
        Geometry m_sphereGeo{};
        Geometry m_torusGeo{};
        Geometry m_cubeGeo{};
        Geometry m_coneGeo{};
        Geometry m_cylinderGeo{};
        Geometry m_jemGeo{};
        Geometry m_diceGeo{};

        std::vector<Scene> m_maskScenes;
        std::vector<Scene> m_objectScenes;
    };

    pwgpu::test::SampleAppDesc MakeStencilMaskDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "StencilMaskTest";
        desc.windowTitle = "PhasmaWebGPU Stencil Mask";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    StencilMaskSample sample;
    pwgpu::test::SampleApp app(sample, MakeStencilMaskDesc());
    return app.Run(argc, argv);
}
