#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb/stb_image.h"

#include "../Common/CubeGeometry.h"
#include "../Common/SampleApp.h"
#include "../Common/SampleBase.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"

#include <cmath>

namespace
{
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    constexpr float kPi = 3.14159265f;

    class CamerasSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            m_vertexShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "cube.vert.hlsl").c_str(), "cube_vs");
            m_pixelShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "cube.pixel.hlsl").c_str(), "cube_ps");
            if (!m_vertexShader || !m_pixelShader)
                return false;

            const std::string imagePath = pwgpu::test::GetAssetPath(ctx.exeDir, "Di-3d.png");
            int imageWidth = 0;
            int imageHeight = 0;
            int imageChannels = 0;
            stbi_uc *pixels =
                stbi_load(imagePath.c_str(), &imageWidth, &imageHeight, &imageChannels, 4);
            if (!pixels)
                return false;

            m_texture = CreateTexture(ctx.device, imageWidth, imageHeight);
            if (!m_texture)
            {
                stbi_image_free(pixels);
                return false;
            }

            pwgpu::test::UploadRGBA8Texture(ctx.queue,
                                            m_texture,
                                            pixels,
                                            static_cast<uint32_t>(imageWidth),
                                            static_cast<uint32_t>(imageHeight));
            stbi_image_free(pixels);

            m_textureView =
                pwgpu::test::CreateTextureView(m_texture, WGPUTextureFormat_RGBA8Unorm);
            m_sampler = CreateSampler(ctx.device);
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
            if (!m_textureView || !m_sampler || !m_vertexBuffer || !m_uniformBuffer)
                return false;

            wgpuQueueWriteBuffer(ctx.queue,
                                 m_vertexBuffer,
                                 0,
                                 pwgpu::test::cube::kVertices,
                                 sizeof(pwgpu::test::cube::kVertices));

            if (!CreatePipelineResources(ctx))
                return false;

            return true;
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

        void HandleEvent(pwgpu::test::SampleContext &, const SDL_Event &event, bool &running) override
        {
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
            {
                running = false;
                return;
            }

            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT)
            {
                m_mouseLook = true;
                SDL_SetRelativeMouseMode(SDL_TRUE);
                return;
            }

            if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_RIGHT)
            {
                m_mouseLook = false;
                SDL_SetRelativeMouseMode(SDL_FALSE);
                return;
            }

            if (event.type == SDL_MOUSEMOTION && m_mouseLook)
            {
                m_mouseDx += static_cast<float>(event.motion.xrel);
                m_mouseDy += static_cast<float>(event.motion.yrel);
            }
        }

        void Update(pwgpu::test::SampleContext &ctx) override
        {
            m_yaw -= m_mouseDx * m_rotationSpeed;
            m_pitch -= m_mouseDy * m_rotationSpeed;
            m_pitch = glm::clamp(m_pitch, -kPi * 0.5f + 0.01f, kPi * 0.5f - 0.01f);
            m_mouseDx = 0.f;
            m_mouseDy = 0.f;

            const Uint8 *keys = SDL_GetKeyboardState(nullptr);
            float forwardInput =
                (keys[SDL_SCANCODE_S] ? 1.f : 0.f) - (keys[SDL_SCANCODE_W] ? 1.f : 0.f);
            float rightInput =
                (keys[SDL_SCANCODE_D] ? 1.f : 0.f) - (keys[SDL_SCANCODE_A] ? 1.f : 0.f);
            float upInput =
                (keys[SDL_SCANCODE_SPACE] ? 1.f : 0.f) -
                (keys[SDL_SCANCODE_LSHIFT] ? 1.f : 0.f);

            mat4 cameraRotation = glm::rotate(mat4(1.f), m_yaw, vec3(0.f, 1.f, 0.f));
            cameraRotation = glm::rotate(cameraRotation, m_pitch, vec3(1.f, 0.f, 0.f));
            vec3 cameraRight = vec3(cameraRotation[0]);
            vec3 cameraUp = vec3(cameraRotation[1]);
            vec3 cameraBack = vec3(cameraRotation[2]);

            vec3 targetVelocity(0.f);
            targetVelocity += cameraRight * rightInput;
            targetVelocity += cameraUp * upInput;
            targetVelocity += cameraBack * forwardInput;
            if (glm::length(targetVelocity) > 0.f)
                targetVelocity = glm::normalize(targetVelocity) * m_movementSpeed;

            float blend = powf(1.f - m_friction, static_cast<float>(ctx.timing.deltaSeconds));
            m_velocity = glm::mix(targetVelocity, m_velocity, blend);
            m_cameraPosition += m_velocity * static_cast<float>(ctx.timing.deltaSeconds);

            float aspect = ctx.height > 0 ? static_cast<float>(ctx.width) /
                                                static_cast<float>(ctx.height)
                                          : 1.0f;
            mat4 projection =
                glm::perspectiveRH_ZO((2.f * kPi) / 5.f, aspect, 1.f, 100.f);
            projection[1][1] *= -1.f;

            mat4 cameraMatrix = cameraRotation;
            cameraMatrix[3] = glm::vec4(m_cameraPosition, 1.f);
            mat4 view = glm::inverse(cameraMatrix);
            mat4 mvp = projection * view;
            wgpuQueueWriteBuffer(ctx.queue, m_uniformBuffer, 0, glm::value_ptr(mvp), 64);
        }

        bool Execute(pwgpu::test::SampleContext &,
                     pwgpu::test::SampleFrame &frame) override
        {
            if (!m_depthView || !m_pipeline || !m_bindGroup)
                return true;

            WGPURenderPassEncoder renderPass =
                BeginRenderPass(frame, "render_pass", {0.5, 0.5, 0.5, 1.0}, m_depthView);
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
            SDL_SetRelativeMouseMode(SDL_FALSE);
            ReleaseDepthTarget();

            if (m_pipeline)
                wgpuRenderPipelineRelease(m_pipeline);
            if (m_pipelineLayout)
                wgpuPipelineLayoutRelease(m_pipelineLayout);
            if (m_bindGroup)
                wgpuBindGroupRelease(m_bindGroup);
            if (m_bindGroupLayout)
                wgpuBindGroupLayoutRelease(m_bindGroupLayout);
            if (m_sampler)
                wgpuSamplerRelease(m_sampler);
            if (m_textureView)
                wgpuTextureViewRelease(m_textureView);
            if (m_texture)
                wgpuTextureRelease(m_texture);
            if (m_pixelShader)
                wgpuShaderModuleRelease(m_pixelShader);
            if (m_vertexShader)
                wgpuShaderModuleRelease(m_vertexShader);
            if (m_uniformBuffer)
                wgpuBufferRelease(m_uniformBuffer);
            if (m_vertexBuffer)
                wgpuBufferRelease(m_vertexBuffer);

            m_pipeline = nullptr;
            m_pipelineLayout = nullptr;
            m_bindGroup = nullptr;
            m_bindGroupLayout = nullptr;
            m_sampler = nullptr;
            m_textureView = nullptr;
            m_texture = nullptr;
            m_pixelShader = nullptr;
            m_vertexShader = nullptr;
            m_uniformBuffer = nullptr;
            m_vertexBuffer = nullptr;
        }

    private:
        static WGPUTexture CreateTexture(WGPUDevice device, int width, int height)
        {
            WGPUTextureDescriptor desc{};
            desc.label = {"cube_tex", WGPU_STRLEN};
            desc.dimension = WGPUTextureDimension_2D;
            desc.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
            desc.mipLevelCount = 1;
            desc.sampleCount = 1;
            desc.format = WGPUTextureFormat_RGBA8Unorm;
            desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
            return wgpuDeviceCreateTexture(device, &desc);
        }

        static WGPUSampler CreateSampler(WGPUDevice device)
        {
            WGPUSamplerDescriptor desc{};
            desc.label = {"cube_sampler", WGPU_STRLEN};
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
            return wgpuDeviceCreateSampler(device, &desc);
        }

        bool CreatePipelineResources(pwgpu::test::SampleContext &ctx)
        {
            WGPUBindGroupLayoutEntry entries[3] = {};
            entries[0].binding = 0;
            entries[0].visibility = WGPUShaderStage_Vertex;
            entries[0].buffer.type = WGPUBufferBindingType_Uniform;
            entries[0].buffer.minBindingSize = 64;
            entries[1].binding = 1;
            entries[1].visibility = WGPUShaderStage_Fragment;
            entries[1].sampler.type = WGPUSamplerBindingType_Filtering;
            entries[2].binding = 2;
            entries[2].visibility = WGPUShaderStage_Fragment;
            entries[2].texture.sampleType = WGPUTextureSampleType_Float;
            entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;
            entries[2].texture.multisampled = WGPUOptionalBool_False;

            WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc{};
            bindGroupLayoutDesc.label = {"bgl", WGPU_STRLEN};
            bindGroupLayoutDesc.entryCount = 3;
            bindGroupLayoutDesc.entries = entries;
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

            WGPUBindGroupEntry bindGroupEntries[3] = {};
            bindGroupEntries[0].binding = 0;
            bindGroupEntries[0].buffer = m_uniformBuffer;
            bindGroupEntries[0].size = 64;
            bindGroupEntries[1].binding = 1;
            bindGroupEntries[1].sampler = m_sampler;
            bindGroupEntries[2].binding = 2;
            bindGroupEntries[2].textureView = m_textureView;

            WGPUBindGroupDescriptor bindGroupDesc{};
            bindGroupDesc.label = {"bg", WGPU_STRLEN};
            bindGroupDesc.layout = m_bindGroupLayout;
            bindGroupDesc.entryCount = 3;
            bindGroupDesc.entries = bindGroupEntries;
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
        WGPUTexture m_texture = nullptr;
        WGPUTextureView m_textureView = nullptr;
        WGPUSampler m_sampler = nullptr;
        WGPUBindGroupLayout m_bindGroupLayout = nullptr;
        WGPUPipelineLayout m_pipelineLayout = nullptr;
        WGPUBindGroup m_bindGroup = nullptr;
        WGPURenderPipeline m_pipeline = nullptr;
        WGPUTexture m_depthTexture = nullptr;
        WGPUTextureView m_depthView = nullptr;

        vec3 m_cameraPosition = vec3(3.f, 2.f, 5.f);
        float m_yaw = atan2f(m_cameraPosition.x, m_cameraPosition.z);
        float m_pitch = -asinf(m_cameraPosition.y / glm::length(m_cameraPosition));
        vec3 m_velocity = vec3(0.f);
        float m_mouseDx = 0.f;
        float m_mouseDy = 0.f;
        bool m_mouseLook = false;

        const float m_movementSpeed = 10.f;
        const float m_rotationSpeed = 0.003f;
        const float m_friction = 0.99f;
    };

    pwgpu::test::SampleAppDesc MakeCamerasDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "CamerasTest";
        desc.windowTitle = "PhasmaWebGPU Cameras";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    CamerasSample sample;
    pwgpu::test::SampleApp app(sample, MakeCamerasDesc());
    return app.Run(argc, argv);
}
