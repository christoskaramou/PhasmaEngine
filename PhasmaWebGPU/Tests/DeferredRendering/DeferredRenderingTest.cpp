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
    constexpr int kMaxNumLights = 1024;
    constexpr int kNumLights = 128;
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kLightExtentMin[3] = {-50.f, -30.f, -50.f};
    constexpr float kLightExtentMax[3] = {50.f, 50.f, 50.f};

    struct Vertex
    {
        float pos[3];
        float normal[3];
        float uv[2];
    };
    static_assert(sizeof(Vertex) == 32);

    struct LightData
    {
        float pos[4];
        float color[3];
        float radius;
    };
    static_assert(sizeof(LightData) == 32);

    struct ModelUniforms
    {
        float modelMatrix[16];
        float normalModelMatrix[16];
    };

    struct CameraUniforms
    {
        float viewProjectionMatrix[16];
        float invViewProjectionMatrix[16];
    };

    struct Config
    {
        uint32_t numLights;
        float dtScale;
        uint32_t pad[2];
    };

    struct LightExtent
    {
        float min[4];
        float max[4];
    };

    bool LoadDragon(const std::string &path,
                    std::vector<Vertex> &vertices,
                    std::vector<uint32_t> &indices)
    {
        FILE *file = fopen(path.c_str(), "rb");
        if (!file)
        {
            fprintf(stderr, "Could not open %s\n", path.c_str());
            return false;
        }

        uint32_t vertexCount = 0;
        uint32_t triangleCount = 0;
        fread(&vertexCount, sizeof(uint32_t), 1, file);
        fread(&triangleCount, sizeof(uint32_t), 1, file);
        vertices.resize(vertexCount);
        indices.resize(static_cast<size_t>(triangleCount) * 3u);
        fread(vertices.data(), sizeof(Vertex), vertexCount, file);
        fread(indices.data(), sizeof(uint32_t), static_cast<size_t>(triangleCount) * 3u, file);
        fclose(file);

        fprintf(stdout, "[mesh] dragon: %u verts, %u tris\n", vertexCount, triangleCount);
        return true;
    }

    class DeferredRenderingSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            const std::string dragonPath =
                pwgpu::test::GetAssetPath(ctx.exeDir, "Assets/stanfordDragon.bin");
            if (!LoadDragon(dragonPath, m_vertices, m_indices))
                return false;

            m_gbufferVertexShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "gbuffer.vert.hlsl").c_str(), "gbuffer_vs");
            m_gbufferPixelShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "gbuffer.pixel.hlsl").c_str(), "gbuffer_ps");
            m_quadVertexShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "quad.vert.hlsl").c_str(), "quad_vs");
            m_deferredPixelShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "deferred.pixel.hlsl").c_str(), "deferred_ps");
            m_lightsComputeShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "lights.comp.hlsl").c_str(), "lights_cs");
            if (!m_gbufferVertexShader || !m_gbufferPixelShader || !m_quadVertexShader ||
                !m_deferredPixelShader || !m_lightsComputeShader)
            {
                return false;
            }

            if (!CreateBuffers(ctx) || !CreatePipelineResources(ctx))
                return false;

            fprintf(stdout, "[Pipelines] G-buffer, Deferred, Compute - all created\n");
            return true;
        }

        void Resize(pwgpu::test::SampleContext &ctx, uint32_t width, uint32_t height) override
        {
            ReleaseResizeTargets();
            if (width == 0 || height == 0)
                return;

            m_gbufferNormalTexture = CreateRenderTexture(ctx.device,
                                                         width,
                                                         height,
                                                         WGPUTextureFormat_RGBA16Float,
                                                         static_cast<WGPUTextureUsage>(
                                                             WGPUTextureUsage_RenderAttachment |
                                                             WGPUTextureUsage_TextureBinding),
                                                         "gbuf_normal");
            m_gbufferAlbedoTexture = CreateRenderTexture(ctx.device,
                                                         width,
                                                         height,
                                                         WGPUTextureFormat_RGBA8Unorm,
                                                         static_cast<WGPUTextureUsage>(
                                                             WGPUTextureUsage_RenderAttachment |
                                                             WGPUTextureUsage_TextureBinding),
                                                         "gbuf_albedo");
            m_depthTexture = CreateRenderTexture(ctx.device,
                                                 width,
                                                 height,
                                                 WGPUTextureFormat_Depth24Plus,
                                                 static_cast<WGPUTextureUsage>(
                                                     WGPUTextureUsage_RenderAttachment |
                                                     WGPUTextureUsage_TextureBinding),
                                                 "gbuf_depth");
            if (!m_gbufferNormalTexture || !m_gbufferAlbedoTexture || !m_depthTexture)
                return;

            m_gbufferNormalView = CreateTextureView(m_gbufferNormalTexture,
                                                    WGPUTextureFormat_RGBA16Float,
                                                    WGPUTextureAspect_All,
                                                    "v_normal");
            m_gbufferAlbedoView = CreateTextureView(m_gbufferAlbedoTexture,
                                                    WGPUTextureFormat_RGBA8Unorm,
                                                    WGPUTextureAspect_All,
                                                    "v_albedo");
            m_depthViewRender = CreateTextureView(m_depthTexture,
                                                  WGPUTextureFormat_Depth24Plus,
                                                  WGPUTextureAspect_All,
                                                  "v_depth_rt");
            m_depthViewRead = CreateTextureView(m_depthTexture,
                                                WGPUTextureFormat_Depth24Plus,
                                                WGPUTextureAspect_DepthOnly,
                                                "v_depth_read");
            if (!m_gbufferNormalView || !m_gbufferAlbedoView || !m_depthViewRender ||
                !m_depthViewRead)
            {
                return;
            }

            RebuildGBufferBindGroup(ctx.device);
        }

        void Update(pwgpu::test::SampleContext &ctx) override
        {
            const float aspect =
                ctx.height > 0 ? static_cast<float>(ctx.width) / static_cast<float>(ctx.height)
                               : 1.0f;
            mat4 projection =
                glm::perspectiveRH_ZO((2.f * kPi) / 5.f, aspect, 1.f, 2000.f);
            projection[1][1] *= -1.f;

            const float radians =
                kPi * static_cast<float>(ctx.timing.elapsedSeconds) / 5.0f;
            vec3 eyeBase(0.f, 50.f, -100.f);
            vec3 eye(std::cos(radians) * eyeBase.x + std::sin(radians) * eyeBase.z,
                     eyeBase.y,
                     -std::sin(radians) * eyeBase.x + std::cos(radians) * eyeBase.z);
            mat4 view = glm::lookAtRH(eye, vec3(0.f, 0.f, 0.f), vec3(0.f, 1.f, 0.f));
            mat4 viewProjection = projection * view;
            mat4 inverseViewProjection = glm::inverse(viewProjection);

            CameraUniforms camera{};
            memcpy(camera.viewProjectionMatrix, glm::value_ptr(viewProjection), 64);
            memcpy(camera.invViewProjectionMatrix, glm::value_ptr(inverseViewProjection), 64);
            wgpuQueueWriteBuffer(ctx.queue, m_cameraBuffer, 0, &camera, sizeof(camera));

            Config config{static_cast<uint32_t>(kNumLights),
                          static_cast<float>(ctx.timing.deltaSeconds) * 60.0f,
                          {0, 0}};
            wgpuQueueWriteBuffer(ctx.queue, m_configBuffer, 0, &config, sizeof(config));
        }

        bool Execute(pwgpu::test::SampleContext &,
                     pwgpu::test::SampleFrame &frame) override
        {
            if (!m_gbufferPipeline || !m_deferredPipeline || !m_lightsPipeline ||
                !m_gbufferBindGroup)
            {
                return true;
            }

            {
                WGPURenderPassColorAttachment colorAttachments[2] = {};
                colorAttachments[0].view = m_gbufferNormalView;
                colorAttachments[0].depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
                colorAttachments[0].loadOp = WGPULoadOp_Clear;
                colorAttachments[0].storeOp = WGPUStoreOp_Store;
                colorAttachments[0].clearValue = {0.0, 0.0, 1.0, 1.0};
                colorAttachments[1].view = m_gbufferAlbedoView;
                colorAttachments[1].depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
                colorAttachments[1].loadOp = WGPULoadOp_Clear;
                colorAttachments[1].storeOp = WGPUStoreOp_Store;
                colorAttachments[1].clearValue = {0.0, 0.0, 0.0, 1.0};

                WGPURenderPassDepthStencilAttachment depthAttachment{};
                depthAttachment.view = m_depthViewRender;
                depthAttachment.depthClearValue = 1.0f;
                depthAttachment.depthLoadOp = WGPULoadOp_Clear;
                depthAttachment.depthStoreOp = WGPUStoreOp_Store;
                depthAttachment.stencilLoadOp = WGPULoadOp_Undefined;
                depthAttachment.stencilStoreOp = WGPUStoreOp_Undefined;

                WGPURenderPassDescriptor passDesc{};
                passDesc.label = {"gbuf_pass", WGPU_STRLEN};
                passDesc.colorAttachmentCount = 2;
                passDesc.colorAttachments = colorAttachments;
                passDesc.depthStencilAttachment = &depthAttachment;

                WGPURenderPassEncoder pass =
                    wgpuCommandEncoderBeginRenderPass(frame.encoder, &passDesc);
                wgpuRenderPassEncoderSetPipeline(pass, m_gbufferPipeline);
                wgpuRenderPassEncoderSetBindGroup(pass, 0, m_sceneBindGroup, 0, nullptr);
                wgpuRenderPassEncoderSetVertexBuffer(pass,
                                                     0,
                                                     m_vertexBuffer,
                                                     0,
                                                     static_cast<uint64_t>(m_vertices.size()) *
                                                         sizeof(Vertex));
                wgpuRenderPassEncoderSetIndexBuffer(
                    pass,
                    m_indexBuffer,
                    WGPUIndexFormat_Uint32,
                    0,
                    static_cast<uint64_t>(m_indices.size()) * sizeof(uint32_t));
                wgpuRenderPassEncoderDrawIndexed(
                    pass, static_cast<uint32_t>(m_indices.size()), 1, 0, 0, 0);
                wgpuRenderPassEncoderEnd(pass);
                wgpuRenderPassEncoderRelease(pass);
            }

            {
                WGPUComputePassDescriptor passDesc{};
                passDesc.label = {"lights_pass", WGPU_STRLEN};
                WGPUComputePassEncoder pass =
                    wgpuCommandEncoderBeginComputePass(frame.encoder, &passDesc);
                wgpuComputePassEncoderSetPipeline(pass, m_lightsPipeline);
                wgpuComputePassEncoderSetBindGroup(pass, 0, m_computeBindGroup, 0, nullptr);
                wgpuComputePassEncoderDispatchWorkgroups(
                    pass, (kMaxNumLights + 63u) / 64u, 1, 1);
                wgpuComputePassEncoderEnd(pass);
                wgpuComputePassEncoderRelease(pass);
            }

            {
                WGPURenderPassColorAttachment colorAttachment{};
                colorAttachment.view = frame.surfaceView;
                colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
                colorAttachment.loadOp = WGPULoadOp_Clear;
                colorAttachment.storeOp = WGPUStoreOp_Store;
                colorAttachment.clearValue = {0.0, 0.0, 0.0, 1.0};

                WGPURenderPassDescriptor passDesc{};
                passDesc.label = {"deferred_pass", WGPU_STRLEN};
                passDesc.colorAttachmentCount = 1;
                passDesc.colorAttachments = &colorAttachment;

                WGPURenderPassEncoder pass =
                    wgpuCommandEncoderBeginRenderPass(frame.encoder, &passDesc);
                wgpuRenderPassEncoderSetPipeline(pass, m_deferredPipeline);
                wgpuRenderPassEncoderSetBindGroup(pass, 0, m_gbufferBindGroup, 0, nullptr);
                wgpuRenderPassEncoderSetBindGroup(pass, 1, m_lightsBindGroup, 0, nullptr);
                wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
                wgpuRenderPassEncoderEnd(pass);
                wgpuRenderPassEncoderRelease(pass);
            }

            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            ReleaseResizeTargets();

            if (m_lightsPipeline)
                wgpuComputePipelineRelease(m_lightsPipeline);
            if (m_deferredPipeline)
                wgpuRenderPipelineRelease(m_deferredPipeline);
            if (m_gbufferPipeline)
                wgpuRenderPipelineRelease(m_gbufferPipeline);
            if (m_computePipelineLayout)
                wgpuPipelineLayoutRelease(m_computePipelineLayout);
            if (m_deferredPipelineLayout)
                wgpuPipelineLayoutRelease(m_deferredPipelineLayout);
            if (m_gbufferPipelineLayout)
                wgpuPipelineLayoutRelease(m_gbufferPipelineLayout);
            if (m_computeBindGroup)
                wgpuBindGroupRelease(m_computeBindGroup);
            if (m_lightsBindGroup)
                wgpuBindGroupRelease(m_lightsBindGroup);
            if (m_sceneBindGroup)
                wgpuBindGroupRelease(m_sceneBindGroup);
            if (m_computeBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_computeBindGroupLayout);
            if (m_lightsBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_lightsBindGroupLayout);
            if (m_gbufferTexturesBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_gbufferTexturesBindGroupLayout);
            if (m_sceneBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_sceneBindGroupLayout);
            if (m_lightsComputeShader)
                wgpuShaderModuleRelease(m_lightsComputeShader);
            if (m_deferredPixelShader)
                wgpuShaderModuleRelease(m_deferredPixelShader);
            if (m_quadVertexShader)
                wgpuShaderModuleRelease(m_quadVertexShader);
            if (m_gbufferPixelShader)
                wgpuShaderModuleRelease(m_gbufferPixelShader);
            if (m_gbufferVertexShader)
                wgpuShaderModuleRelease(m_gbufferVertexShader);
            if (m_extentBuffer)
                wgpuBufferRelease(m_extentBuffer);
            if (m_configBuffer)
                wgpuBufferRelease(m_configBuffer);
            if (m_lightsBuffer)
                wgpuBufferRelease(m_lightsBuffer);
            if (m_cameraBuffer)
                wgpuBufferRelease(m_cameraBuffer);
            if (m_modelBuffer)
                wgpuBufferRelease(m_modelBuffer);
            if (m_indexBuffer)
                wgpuBufferRelease(m_indexBuffer);
            if (m_vertexBuffer)
                wgpuBufferRelease(m_vertexBuffer);
        }

    private:
        static WGPUTexture CreateRenderTexture(WGPUDevice device,
                                               uint32_t width,
                                               uint32_t height,
                                               WGPUTextureFormat format,
                                               WGPUTextureUsage usage,
                                               const char *label)
        {
            WGPUTextureDescriptor desc{};
            desc.label = {label, WGPU_STRLEN};
            desc.format = format;
            desc.dimension = WGPUTextureDimension_2D;
            desc.size = {width, height, 1};
            desc.mipLevelCount = 1;
            desc.sampleCount = 1;
            desc.usage = usage;
            return wgpuDeviceCreateTexture(device, &desc);
        }

        static WGPUTextureView CreateTextureView(WGPUTexture texture,
                                                 WGPUTextureFormat format,
                                                 WGPUTextureAspect aspect,
                                                 const char *label)
        {
            WGPUTextureViewDescriptor desc{};
            desc.label = {label, WGPU_STRLEN};
            desc.format = format;
            desc.dimension = WGPUTextureViewDimension_2D;
            desc.mipLevelCount = 1;
            desc.arrayLayerCount = 1;
            desc.aspect = aspect;
            return wgpuTextureCreateView(texture, &desc);
        }

        bool CreateBuffers(pwgpu::test::SampleContext &ctx)
        {
            m_vertexBuffer = pwgpu::test::CreateBuffer(
                ctx.device,
                static_cast<uint64_t>(m_vertices.size()) * sizeof(Vertex),
                WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
                "dragon_vb");
            m_indexBuffer = pwgpu::test::CreateBuffer(
                ctx.device,
                static_cast<uint64_t>(m_indices.size()) * sizeof(uint32_t),
                WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst,
                "dragon_ib");
            m_modelBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                      sizeof(ModelUniforms),
                                                      WGPUBufferUsage_Uniform |
                                                          WGPUBufferUsage_CopyDst,
                                                      "model_ub");
            m_cameraBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                       sizeof(CameraUniforms),
                                                       WGPUBufferUsage_Uniform |
                                                           WGPUBufferUsage_CopyDst,
                                                       "camera_ub");
            m_lightsBuffer = pwgpu::test::CreateBuffer(
                ctx.device,
                static_cast<uint64_t>(kMaxNumLights) * sizeof(LightData),
                WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
                "lights_sb");
            m_configBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                       sizeof(Config),
                                                       WGPUBufferUsage_Uniform |
                                                           WGPUBufferUsage_CopyDst,
                                                       "config_ub");
            m_extentBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                       sizeof(LightExtent),
                                                       WGPUBufferUsage_Uniform |
                                                           WGPUBufferUsage_CopyDst,
                                                       "extent_ub");

            if (!m_vertexBuffer || !m_indexBuffer || !m_modelBuffer || !m_cameraBuffer ||
                !m_lightsBuffer || !m_configBuffer || !m_extentBuffer)
            {
                return false;
            }

            wgpuQueueWriteBuffer(
                ctx.queue, m_vertexBuffer, 0, m_vertices.data(), m_vertices.size() * sizeof(Vertex));
            wgpuQueueWriteBuffer(
                ctx.queue, m_indexBuffer, 0, m_indices.data(), m_indices.size() * sizeof(uint32_t));

            ModelUniforms model{};
            mat4 modelMatrix = glm::translate(mat4(1.f), vec3(0.f, -45.f, 0.f));
            mat4 normalMatrix(1.f);
            memcpy(model.modelMatrix, glm::value_ptr(modelMatrix), 64);
            memcpy(model.normalModelMatrix, glm::value_ptr(normalMatrix), 64);
            wgpuQueueWriteBuffer(ctx.queue, m_modelBuffer, 0, &model, sizeof(model));

            std::mt19937 rng(0xC0FFEEu);
            std::uniform_real_distribution<float> dist01(0.f, 1.f);
            std::vector<LightData> lights(kMaxNumLights);
            for (LightData &light : lights)
            {
                for (int i = 0; i < 3; i++)
                {
                    light.pos[i] = dist01(rng) * (kLightExtentMax[i] - kLightExtentMin[i]) +
                                   kLightExtentMin[i];
                }
                light.pos[3] = 1.f;
                light.color[0] = dist01(rng) * 2.f;
                light.color[1] = dist01(rng) * 2.f;
                light.color[2] = dist01(rng) * 2.f;
                light.radius = 20.f;
            }
            wgpuQueueWriteBuffer(
                ctx.queue, m_lightsBuffer, 0, lights.data(), lights.size() * sizeof(LightData));

            Config config{static_cast<uint32_t>(kNumLights), 1.0f, {0, 0}};
            wgpuQueueWriteBuffer(ctx.queue, m_configBuffer, 0, &config, sizeof(config));

            LightExtent extent{{kLightExtentMin[0], kLightExtentMin[1], kLightExtentMin[2], 0.f},
                               {kLightExtentMax[0], kLightExtentMax[1], kLightExtentMax[2], 0.f}};
            wgpuQueueWriteBuffer(ctx.queue, m_extentBuffer, 0, &extent, sizeof(extent));
            return true;
        }

        bool CreatePipelineResources(pwgpu::test::SampleContext &ctx)
        {
            WGPUBindGroupLayoutEntry sceneEntries[2] = {};
            sceneEntries[0].binding = 0;
            sceneEntries[0].visibility = WGPUShaderStage_Vertex;
            sceneEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
            sceneEntries[0].buffer.minBindingSize = sizeof(ModelUniforms);
            sceneEntries[1].binding = 1;
            sceneEntries[1].visibility = WGPUShaderStage_Vertex;
            sceneEntries[1].buffer.type = WGPUBufferBindingType_Uniform;
            sceneEntries[1].buffer.minBindingSize = sizeof(CameraUniforms);

            WGPUBindGroupLayoutDescriptor sceneLayoutDesc{};
            sceneLayoutDesc.label = {"bgl_scene", WGPU_STRLEN};
            sceneLayoutDesc.entryCount = 2;
            sceneLayoutDesc.entries = sceneEntries;
            m_sceneBindGroupLayout =
                wgpuDeviceCreateBindGroupLayout(ctx.device, &sceneLayoutDesc);
            if (!m_sceneBindGroupLayout)
                return false;

            WGPUBindGroupLayoutEntry gbufferTextureEntries[3] = {};
            for (int i = 0; i < 3; i++)
            {
                gbufferTextureEntries[i].binding = static_cast<uint32_t>(i);
                gbufferTextureEntries[i].visibility = WGPUShaderStage_Fragment;
                gbufferTextureEntries[i].texture.sampleType =
                    WGPUTextureSampleType_UnfilterableFloat;
                gbufferTextureEntries[i].texture.viewDimension =
                    WGPUTextureViewDimension_2D;
            }

            WGPUBindGroupLayoutDescriptor gbufferTexturesLayoutDesc{};
            gbufferTexturesLayoutDesc.label = {"bgl_gbuf_tex", WGPU_STRLEN};
            gbufferTexturesLayoutDesc.entryCount = 3;
            gbufferTexturesLayoutDesc.entries = gbufferTextureEntries;
            m_gbufferTexturesBindGroupLayout =
                wgpuDeviceCreateBindGroupLayout(ctx.device, &gbufferTexturesLayoutDesc);
            if (!m_gbufferTexturesBindGroupLayout)
                return false;

            WGPUBindGroupLayoutEntry lightsEntries[3] = {};
            lightsEntries[0].binding = 0;
            lightsEntries[0].visibility =
                static_cast<WGPUShaderStage>(WGPUShaderStage_Fragment | WGPUShaderStage_Compute);
            lightsEntries[0].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
            lightsEntries[0].buffer.minBindingSize = sizeof(LightData);
            lightsEntries[1].binding = 1;
            lightsEntries[1].visibility =
                static_cast<WGPUShaderStage>(WGPUShaderStage_Fragment | WGPUShaderStage_Compute);
            lightsEntries[1].buffer.type = WGPUBufferBindingType_Uniform;
            lightsEntries[1].buffer.minBindingSize = sizeof(Config);
            lightsEntries[2].binding = 2;
            lightsEntries[2].visibility = WGPUShaderStage_Fragment;
            lightsEntries[2].buffer.type = WGPUBufferBindingType_Uniform;
            lightsEntries[2].buffer.minBindingSize = sizeof(CameraUniforms);

            WGPUBindGroupLayoutDescriptor lightsLayoutDesc{};
            lightsLayoutDesc.label = {"bgl_lights", WGPU_STRLEN};
            lightsLayoutDesc.entryCount = 3;
            lightsLayoutDesc.entries = lightsEntries;
            m_lightsBindGroupLayout =
                wgpuDeviceCreateBindGroupLayout(ctx.device, &lightsLayoutDesc);
            if (!m_lightsBindGroupLayout)
                return false;

            WGPUBindGroupLayoutEntry computeEntries[3] = {};
            computeEntries[0].binding = 0;
            computeEntries[0].visibility = WGPUShaderStage_Compute;
            computeEntries[0].buffer.type = WGPUBufferBindingType_Storage;
            computeEntries[0].buffer.minBindingSize = sizeof(LightData);
            computeEntries[1].binding = 1;
            computeEntries[1].visibility = WGPUShaderStage_Compute;
            computeEntries[1].buffer.type = WGPUBufferBindingType_Uniform;
            computeEntries[1].buffer.minBindingSize = sizeof(Config);
            computeEntries[2].binding = 2;
            computeEntries[2].visibility = WGPUShaderStage_Compute;
            computeEntries[2].buffer.type = WGPUBufferBindingType_Uniform;
            computeEntries[2].buffer.minBindingSize = sizeof(LightExtent);

            WGPUBindGroupLayoutDescriptor computeLayoutDesc{};
            computeLayoutDesc.label = {"bgl_compute", WGPU_STRLEN};
            computeLayoutDesc.entryCount = 3;
            computeLayoutDesc.entries = computeEntries;
            m_computeBindGroupLayout =
                wgpuDeviceCreateBindGroupLayout(ctx.device, &computeLayoutDesc);
            if (!m_computeBindGroupLayout)
                return false;

            m_gbufferPipelineLayout =
                CreatePipelineLayout(ctx.device, &m_sceneBindGroupLayout, 1, "pl_gbuf");
            WGPUBindGroupLayout deferredLayouts[2] = {m_gbufferTexturesBindGroupLayout,
                                                      m_lightsBindGroupLayout};
            m_deferredPipelineLayout =
                CreatePipelineLayout(ctx.device, deferredLayouts, 2, "pl_def");
            m_computePipelineLayout =
                CreatePipelineLayout(ctx.device, &m_computeBindGroupLayout, 1, "pl_comp");
            if (!m_gbufferPipelineLayout || !m_deferredPipelineLayout || !m_computePipelineLayout)
                return false;

            if (!CreateStaticBindGroups(ctx))
                return false;

            return CreatePipelines(ctx);
        }

        static WGPUPipelineLayout CreatePipelineLayout(WGPUDevice device,
                                                       WGPUBindGroupLayout *layouts,
                                                       uint32_t count,
                                                       const char *label)
        {
            WGPUPipelineLayoutDescriptor desc{};
            desc.label = {label, WGPU_STRLEN};
            desc.bindGroupLayoutCount = count;
            desc.bindGroupLayouts = layouts;
            return wgpuDeviceCreatePipelineLayout(device, &desc);
        }

        bool CreateStaticBindGroups(pwgpu::test::SampleContext &ctx)
        {
            WGPUBindGroupEntry sceneEntries[2] = {};
            sceneEntries[0].binding = 0;
            sceneEntries[0].buffer = m_modelBuffer;
            sceneEntries[0].size = sizeof(ModelUniforms);
            sceneEntries[1].binding = 1;
            sceneEntries[1].buffer = m_cameraBuffer;
            sceneEntries[1].size = sizeof(CameraUniforms);

            WGPUBindGroupDescriptor sceneDesc{};
            sceneDesc.label = {"bg_scene", WGPU_STRLEN};
            sceneDesc.layout = m_sceneBindGroupLayout;
            sceneDesc.entryCount = 2;
            sceneDesc.entries = sceneEntries;
            m_sceneBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &sceneDesc);
            if (!m_sceneBindGroup)
                return false;

            WGPUBindGroupEntry lightsEntries[3] = {};
            lightsEntries[0].binding = 0;
            lightsEntries[0].buffer = m_lightsBuffer;
            lightsEntries[0].size = static_cast<uint64_t>(kMaxNumLights) * sizeof(LightData);
            lightsEntries[1].binding = 1;
            lightsEntries[1].buffer = m_configBuffer;
            lightsEntries[1].size = sizeof(Config);
            lightsEntries[2].binding = 2;
            lightsEntries[2].buffer = m_cameraBuffer;
            lightsEntries[2].size = sizeof(CameraUniforms);

            WGPUBindGroupDescriptor lightsDesc{};
            lightsDesc.label = {"bg_lights", WGPU_STRLEN};
            lightsDesc.layout = m_lightsBindGroupLayout;
            lightsDesc.entryCount = 3;
            lightsDesc.entries = lightsEntries;
            m_lightsBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &lightsDesc);
            if (!m_lightsBindGroup)
                return false;

            WGPUBindGroupEntry computeEntries[3] = {};
            computeEntries[0].binding = 0;
            computeEntries[0].buffer = m_lightsBuffer;
            computeEntries[0].size = static_cast<uint64_t>(kMaxNumLights) * sizeof(LightData);
            computeEntries[1].binding = 1;
            computeEntries[1].buffer = m_configBuffer;
            computeEntries[1].size = sizeof(Config);
            computeEntries[2].binding = 2;
            computeEntries[2].buffer = m_extentBuffer;
            computeEntries[2].size = sizeof(LightExtent);

            WGPUBindGroupDescriptor computeDesc{};
            computeDesc.label = {"bg_compute", WGPU_STRLEN};
            computeDesc.layout = m_computeBindGroupLayout;
            computeDesc.entryCount = 3;
            computeDesc.entries = computeEntries;
            m_computeBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &computeDesc);
            return m_computeBindGroup != nullptr;
        }

        bool CreatePipelines(pwgpu::test::SampleContext &ctx)
        {
            WGPUVertexAttribute vertexAttributes[3] = {};
            vertexAttributes[0].format = WGPUVertexFormat_Float32x3;
            vertexAttributes[0].offset = offsetof(Vertex, pos);
            vertexAttributes[0].shaderLocation = 0;
            vertexAttributes[1].format = WGPUVertexFormat_Float32x3;
            vertexAttributes[1].offset = offsetof(Vertex, normal);
            vertexAttributes[1].shaderLocation = 1;
            vertexAttributes[2].format = WGPUVertexFormat_Float32x2;
            vertexAttributes[2].offset = offsetof(Vertex, uv);
            vertexAttributes[2].shaderLocation = 2;

            WGPUVertexBufferLayout vertexLayout{};
            vertexLayout.arrayStride = sizeof(Vertex);
            vertexLayout.stepMode = WGPUVertexStepMode_Vertex;
            vertexLayout.attributeCount = 3;
            vertexLayout.attributes = vertexAttributes;

            WGPUColorTargetState gbufferTargets[2] = {};
            gbufferTargets[0].format = WGPUTextureFormat_RGBA16Float;
            gbufferTargets[0].writeMask = WGPUColorWriteMask_All;
            gbufferTargets[1].format = WGPUTextureFormat_RGBA8Unorm;
            gbufferTargets[1].writeMask = WGPUColorWriteMask_All;

            WGPUFragmentState gbufferFragment{};
            gbufferFragment.module = m_gbufferPixelShader;
            gbufferFragment.entryPoint = {"PSMain", WGPU_STRLEN};
            gbufferFragment.targetCount = 2;
            gbufferFragment.targets = gbufferTargets;

            WGPUDepthStencilState gbufferDepth{};
            gbufferDepth.format = WGPUTextureFormat_Depth24Plus;
            gbufferDepth.depthWriteEnabled = WGPUOptionalBool_True;
            gbufferDepth.depthCompare = WGPUCompareFunction_Less;
            gbufferDepth.stencilFront.compare = WGPUCompareFunction_Always;
            gbufferDepth.stencilBack.compare = WGPUCompareFunction_Always;

            WGPURenderPipelineDescriptor gbufferDesc{};
            gbufferDesc.label = {"pipeline_gbuf", WGPU_STRLEN};
            gbufferDesc.layout = m_gbufferPipelineLayout;
            gbufferDesc.vertex.module = m_gbufferVertexShader;
            gbufferDesc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            gbufferDesc.vertex.bufferCount = 1;
            gbufferDesc.vertex.buffers = &vertexLayout;
            gbufferDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            gbufferDesc.primitive.frontFace = WGPUFrontFace_CCW;
            gbufferDesc.primitive.cullMode = WGPUCullMode_Back;
            gbufferDesc.multisample.count = 1;
            gbufferDesc.multisample.mask = 0xFFFFFFFF;
            gbufferDesc.depthStencil = &gbufferDepth;
            gbufferDesc.fragment = &gbufferFragment;
            m_gbufferPipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &gbufferDesc);
            if (!m_gbufferPipeline)
                return false;

            WGPUColorTargetState deferredTarget{};
            deferredTarget.format = ctx.surfaceFormat;
            deferredTarget.writeMask = WGPUColorWriteMask_All;

            WGPUFragmentState deferredFragment{};
            deferredFragment.module = m_deferredPixelShader;
            deferredFragment.entryPoint = {"PSMain", WGPU_STRLEN};
            deferredFragment.targetCount = 1;
            deferredFragment.targets = &deferredTarget;

            WGPURenderPipelineDescriptor deferredDesc{};
            deferredDesc.label = {"pipeline_deferred", WGPU_STRLEN};
            deferredDesc.layout = m_deferredPipelineLayout;
            deferredDesc.vertex.module = m_quadVertexShader;
            deferredDesc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            deferredDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            deferredDesc.primitive.frontFace = WGPUFrontFace_CCW;
            deferredDesc.primitive.cullMode = WGPUCullMode_None;
            deferredDesc.multisample.count = 1;
            deferredDesc.multisample.mask = 0xFFFFFFFF;
            deferredDesc.fragment = &deferredFragment;
            m_deferredPipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &deferredDesc);
            if (!m_deferredPipeline)
                return false;

            WGPUComputePipelineDescriptor computeDesc{};
            computeDesc.label = {"pipeline_lights", WGPU_STRLEN};
            computeDesc.layout = m_computePipelineLayout;
            computeDesc.compute.module = m_lightsComputeShader;
            computeDesc.compute.entryPoint = {"CSMain", WGPU_STRLEN};
            m_lightsPipeline = wgpuDeviceCreateComputePipeline(ctx.device, &computeDesc);
            return m_lightsPipeline != nullptr;
        }

        void RebuildGBufferBindGroup(WGPUDevice device)
        {
            if (m_gbufferBindGroup)
            {
                wgpuBindGroupRelease(m_gbufferBindGroup);
                m_gbufferBindGroup = nullptr;
            }

            if (!m_gbufferTexturesBindGroupLayout || !m_gbufferNormalView ||
                !m_gbufferAlbedoView || !m_depthViewRead)
            {
                return;
            }

            WGPUBindGroupEntry entries[3] = {};
            entries[0].binding = 0;
            entries[0].textureView = m_gbufferNormalView;
            entries[1].binding = 1;
            entries[1].textureView = m_gbufferAlbedoView;
            entries[2].binding = 2;
            entries[2].textureView = m_depthViewRead;

            WGPUBindGroupDescriptor desc{};
            desc.label = {"bg_gbuf_tex", WGPU_STRLEN};
            desc.layout = m_gbufferTexturesBindGroupLayout;
            desc.entryCount = 3;
            desc.entries = entries;
            m_gbufferBindGroup = wgpuDeviceCreateBindGroup(device, &desc);
        }

        void ReleaseResizeTargets()
        {
            if (m_gbufferBindGroup)
            {
                wgpuBindGroupRelease(m_gbufferBindGroup);
                m_gbufferBindGroup = nullptr;
            }
            if (m_depthViewRead)
            {
                wgpuTextureViewRelease(m_depthViewRead);
                m_depthViewRead = nullptr;
            }
            if (m_depthViewRender)
            {
                wgpuTextureViewRelease(m_depthViewRender);
                m_depthViewRender = nullptr;
            }
            if (m_gbufferAlbedoView)
            {
                wgpuTextureViewRelease(m_gbufferAlbedoView);
                m_gbufferAlbedoView = nullptr;
            }
            if (m_gbufferNormalView)
            {
                wgpuTextureViewRelease(m_gbufferNormalView);
                m_gbufferNormalView = nullptr;
            }
            if (m_depthTexture)
            {
                wgpuTextureRelease(m_depthTexture);
                m_depthTexture = nullptr;
            }
            if (m_gbufferAlbedoTexture)
            {
                wgpuTextureRelease(m_gbufferAlbedoTexture);
                m_gbufferAlbedoTexture = nullptr;
            }
            if (m_gbufferNormalTexture)
            {
                wgpuTextureRelease(m_gbufferNormalTexture);
                m_gbufferNormalTexture = nullptr;
            }
        }

        std::vector<Vertex> m_vertices{};
        std::vector<uint32_t> m_indices{};
        WGPUShaderModule m_gbufferVertexShader = nullptr;
        WGPUShaderModule m_gbufferPixelShader = nullptr;
        WGPUShaderModule m_quadVertexShader = nullptr;
        WGPUShaderModule m_deferredPixelShader = nullptr;
        WGPUShaderModule m_lightsComputeShader = nullptr;
        WGPUTexture m_gbufferNormalTexture = nullptr;
        WGPUTexture m_gbufferAlbedoTexture = nullptr;
        WGPUTexture m_depthTexture = nullptr;
        WGPUTextureView m_gbufferNormalView = nullptr;
        WGPUTextureView m_gbufferAlbedoView = nullptr;
        WGPUTextureView m_depthViewRender = nullptr;
        WGPUTextureView m_depthViewRead = nullptr;
        WGPUBuffer m_vertexBuffer = nullptr;
        WGPUBuffer m_indexBuffer = nullptr;
        WGPUBuffer m_modelBuffer = nullptr;
        WGPUBuffer m_cameraBuffer = nullptr;
        WGPUBuffer m_lightsBuffer = nullptr;
        WGPUBuffer m_configBuffer = nullptr;
        WGPUBuffer m_extentBuffer = nullptr;
        WGPUBindGroupLayout m_sceneBindGroupLayout = nullptr;
        WGPUBindGroupLayout m_gbufferTexturesBindGroupLayout = nullptr;
        WGPUBindGroupLayout m_lightsBindGroupLayout = nullptr;
        WGPUBindGroupLayout m_computeBindGroupLayout = nullptr;
        WGPUPipelineLayout m_gbufferPipelineLayout = nullptr;
        WGPUPipelineLayout m_deferredPipelineLayout = nullptr;
        WGPUPipelineLayout m_computePipelineLayout = nullptr;
        WGPUBindGroup m_sceneBindGroup = nullptr;
        WGPUBindGroup m_gbufferBindGroup = nullptr;
        WGPUBindGroup m_lightsBindGroup = nullptr;
        WGPUBindGroup m_computeBindGroup = nullptr;
        WGPURenderPipeline m_gbufferPipeline = nullptr;
        WGPURenderPipeline m_deferredPipeline = nullptr;
        WGPUComputePipeline m_lightsPipeline = nullptr;
    };

    pwgpu::test::SampleAppDesc MakeDeferredDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "DeferredTest";
        desc.windowTitle = "PhasmaWebGPU Deferred Rendering";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    DeferredRenderingSample sample;
    pwgpu::test::SampleApp app(sample, MakeDeferredDesc());
    return app.Run(argc, argv);
}
