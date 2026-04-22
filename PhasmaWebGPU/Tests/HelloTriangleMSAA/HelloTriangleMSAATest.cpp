#include "../Common/SampleApp.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"

#include "Base/Log.h"

#include <cstdio>

namespace
{
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    constexpr uint32_t kSampleCount = 4;

    class HelloTriangleMSAASample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            m_vertexShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "triangle.vert.hlsl").c_str(), "tri_vs");
            m_pixelShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "triangle.pixel.hlsl").c_str(), "tri_ps");
            if (!m_vertexShader || !m_pixelShader)
                return false;

            WGPUColorTargetState colorTarget{};
            colorTarget.format = ctx.surfaceFormat;
            colorTarget.writeMask = WGPUColorWriteMask_All;

            WGPUFragmentState fragmentState{};
            fragmentState.module = m_pixelShader;
            fragmentState.entryPoint = {"PSMain", WGPU_STRLEN};
            fragmentState.targetCount = 1;
            fragmentState.targets = &colorTarget;

            WGPURenderPipelineDescriptor pipelineDesc{};
            pipelineDesc.label = {"tri_pipe", WGPU_STRLEN};
            pipelineDesc.vertex.module = m_vertexShader;
            pipelineDesc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            pipelineDesc.primitive.cullMode = WGPUCullMode_None;
            pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
            pipelineDesc.multisample.count = kSampleCount;
            pipelineDesc.multisample.mask = 0xFFFFFFFF;
            pipelineDesc.fragment = &fragmentState;

            m_pipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &pipelineDesc);
            if (!m_pipeline)
                return false;

            Resize(ctx, ctx.width, ctx.height);
            return m_msaaTexture != nullptr && m_msaaView != nullptr;
        }

        void Resize(pwgpu::test::SampleContext &ctx, uint32_t width, uint32_t height) override
        {
            ReleaseMsaaTarget();
            if (width == 0 || height == 0)
                return;

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
                return;

            m_msaaView = pwgpu::test::CreateTextureView(m_msaaTexture, ctx.surfaceFormat);
        }

        void Update(pwgpu::test::SampleContext &) override {}

        bool Execute(pwgpu::test::SampleContext &, pwgpu::test::SampleFrame &frame) override
        {
            if (!m_msaaView)
                return true;

            WGPURenderPassColorAttachment attachment{};
            attachment.view = m_msaaView;
            attachment.resolveTarget = frame.surfaceView;
            attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            attachment.loadOp = WGPULoadOp_Clear;
            attachment.storeOp = WGPUStoreOp_Discard;
            attachment.clearValue = {0.0, 0.0, 0.0, 1.0};

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
        }

        WGPUShaderModule m_vertexShader = nullptr;
        WGPUShaderModule m_pixelShader = nullptr;
        WGPURenderPipeline m_pipeline = nullptr;
        WGPUTexture m_msaaTexture = nullptr;
        WGPUTextureView m_msaaView = nullptr;
    };

    pwgpu::test::SampleAppDesc MakeHelloTriangleMSAADesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "HelloTriangleMSAATest";
        desc.windowTitle = "PhasmaWebGPU Hello Triangle MSAA";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    pe::Log::Init();
    setvbuf(stdout, nullptr, _IONBF, 0);

    HelloTriangleMSAASample sample;
    pwgpu::test::SampleApp app(sample, MakeHelloTriangleMSAADesc());
    return app.Run(argc, argv);
}
