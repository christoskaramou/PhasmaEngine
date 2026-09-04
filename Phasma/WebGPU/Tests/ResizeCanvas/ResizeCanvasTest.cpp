#include "../Common/SampleApp.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"


#include <cmath>

namespace
{
    constexpr uint32_t kSampleCount = 4;
    constexpr uint32_t kInitialWidth = 1280;
    constexpr uint32_t kInitialHeight = 720;

    bool CreateTrianglePipeline(pwgpu::test::SampleContext &ctx,
                                WGPUShaderModule &vertexShader,
                                WGPUShaderModule &pixelShader,
                                WGPURenderPipeline &pipeline)
    {
        vertexShader = pwgpu::test::MakeRuntimeShaderModule(
            ctx.device, (ctx.shaderDir + "triangle.vert.hlsl").c_str(), "tri_vs");
        pixelShader = pwgpu::test::MakeRuntimeShaderModule(
            ctx.device, (ctx.shaderDir + "triangle.pixel.hlsl").c_str(), "tri_ps");
        if (!vertexShader || !pixelShader)
            return false;

        WGPUColorTargetState colorTarget{};
        colorTarget.format = ctx.surfaceFormat;
        colorTarget.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fragmentState{};
        fragmentState.module = pixelShader;
        fragmentState.entryPoint = {"PSMain", WGPU_STRLEN};
        fragmentState.targetCount = 1;
        fragmentState.targets = &colorTarget;

        WGPURenderPipelineDescriptor pipelineDesc{};
        pipelineDesc.label = {"tri_pipe", WGPU_STRLEN};
        pipelineDesc.vertex.module = vertexShader;
        pipelineDesc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
        pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pipelineDesc.primitive.cullMode = WGPUCullMode_None;
        pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
        pipelineDesc.multisample.count = kSampleCount;
        pipelineDesc.multisample.mask = 0xFFFFFFFF;
        pipelineDesc.fragment = &fragmentState;

        pipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &pipelineDesc);
        return pipeline != nullptr;
    }

    class ResizeCanvasSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            if (!CreateTrianglePipeline(ctx, m_vertexShader, m_pixelShader, m_pipeline))
                return false;

            Resize(ctx, ctx.width, ctx.height);
            return m_msaaTexture != nullptr && m_msaaView != nullptr;
        }

        void Resize(pwgpu::test::SampleContext &ctx, uint32_t width, uint32_t height) override
        {
            CreateMsaaTarget(ctx, width, height);
        }

        bool CreateMsaaTarget(pwgpu::test::SampleContext &ctx, uint32_t width, uint32_t height)
        {
            ReleaseMsaaTarget();
            if (width == 0 || height == 0)
                return false;

            WGPUTextureDescriptor textureDesc{};
            textureDesc.label = {"msaa_color", WGPU_STRLEN};
            textureDesc.dimension = WGPUTextureDimension_2D;
            textureDesc.size = {width, height, 1};
            textureDesc.mipLevelCount = 1;
            textureDesc.sampleCount = kSampleCount;
            textureDesc.format = ctx.surfaceFormat;
            textureDesc.usage = WGPUTextureUsage_RenderAttachment;
            m_msaaTexture = wgpuDeviceCreateTexture(ctx.device, &textureDesc);

            if (!m_msaaTexture)
                return false;

            m_msaaView = pwgpu::test::CreateTextureView(m_msaaTexture, ctx.surfaceFormat);
            if (!m_msaaView)
                return false;

            m_msaaWidth = width;
            m_msaaHeight = height;
            return true;
        }

        void Update(pwgpu::test::SampleContext &ctx) override
        {
            // Match the upstream sample's animated canvas growth/shrink cycle.
            const float phase = 0.5f *
                                (1.0f - cosf(static_cast<float>(ctx.timing.elapsedSeconds) *
                                             3.14159265f / 2.0f));
            const uint32_t targetWidth =
                static_cast<uint32_t>(kInitialWidth + phase * 320.0f);
            const uint32_t targetHeight =
                static_cast<uint32_t>(kInitialHeight + phase * 180.0f);

            int windowWidth = 0;
            int windowHeight = 0;
            SDL_GetWindowSize(ctx.window, &windowWidth, &windowHeight);
            if (static_cast<uint32_t>(windowWidth) != targetWidth ||
                static_cast<uint32_t>(windowHeight) != targetHeight)
            {
                SDL_SetWindowSize(ctx.window,
                                  static_cast<int>(targetWidth),
                                  static_cast<int>(targetHeight));
            }
        }

        bool Execute(pwgpu::test::SampleContext &ctx, pwgpu::test::SampleFrame &frame) override
        {
            const uint32_t surfaceWidth = wgpuTextureGetWidth(frame.surfaceTexture.texture);
            const uint32_t surfaceHeight = wgpuTextureGetHeight(frame.surfaceTexture.texture);
            if (!m_msaaView || m_msaaWidth != surfaceWidth || m_msaaHeight != surfaceHeight)
            {
                if (!CreateMsaaTarget(ctx, surfaceWidth, surfaceHeight))
                    return true;
            }
            if (!m_msaaView)
                return true;

            WGPURenderPassColorAttachment attachment{};
            attachment.view = m_msaaView;
            attachment.resolveTarget = frame.surfaceView;
            attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            attachment.loadOp = WGPULoadOp_Clear;
            attachment.storeOp = WGPUStoreOp_Discard;
            attachment.clearValue = {0.2, 0.2, 0.2, 1.0};

            WGPURenderPassDescriptor renderPassDesc{};
            renderPassDesc.label = {"render_pass", WGPU_STRLEN};
            renderPassDesc.colorAttachmentCount = 1;
            renderPassDesc.colorAttachments = &attachment;

            WGPURenderPassEncoder renderPass =
                wgpuCommandEncoderBeginRenderPass(frame.encoder, &renderPassDesc);
            wgpuRenderPassEncoderSetPipeline(renderPass, m_pipeline);
            wgpuRenderPassEncoderDraw(renderPass, 3, 1, 0, 0);
            wgpuRenderPassEncoderEnd(renderPass);
            wgpuRenderPassEncoderRelease(renderPass);
            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            ReleaseMsaaTarget();
            ReleasePipeline();
        }

    private:
        void ReleaseMsaaTarget()
        {
            if (m_msaaView)
            {
                wgpuTextureViewRelease(m_msaaView);
                m_msaaView = nullptr;
            }

            if (m_msaaTexture)
            {
                wgpuTextureRelease(m_msaaTexture);
                m_msaaTexture = nullptr;
            }
            m_msaaWidth = 0;
            m_msaaHeight = 0;
        }

        void ReleasePipeline()
        {
            if (m_pipeline)
            {
                wgpuRenderPipelineRelease(m_pipeline);
                m_pipeline = nullptr;
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

        WGPUShaderModule m_vertexShader = nullptr;
        WGPUShaderModule m_pixelShader = nullptr;
        WGPURenderPipeline m_pipeline = nullptr;
        WGPUTexture m_msaaTexture = nullptr;
        WGPUTextureView m_msaaView = nullptr;
        uint32_t m_msaaWidth = 0;
        uint32_t m_msaaHeight = 0;
    };

    pwgpu::test::SampleAppDesc MakeResizeCanvasDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "ResizeCanvasTest";
        desc.windowTitle = "Phasma WebGPU Resize Canvas";
        desc.initialWidth = kInitialWidth;
        desc.initialHeight = kInitialHeight;
        desc.windowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    pe::Log::Init();
    setvbuf(stdout, nullptr, _IONBF, 0);

    ResizeCanvasSample sample;
    pwgpu::test::SampleApp app(sample, MakeResizeCanvasDesc());
    return app.Run(argc, argv);
}
