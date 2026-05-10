#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb/stb_image.h"

#include "../Common/CubeGeometry.h"
#include "../Common/SampleApp.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"

#include "Base/Log.h"

#include <cmath>

namespace
{
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    constexpr float kPi = 3.14159265f;

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

    class TexturedCubeSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            LoadedImage image{};
            std::string imagePath = pwgpu::test::GetAssetPath(ctx.exeDir, "Di-3d.png");
            if (!LoadImageRGBA8(imagePath, image))
            {
                fprintf(stderr, "        Place Di-3d.png next to the executable.\n");
                return false;
            }

            m_vertexShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "cube.vert.hlsl").c_str(), "cube_vs");
            m_pixelShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "cube.pixel.hlsl").c_str(), "cube_ps");
            if (!m_vertexShader || !m_pixelShader)
            {
                stbi_image_free(image.pixels);
                return false;
            }

            if (!CreateTextureResources(ctx, image))
            {
                stbi_image_free(image.pixels);
                return false;
            }
            stbi_image_free(image.pixels);

            m_vertexBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                       sizeof(pwgpu::test::cube::kVertices),
                                                       WGPUBufferUsage_Vertex |
                                                           WGPUBufferUsage_CopyDst,
                                                       "cube_vb");
            m_uniformBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                        64,
                                                        WGPUBufferUsage_Uniform |
                                                            WGPUBufferUsage_CopyDst,
                                                        "cube_ubo");
            if (!m_vertexBuffer || !m_uniformBuffer)
                return false;

            wgpuQueueWriteBuffer(ctx.queue,
                                 m_vertexBuffer,
                                 0,
                                 pwgpu::test::cube::kVertices,
                                 sizeof(pwgpu::test::cube::kVertices));

            if (!CreatePipelineResources(ctx))
                return false;

            Resize(ctx, ctx.width, ctx.height);
            return m_depthTexture != nullptr && m_depthView != nullptr;
        }

        void Resize(pwgpu::test::SampleContext &ctx, uint32_t width, uint32_t height) override
        {
            ReleaseDepthTarget();
            if (width == 0 || height == 0)
                return;

            WGPUTextureDescriptor depthDesc{};
            depthDesc.label = {"depth_tex", WGPU_STRLEN};
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
            mat4 projection =
                glm::perspectiveRH_ZO((2.f * kPi) / 5.f, aspect, 1.f, 100.f);
            projection[1][1] *= -1.f;

            float time = static_cast<float>(ctx.timing.elapsedSeconds);
            mat4 view = glm::translate(mat4(1.f), vec3(0.f, 0.f, -4.f));
            view = glm::rotate(view, 1.f, vec3(sinf(time), cosf(time), 0.f));
            mat4 mvp = projection * view;
            wgpuQueueWriteBuffer(ctx.queue, m_uniformBuffer, 0, glm::value_ptr(mvp), 64);
        }

        bool Execute(pwgpu::test::SampleContext &, pwgpu::test::SampleFrame &frame) override
        {
            if (!m_depthView)
                return true;

            WGPURenderPassColorAttachment colorAttachment{};
            colorAttachment.view = frame.surfaceView;
            colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            colorAttachment.loadOp = WGPULoadOp_Clear;
            colorAttachment.storeOp = WGPUStoreOp_Store;
            colorAttachment.clearValue = {0.5, 0.5, 0.5, 1.0};

            WGPURenderPassDepthStencilAttachment depthAttachment{};
            depthAttachment.view = m_depthView;
            depthAttachment.depthLoadOp = WGPULoadOp_Clear;
            depthAttachment.depthStoreOp = WGPUStoreOp_Store;
            depthAttachment.depthClearValue = 1.0f;
            depthAttachment.stencilLoadOp = WGPULoadOp_Undefined;
            depthAttachment.stencilStoreOp = WGPUStoreOp_Undefined;

            WGPURenderPassDescriptor renderPassDesc{};
            renderPassDesc.label = {"render_pass", WGPU_STRLEN};
            renderPassDesc.colorAttachmentCount = 1;
            renderPassDesc.colorAttachments = &colorAttachment;
            renderPassDesc.depthStencilAttachment = &depthAttachment;

            WGPURenderPassEncoder renderPass =
                wgpuCommandEncoderBeginRenderPass(frame.encoder, &renderPassDesc);
            wgpuRenderPassEncoderSetPipeline(renderPass, m_pipeline);
            wgpuRenderPassEncoderSetBindGroup(renderPass, 0, m_bindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetVertexBuffer(renderPass,
                                                 0,
                                                 m_vertexBuffer,
                                                 0,
                                                 sizeof(pwgpu::test::cube::kVertices));
            wgpuRenderPassEncoderDraw(renderPass, pwgpu::test::cube::kVertexCount, 1, 0, 0);
            wgpuRenderPassEncoderEnd(renderPass);
            wgpuRenderPassEncoderRelease(renderPass);
            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            ReleaseDepthTarget();

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
        }

    private:
        bool CreateTextureResources(pwgpu::test::SampleContext &ctx, const LoadedImage &image)
        {
            WGPUTextureDescriptor textureDesc{};
            textureDesc.label = {"cube_tex", WGPU_STRLEN};
            textureDesc.dimension = WGPUTextureDimension_2D;
            textureDesc.size = {static_cast<uint32_t>(image.width),
                                static_cast<uint32_t>(image.height),
                                1};
            textureDesc.mipLevelCount = 1;
            textureDesc.sampleCount = 1;
            textureDesc.format = WGPUTextureFormat_RGBA8Unorm;
            textureDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
            m_texture = wgpuDeviceCreateTexture(ctx.device, &textureDesc);
            if (!m_texture)
                return false;

            pwgpu::test::UploadRGBA8Texture(ctx.queue,
                                            m_texture,
                                            image.pixels,
                                            static_cast<uint32_t>(image.width),
                                            static_cast<uint32_t>(image.height));

            m_textureView = pwgpu::test::CreateTextureView(m_texture, WGPUTextureFormat_RGBA8Unorm);
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
            bindGroupEntries[0].visibility = WGPUShaderStage_Vertex;
            bindGroupEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
            bindGroupEntries[0].buffer.minBindingSize = 64;
            bindGroupEntries[1].binding = 1;
            bindGroupEntries[1].visibility = WGPUShaderStage_Fragment;
            bindGroupEntries[1].sampler.type = WGPUSamplerBindingType_Filtering;
            bindGroupEntries[2].binding = 2;
            bindGroupEntries[2].visibility = WGPUShaderStage_Fragment;
            bindGroupEntries[2].texture.sampleType = WGPUTextureSampleType_Float;
            bindGroupEntries[2].texture.viewDimension = WGPUTextureViewDimension_2D;
            bindGroupEntries[2].texture.multisampled = WGPUOptionalBool_False;

            WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc{};
            bindGroupLayoutDesc.label = {"bgl", WGPU_STRLEN};
            bindGroupLayoutDesc.entryCount = 3;
            bindGroupLayoutDesc.entries = bindGroupEntries;
            m_bindGroupLayout =
                wgpuDeviceCreateBindGroupLayout(ctx.device, &bindGroupLayoutDesc);
            if (!m_bindGroupLayout)
                return false;

            WGPUPipelineLayoutDescriptor pipelineLayoutDesc{};
            pipelineLayoutDesc.label = {"pl", WGPU_STRLEN};
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
            bindGroupDesc.label = {"bg", WGPU_STRLEN};
            bindGroupDesc.layout = m_bindGroupLayout;
            bindGroupDesc.entryCount = 3;
            bindGroupDesc.entries = bindGroupEntriesDesc;
            m_bindGroup = wgpuDeviceCreateBindGroup(ctx.device, &bindGroupDesc);
            if (!m_bindGroup)
                return false;

            WGPUVertexAttribute attributes[2] = {};
            attributes[0].format = WGPUVertexFormat_Float32x4;
            attributes[0].offset = pwgpu::test::cube::kPositionOffset;
            attributes[0].shaderLocation = 0;
            attributes[1].format = WGPUVertexFormat_Float32x2;
            attributes[1].offset = pwgpu::test::cube::kUvOffset;
            attributes[1].shaderLocation = 1;

            WGPUVertexBufferLayout vertexBufferLayout{};
            vertexBufferLayout.arrayStride = pwgpu::test::cube::kVertexStride;
            vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
            vertexBufferLayout.attributeCount = 2;
            vertexBufferLayout.attributes = attributes;

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
            depthState.stencilBack.compare = WGPUCompareFunction_Always;

            WGPURenderPipelineDescriptor pipelineDesc{};
            pipelineDesc.label = {"pipe_cube", WGPU_STRLEN};
            pipelineDesc.layout = m_pipelineLayout;
            pipelineDesc.vertex.module = m_vertexShader;
            pipelineDesc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            pipelineDesc.vertex.bufferCount = 1;
            pipelineDesc.vertex.buffers = &vertexBufferLayout;
            pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            pipelineDesc.primitive.cullMode = WGPUCullMode_Back;
            pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
            pipelineDesc.multisample.count = 1;
            pipelineDesc.multisample.mask = 0xFFFFFFFF;
            pipelineDesc.depthStencil = &depthState;
            pipelineDesc.fragment = &fragmentState;
            m_pipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &pipelineDesc);
            return m_pipeline != nullptr;
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
        WGPUBuffer m_vertexBuffer = nullptr;
        WGPUBuffer m_uniformBuffer = nullptr;
        WGPUBindGroupLayout m_bindGroupLayout = nullptr;
        WGPUPipelineLayout m_pipelineLayout = nullptr;
        WGPUBindGroup m_bindGroup = nullptr;
        WGPURenderPipeline m_pipeline = nullptr;
        WGPUTexture m_texture = nullptr;
        WGPUTextureView m_textureView = nullptr;
        WGPUSampler m_sampler = nullptr;
        WGPUTexture m_depthTexture = nullptr;
        WGPUTextureView m_depthView = nullptr;
    };

    pwgpu::test::SampleAppDesc MakeTexturedCubeDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "TexturedCubeTest";
        desc.windowTitle = "PhasmaWebGPU Textured Cube";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    pe::Log::Init();
    setvbuf(stdout, nullptr, _IONBF, 0);

    TexturedCubeSample sample;
    pwgpu::test::SampleApp app(sample, MakeTexturedCubeDesc());
    return app.Run(argc, argv);
}
