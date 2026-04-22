#include "../Common/SampleApp.h"
#include "../Common/SampleBase.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    constexpr float kPi = 3.14159265f;
    constexpr uint32_t kShadowSize = 1024;
    constexpr uint64_t kSceneBufferSize = 144;
    constexpr uint64_t kModelBufferSize = 64;

    struct DragonMesh
    {
        std::vector<float> vertexData;
        std::vector<uint16_t> indices;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
    };

    bool LoadDragon(const std::string &path, DragonMesh &mesh)
    {
        FILE *file = std::fopen(path.c_str(), "rb");
        if (!file)
        {
            fprintf(stderr, "[Dragon] cannot open %s\n", path.c_str());
            return false;
        }

        uint32_t header[2]{};
        if (std::fread(header, sizeof(uint32_t), 2, file) != 2)
        {
            std::fclose(file);
            return false;
        }

        mesh.vertexCount = header[0];
        mesh.indexCount = header[1];
        mesh.vertexData.resize(static_cast<size_t>(mesh.vertexCount) * 6u);
        mesh.indices.resize(mesh.indexCount);

        if (std::fread(mesh.vertexData.data(), sizeof(float), mesh.vertexData.size(), file) !=
            mesh.vertexData.size())
        {
            std::fclose(file);
            return false;
        }

        if (std::fread(mesh.indices.data(), sizeof(uint16_t), mesh.indices.size(), file) !=
            mesh.indices.size())
        {
            std::fclose(file);
            return false;
        }

        std::fclose(file);
        return true;
    }

    class ShadowMappingSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            const std::string dragonPath = pwgpu::test::GetAssetPath(ctx.exeDir, "stanfordDragon.bin");
            if (!LoadDragon(dragonPath, m_mesh))
            {
                fprintf(stderr, "[Dragon] failed to load mesh asset %s\n", dragonPath.c_str());
                return false;
            }

            fprintf(stdout,
                    "[Dragon] %u vertices, %u indices\n",
                    m_mesh.vertexCount,
                    m_mesh.indexCount);

            m_shadowVertexShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "shadow.vert.hlsl").c_str(), "shadow_vs");
            m_litVertexShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "lit.vert.hlsl").c_str(), "lit_vs");
            m_litPixelShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "lit.pixel.hlsl").c_str(), "lit_ps");
            if (!m_shadowVertexShader || !m_litVertexShader || !m_litPixelShader)
                return false;

            if (!CreateBuffers(ctx) || !CreateShadowResources(ctx) || !CreatePipelineResources(ctx))
                return false;

            fprintf(stdout, "[Pipelines] shadow + main created\n");
            return true;
        }

        void Resize(pwgpu::test::SampleContext &ctx, uint32_t width, uint32_t height) override
        {
            ReleaseMainDepthTarget();
            if (width == 0 || height == 0)
                return;

            WGPUTextureDescriptor depthDesc{};
            depthDesc.label = {"main_depth", WGPU_STRLEN};
            depthDesc.dimension = WGPUTextureDimension_2D;
            depthDesc.size = {width, height, 1};
            depthDesc.mipLevelCount = 1;
            depthDesc.sampleCount = 1;
            depthDesc.format = WGPUTextureFormat_Depth24Plus;
            depthDesc.usage = WGPUTextureUsage_RenderAttachment;
            m_mainDepthTexture = wgpuDeviceCreateTexture(ctx.device, &depthDesc);
            if (!m_mainDepthTexture)
                return;

            WGPUTextureViewDescriptor viewDesc{};
            viewDesc.format = WGPUTextureFormat_Depth24Plus;
            viewDesc.dimension = WGPUTextureViewDimension_2D;
            viewDesc.mipLevelCount = 1;
            viewDesc.arrayLayerCount = 1;
            viewDesc.aspect = WGPUTextureAspect_All;
            m_mainDepthView = wgpuTextureCreateView(m_mainDepthTexture, &viewDesc);
        }

        void Update(pwgpu::test::SampleContext &ctx) override
        {
            vec3 lightPosition(50.f, 100.f, -100.f);
            mat4 lightProjection =
                glm::orthoRH_ZO(-80.f, 80.f, -80.f, 80.f, -200.f, 300.f);
            mat4 lightView =
                glm::lookAtRH(lightPosition, vec3(0.f, 0.f, 0.f), vec3(0.f, 1.f, 0.f));
            mat4 lightViewProjection = lightProjection * lightView;

            const float aspect =
                ctx.height > 0 ? static_cast<float>(ctx.width) / static_cast<float>(ctx.height)
                               : 1.0f;
            mat4 cameraProjection =
                glm::perspectiveRH_ZO(2.f * kPi / 5.f, aspect, 1.f, 2000.f);
            cameraProjection[1][1] *= -1.f;

            const float radians = kPi * static_cast<float>(ctx.timing.elapsedSeconds) * 0.5f;
            vec3 eyeBase(0.f, 50.f, -100.f);
            vec3 eye(std::cos(radians) * eyeBase.x + std::sin(radians) * eyeBase.z,
                     eyeBase.y,
                     -std::sin(radians) * eyeBase.x + std::cos(radians) * eyeBase.z);
            mat4 cameraView =
                glm::lookAtRH(eye, vec3(0.f, 0.f, 0.f), vec3(0.f, 1.f, 0.f));
            mat4 cameraViewProjection = cameraProjection * cameraView;

            uint8_t sceneBytes[kSceneBufferSize]{};
            std::memcpy(sceneBytes + 0, glm::value_ptr(lightViewProjection), 64);
            std::memcpy(sceneBytes + 64, glm::value_ptr(cameraViewProjection), 64);
            std::memcpy(sceneBytes + 128, glm::value_ptr(lightPosition), 12);
            wgpuQueueWriteBuffer(ctx.queue, m_sceneBuffer, 0, sceneBytes, kSceneBufferSize);

            mat4 modelMatrix = glm::translate(mat4(1.f), vec3(0.f, -45.f, 0.f));
            wgpuQueueWriteBuffer(ctx.queue,
                                 m_modelBuffer,
                                 0,
                                 glm::value_ptr(modelMatrix),
                                 kModelBufferSize);
        }

        bool Execute(pwgpu::test::SampleContext &,
                     pwgpu::test::SampleFrame &frame) override
        {
            if (!m_shadowPipeline || !m_mainPipeline || !m_shadowView || !m_mainDepthView)
                return true;

            {
                WGPURenderPassDepthStencilAttachment depthAttachment{};
                depthAttachment.view = m_shadowView;
                depthAttachment.depthClearValue = 1.0f;
                depthAttachment.depthLoadOp = WGPULoadOp_Clear;
                depthAttachment.depthStoreOp = WGPUStoreOp_Store;
                depthAttachment.depthReadOnly = WGPUOptionalBool_False;
                depthAttachment.stencilLoadOp = WGPULoadOp_Undefined;
                depthAttachment.stencilStoreOp = WGPUStoreOp_Undefined;
                depthAttachment.stencilReadOnly = WGPUOptionalBool_True;

                WGPURenderPassDescriptor passDesc{};
                passDesc.label = {"shadow_pass", WGPU_STRLEN};
                passDesc.colorAttachmentCount = 0;
                passDesc.colorAttachments = nullptr;
                passDesc.depthStencilAttachment = &depthAttachment;

                WGPURenderPassEncoder pass =
                    wgpuCommandEncoderBeginRenderPass(frame.encoder, &passDesc);
                wgpuRenderPassEncoderSetPipeline(pass, m_shadowPipeline);
                wgpuRenderPassEncoderSetBindGroup(pass, 0, m_shadowSceneBindGroup, 0, nullptr);
                wgpuRenderPassEncoderSetBindGroup(pass, 1, m_modelBindGroup, 0, nullptr);
                wgpuRenderPassEncoderSetVertexBuffer(pass,
                                                     0,
                                                     m_vertexBuffer,
                                                     0,
                                                     static_cast<uint64_t>(m_mesh.vertexData.size()) *
                                                         sizeof(float));
                wgpuRenderPassEncoderSetIndexBuffer(pass,
                                                    m_indexBuffer,
                                                    WGPUIndexFormat_Uint16,
                                                    0,
                                                    m_indexBufferSize);
                wgpuRenderPassEncoderDrawIndexed(pass, m_mesh.indexCount, 1, 0, 0, 0);
                wgpuRenderPassEncoderEnd(pass);
                wgpuRenderPassEncoderRelease(pass);
            }

            {
                WGPURenderPassColorAttachment colorAttachment{};
                colorAttachment.view = frame.surfaceView;
                colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
                colorAttachment.loadOp = WGPULoadOp_Clear;
                colorAttachment.storeOp = WGPUStoreOp_Store;
                colorAttachment.clearValue = {0.5, 0.5, 0.5, 1.0};

                WGPURenderPassDepthStencilAttachment depthAttachment{};
                depthAttachment.view = m_mainDepthView;
                depthAttachment.depthClearValue = 1.0f;
                depthAttachment.depthLoadOp = WGPULoadOp_Clear;
                depthAttachment.depthStoreOp = WGPUStoreOp_Store;
                depthAttachment.depthReadOnly = WGPUOptionalBool_False;
                depthAttachment.stencilLoadOp = WGPULoadOp_Undefined;
                depthAttachment.stencilStoreOp = WGPUStoreOp_Undefined;
                depthAttachment.stencilReadOnly = WGPUOptionalBool_True;

                WGPURenderPassDescriptor passDesc{};
                passDesc.label = {"main_pass", WGPU_STRLEN};
                passDesc.colorAttachmentCount = 1;
                passDesc.colorAttachments = &colorAttachment;
                passDesc.depthStencilAttachment = &depthAttachment;

                WGPURenderPassEncoder pass =
                    wgpuCommandEncoderBeginRenderPass(frame.encoder, &passDesc);
                wgpuRenderPassEncoderSetPipeline(pass, m_mainPipeline);
                wgpuRenderPassEncoderSetBindGroup(pass, 0, m_mainSceneBindGroup, 0, nullptr);
                wgpuRenderPassEncoderSetBindGroup(pass, 1, m_modelBindGroup, 0, nullptr);
                wgpuRenderPassEncoderSetVertexBuffer(pass,
                                                     0,
                                                     m_vertexBuffer,
                                                     0,
                                                     static_cast<uint64_t>(m_mesh.vertexData.size()) *
                                                         sizeof(float));
                wgpuRenderPassEncoderSetIndexBuffer(pass,
                                                    m_indexBuffer,
                                                    WGPUIndexFormat_Uint16,
                                                    0,
                                                    m_indexBufferSize);
                wgpuRenderPassEncoderDrawIndexed(pass, m_mesh.indexCount, 1, 0, 0, 0);
                wgpuRenderPassEncoderEnd(pass);
                wgpuRenderPassEncoderRelease(pass);
            }

            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            ReleaseMainDepthTarget();

            if (m_mainPipeline)
                wgpuRenderPipelineRelease(m_mainPipeline);
            if (m_shadowPipeline)
                wgpuRenderPipelineRelease(m_shadowPipeline);
            if (m_mainPipelineLayout)
                wgpuPipelineLayoutRelease(m_mainPipelineLayout);
            if (m_shadowPipelineLayout)
                wgpuPipelineLayoutRelease(m_shadowPipelineLayout);
            if (m_modelBindGroup)
                wgpuBindGroupRelease(m_modelBindGroup);
            if (m_mainSceneBindGroup)
                wgpuBindGroupRelease(m_mainSceneBindGroup);
            if (m_shadowSceneBindGroup)
                wgpuBindGroupRelease(m_shadowSceneBindGroup);
            if (m_modelBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_modelBindGroupLayout);
            if (m_mainSceneBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_mainSceneBindGroupLayout);
            if (m_shadowSceneBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_shadowSceneBindGroupLayout);
            if (m_shadowSampler)
                wgpuSamplerRelease(m_shadowSampler);
            if (m_shadowView)
                wgpuTextureViewRelease(m_shadowView);
            if (m_shadowTexture)
                wgpuTextureRelease(m_shadowTexture);
            if (m_litPixelShader)
                wgpuShaderModuleRelease(m_litPixelShader);
            if (m_litVertexShader)
                wgpuShaderModuleRelease(m_litVertexShader);
            if (m_shadowVertexShader)
                wgpuShaderModuleRelease(m_shadowVertexShader);
            if (m_modelBuffer)
                wgpuBufferRelease(m_modelBuffer);
            if (m_sceneBuffer)
                wgpuBufferRelease(m_sceneBuffer);
            if (m_indexBuffer)
                wgpuBufferRelease(m_indexBuffer);
            if (m_vertexBuffer)
                wgpuBufferRelease(m_vertexBuffer);
        }

    private:
        bool CreateBuffers(pwgpu::test::SampleContext &ctx)
        {
            m_vertexBuffer = pwgpu::test::CreateBuffer(
                ctx.device,
                static_cast<uint64_t>(m_mesh.vertexData.size()) * sizeof(float),
                WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
                "scene_vb");
            if (!m_vertexBuffer)
                return false;
            wgpuQueueWriteBuffer(ctx.queue,
                                 m_vertexBuffer,
                                 0,
                                 m_mesh.vertexData.data(),
                                 m_mesh.vertexData.size() * sizeof(float));

            const uint64_t indexBytes =
                static_cast<uint64_t>(m_mesh.indices.size()) * sizeof(uint16_t);
            m_indexBufferSize = (indexBytes + 3u) & ~uint64_t(3);
            m_indexBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                      m_indexBufferSize,
                                                      WGPUBufferUsage_Index |
                                                          WGPUBufferUsage_CopyDst,
                                                      "scene_ib");
            if (!m_indexBuffer)
                return false;

            std::vector<uint8_t> indexBlob(static_cast<size_t>(m_indexBufferSize), 0);
            std::memcpy(indexBlob.data(), m_mesh.indices.data(), static_cast<size_t>(indexBytes));
            wgpuQueueWriteBuffer(
                ctx.queue, m_indexBuffer, 0, indexBlob.data(), m_indexBufferSize);

            m_sceneBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                      kSceneBufferSize,
                                                      WGPUBufferUsage_Uniform |
                                                          WGPUBufferUsage_CopyDst,
                                                      "scene_ub");
            m_modelBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                      kModelBufferSize,
                                                      WGPUBufferUsage_Uniform |
                                                          WGPUBufferUsage_CopyDst,
                                                      "model_ub");
            return m_sceneBuffer && m_modelBuffer;
        }

        bool CreateShadowResources(pwgpu::test::SampleContext &ctx)
        {
            WGPUTextureDescriptor shadowDesc{};
            shadowDesc.label = {"shadow_map", WGPU_STRLEN};
            shadowDesc.dimension = WGPUTextureDimension_2D;
            shadowDesc.size = {kShadowSize, kShadowSize, 1};
            shadowDesc.mipLevelCount = 1;
            shadowDesc.sampleCount = 1;
            shadowDesc.format = WGPUTextureFormat_Depth32Float;
            shadowDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
            m_shadowTexture = wgpuDeviceCreateTexture(ctx.device, &shadowDesc);
            if (!m_shadowTexture)
                return false;

            WGPUTextureViewDescriptor shadowViewDesc{};
            shadowViewDesc.format = WGPUTextureFormat_Depth32Float;
            shadowViewDesc.dimension = WGPUTextureViewDimension_2D;
            shadowViewDesc.mipLevelCount = 1;
            shadowViewDesc.arrayLayerCount = 1;
            shadowViewDesc.aspect = WGPUTextureAspect_All;
            m_shadowView = wgpuTextureCreateView(m_shadowTexture, &shadowViewDesc);
            if (!m_shadowView)
                return false;

            WGPUSamplerDescriptor samplerDesc{};
            samplerDesc.label = {"shadow_samp", WGPU_STRLEN};
            samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
            samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
            samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
            samplerDesc.magFilter = WGPUFilterMode_Linear;
            samplerDesc.minFilter = WGPUFilterMode_Linear;
            samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
            samplerDesc.compare = WGPUCompareFunction_Less;
            samplerDesc.maxAnisotropy = 1;
            m_shadowSampler = wgpuDeviceCreateSampler(ctx.device, &samplerDesc);
            return m_shadowSampler != nullptr;
        }

        bool CreatePipelineResources(pwgpu::test::SampleContext &ctx)
        {
            if (!CreateLayouts(ctx) || !CreateBindGroups(ctx))
                return false;
            return CreatePipelines(ctx);
        }

        bool CreateLayouts(pwgpu::test::SampleContext &ctx)
        {
            WGPUBindGroupLayoutEntry shadowSceneEntries[1] = {};
            shadowSceneEntries[0].binding = 0;
            shadowSceneEntries[0].visibility = WGPUShaderStage_Vertex;
            shadowSceneEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
            shadowSceneEntries[0].buffer.minBindingSize = kSceneBufferSize;

            WGPUBindGroupLayoutDescriptor shadowSceneDesc{};
            shadowSceneDesc.label = {"shadow_g0_bgl", WGPU_STRLEN};
            shadowSceneDesc.entryCount = 1;
            shadowSceneDesc.entries = shadowSceneEntries;
            m_shadowSceneBindGroupLayout =
                wgpuDeviceCreateBindGroupLayout(ctx.device, &shadowSceneDesc);
            if (!m_shadowSceneBindGroupLayout)
                return false;

            WGPUBindGroupLayoutEntry mainSceneEntries[3] = {};
            mainSceneEntries[0].binding = 0;
            mainSceneEntries[0].visibility =
                WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
            mainSceneEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
            mainSceneEntries[0].buffer.minBindingSize = kSceneBufferSize;
            mainSceneEntries[1].binding = 1;
            mainSceneEntries[1].visibility = WGPUShaderStage_Fragment;
            mainSceneEntries[1].texture.sampleType = WGPUTextureSampleType_Depth;
            mainSceneEntries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
            mainSceneEntries[2].binding = 2;
            mainSceneEntries[2].visibility = WGPUShaderStage_Fragment;
            mainSceneEntries[2].sampler.type = WGPUSamplerBindingType_Comparison;

            WGPUBindGroupLayoutDescriptor mainSceneDesc{};
            mainSceneDesc.label = {"main_g0_bgl", WGPU_STRLEN};
            mainSceneDesc.entryCount = 3;
            mainSceneDesc.entries = mainSceneEntries;
            m_mainSceneBindGroupLayout =
                wgpuDeviceCreateBindGroupLayout(ctx.device, &mainSceneDesc);
            if (!m_mainSceneBindGroupLayout)
                return false;

            WGPUBindGroupLayoutEntry modelEntries[1] = {};
            modelEntries[0].binding = 0;
            modelEntries[0].visibility = WGPUShaderStage_Vertex;
            modelEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
            modelEntries[0].buffer.minBindingSize = kModelBufferSize;

            WGPUBindGroupLayoutDescriptor modelDesc{};
            modelDesc.label = {"g1_bgl", WGPU_STRLEN};
            modelDesc.entryCount = 1;
            modelDesc.entries = modelEntries;
            m_modelBindGroupLayout =
                wgpuDeviceCreateBindGroupLayout(ctx.device, &modelDesc);
            if (!m_modelBindGroupLayout)
                return false;

            WGPUBindGroupLayout shadowLayouts[2] = {m_shadowSceneBindGroupLayout,
                                                    m_modelBindGroupLayout};
            WGPUPipelineLayoutDescriptor shadowPipelineDesc{};
            shadowPipelineDesc.label = {"shadow_pl", WGPU_STRLEN};
            shadowPipelineDesc.bindGroupLayoutCount = 2;
            shadowPipelineDesc.bindGroupLayouts = shadowLayouts;
            m_shadowPipelineLayout =
                wgpuDeviceCreatePipelineLayout(ctx.device, &shadowPipelineDesc);
            if (!m_shadowPipelineLayout)
                return false;

            WGPUBindGroupLayout mainLayouts[2] = {m_mainSceneBindGroupLayout,
                                                  m_modelBindGroupLayout};
            WGPUPipelineLayoutDescriptor mainPipelineDesc{};
            mainPipelineDesc.label = {"main_pl", WGPU_STRLEN};
            mainPipelineDesc.bindGroupLayoutCount = 2;
            mainPipelineDesc.bindGroupLayouts = mainLayouts;
            m_mainPipelineLayout =
                wgpuDeviceCreatePipelineLayout(ctx.device, &mainPipelineDesc);
            return m_mainPipelineLayout != nullptr;
        }

        bool CreateBindGroups(pwgpu::test::SampleContext &ctx)
        {
            WGPUBindGroupEntry shadowSceneEntries[1] = {};
            shadowSceneEntries[0].binding = 0;
            shadowSceneEntries[0].buffer = m_sceneBuffer;
            shadowSceneEntries[0].size = kSceneBufferSize;

            WGPUBindGroupDescriptor shadowSceneDesc{};
            shadowSceneDesc.label = {"shadow_g0_bg", WGPU_STRLEN};
            shadowSceneDesc.layout = m_shadowSceneBindGroupLayout;
            shadowSceneDesc.entryCount = 1;
            shadowSceneDesc.entries = shadowSceneEntries;
            m_shadowSceneBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &shadowSceneDesc);
            if (!m_shadowSceneBindGroup)
                return false;

            WGPUBindGroupEntry mainSceneEntries[3] = {};
            mainSceneEntries[0].binding = 0;
            mainSceneEntries[0].buffer = m_sceneBuffer;
            mainSceneEntries[0].size = kSceneBufferSize;
            mainSceneEntries[1].binding = 1;
            mainSceneEntries[1].textureView = m_shadowView;
            mainSceneEntries[2].binding = 2;
            mainSceneEntries[2].sampler = m_shadowSampler;

            WGPUBindGroupDescriptor mainSceneDesc{};
            mainSceneDesc.label = {"main_g0_bg", WGPU_STRLEN};
            mainSceneDesc.layout = m_mainSceneBindGroupLayout;
            mainSceneDesc.entryCount = 3;
            mainSceneDesc.entries = mainSceneEntries;
            m_mainSceneBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &mainSceneDesc);
            if (!m_mainSceneBindGroup)
                return false;

            WGPUBindGroupEntry modelEntries[1] = {};
            modelEntries[0].binding = 0;
            modelEntries[0].buffer = m_modelBuffer;
            modelEntries[0].size = kModelBufferSize;

            WGPUBindGroupDescriptor modelDesc{};
            modelDesc.label = {"g1_bg", WGPU_STRLEN};
            modelDesc.layout = m_modelBindGroupLayout;
            modelDesc.entryCount = 1;
            modelDesc.entries = modelEntries;
            m_modelBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &modelDesc);
            return m_modelBindGroup != nullptr;
        }

        bool CreatePipelines(pwgpu::test::SampleContext &ctx)
        {
            WGPUVertexAttribute attributes[2] = {};
            attributes[0].format = WGPUVertexFormat_Float32x3;
            attributes[0].offset = 0;
            attributes[0].shaderLocation = 0;
            attributes[1].format = WGPUVertexFormat_Float32x3;
            attributes[1].offset = 3 * sizeof(float);
            attributes[1].shaderLocation = 1;

            WGPUVertexBufferLayout vertexLayout{};
            vertexLayout.arrayStride = 6 * sizeof(float);
            vertexLayout.stepMode = WGPUVertexStepMode_Vertex;
            vertexLayout.attributeCount = 2;
            vertexLayout.attributes = attributes;

            WGPUDepthStencilState shadowDepth{};
            shadowDepth.format = WGPUTextureFormat_Depth32Float;
            shadowDepth.depthWriteEnabled = WGPUOptionalBool_True;
            shadowDepth.depthCompare = WGPUCompareFunction_Less;
            shadowDepth.stencilFront.compare = WGPUCompareFunction_Always;
            shadowDepth.stencilFront.failOp = WGPUStencilOperation_Keep;
            shadowDepth.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
            shadowDepth.stencilFront.passOp = WGPUStencilOperation_Keep;
            shadowDepth.stencilBack = shadowDepth.stencilFront;

            WGPURenderPipelineDescriptor shadowDesc{};
            shadowDesc.label = {"shadow_pipe", WGPU_STRLEN};
            shadowDesc.layout = m_shadowPipelineLayout;
            shadowDesc.vertex.module = m_shadowVertexShader;
            shadowDesc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            shadowDesc.vertex.bufferCount = 1;
            shadowDesc.vertex.buffers = &vertexLayout;
            shadowDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            shadowDesc.primitive.cullMode = WGPUCullMode_Back;
            shadowDesc.primitive.frontFace = WGPUFrontFace_CCW;
            shadowDesc.depthStencil = &shadowDepth;
            shadowDesc.multisample.count = 1;
            shadowDesc.multisample.mask = 0xFFFFFFFF;
            shadowDesc.fragment = nullptr;
            m_shadowPipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &shadowDesc);
            if (!m_shadowPipeline)
                return false;

            WGPUColorTargetState colorTarget{};
            colorTarget.format = ctx.surfaceFormat;
            colorTarget.writeMask = WGPUColorWriteMask_All;

            WGPUFragmentState fragmentState{};
            fragmentState.module = m_litPixelShader;
            fragmentState.entryPoint = {"PSMain", WGPU_STRLEN};
            fragmentState.targetCount = 1;
            fragmentState.targets = &colorTarget;

            WGPUDepthStencilState mainDepth{};
            mainDepth.format = WGPUTextureFormat_Depth24Plus;
            mainDepth.depthWriteEnabled = WGPUOptionalBool_True;
            mainDepth.depthCompare = WGPUCompareFunction_Less;
            mainDepth.stencilFront.compare = WGPUCompareFunction_Always;
            mainDepth.stencilFront.failOp = WGPUStencilOperation_Keep;
            mainDepth.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
            mainDepth.stencilFront.passOp = WGPUStencilOperation_Keep;
            mainDepth.stencilBack = mainDepth.stencilFront;

            WGPURenderPipelineDescriptor mainDesc{};
            mainDesc.label = {"main_pipe", WGPU_STRLEN};
            mainDesc.layout = m_mainPipelineLayout;
            mainDesc.vertex.module = m_litVertexShader;
            mainDesc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            mainDesc.vertex.bufferCount = 1;
            mainDesc.vertex.buffers = &vertexLayout;
            mainDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            mainDesc.primitive.cullMode = WGPUCullMode_Back;
            mainDesc.primitive.frontFace = WGPUFrontFace_CCW;
            mainDesc.depthStencil = &mainDepth;
            mainDesc.multisample.count = 1;
            mainDesc.multisample.mask = 0xFFFFFFFF;
            mainDesc.fragment = &fragmentState;
            m_mainPipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &mainDesc);
            return m_mainPipeline != nullptr;
        }

        void ReleaseMainDepthTarget()
        {
            if (m_mainDepthView)
            {
                wgpuTextureViewRelease(m_mainDepthView);
                m_mainDepthView = nullptr;
            }
            if (m_mainDepthTexture)
            {
                wgpuTextureRelease(m_mainDepthTexture);
                m_mainDepthTexture = nullptr;
            }
        }

        DragonMesh m_mesh{};
        uint64_t m_indexBufferSize = 0;
        WGPUShaderModule m_shadowVertexShader = nullptr;
        WGPUShaderModule m_litVertexShader = nullptr;
        WGPUShaderModule m_litPixelShader = nullptr;
        WGPUBuffer m_vertexBuffer = nullptr;
        WGPUBuffer m_indexBuffer = nullptr;
        WGPUBuffer m_sceneBuffer = nullptr;
        WGPUBuffer m_modelBuffer = nullptr;
        WGPUTexture m_shadowTexture = nullptr;
        WGPUTextureView m_shadowView = nullptr;
        WGPUSampler m_shadowSampler = nullptr;
        WGPUTexture m_mainDepthTexture = nullptr;
        WGPUTextureView m_mainDepthView = nullptr;
        WGPUBindGroupLayout m_shadowSceneBindGroupLayout = nullptr;
        WGPUBindGroupLayout m_mainSceneBindGroupLayout = nullptr;
        WGPUBindGroupLayout m_modelBindGroupLayout = nullptr;
        WGPUPipelineLayout m_shadowPipelineLayout = nullptr;
        WGPUPipelineLayout m_mainPipelineLayout = nullptr;
        WGPUBindGroup m_shadowSceneBindGroup = nullptr;
        WGPUBindGroup m_mainSceneBindGroup = nullptr;
        WGPUBindGroup m_modelBindGroup = nullptr;
        WGPURenderPipeline m_shadowPipeline = nullptr;
        WGPURenderPipeline m_mainPipeline = nullptr;
    };

    pwgpu::test::SampleAppDesc MakeShadowMappingDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "ShadowMappingTest";
        desc.windowTitle = "PhasmaWebGPU Shadow Mapping";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    ShadowMappingSample sample;
    pwgpu::test::SampleApp app(sample, MakeShadowMappingDesc());
    return app.Run(argc, argv);
}
