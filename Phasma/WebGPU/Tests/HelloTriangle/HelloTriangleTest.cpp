#include "../Common/SampleApp.h"
#include "../Common/SampleBase.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"

namespace
{
    bool CreateTrianglePipeline(pwgpu::test::SampleContext &ctx,
                                WGPUShaderModule &shader,
                                WGPURenderPipeline &pipeline)
    {
        shader = pwgpu::test::MakeWgslShaderModule(
            ctx.device, (ctx.shaderDir + "triangle.wgsl").c_str(), "triangle_wgsl");
        if (!shader)
            return false;

        WGPUColorTargetState colorTarget{};
        colorTarget.format = ctx.surfaceFormat;
        colorTarget.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fragmentState{};
        fragmentState.module = shader;
        fragmentState.entryPoint = {"fs_main", WGPU_STRLEN};
        fragmentState.targetCount = 1;
        fragmentState.targets = &colorTarget;

        WGPURenderPipelineDescriptor pipelineDesc{};
        pipelineDesc.label = {"tri_pipe", WGPU_STRLEN};
        pipelineDesc.vertex.module = shader;
        pipelineDesc.vertex.entryPoint = {"vs_main", WGPU_STRLEN};
        pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pipelineDesc.primitive.cullMode = WGPUCullMode_None;
        pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
        pipelineDesc.multisample.count = 1;
        pipelineDesc.multisample.mask = 0xFFFFFFFF;
        pipelineDesc.fragment = &fragmentState;

        pipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &pipelineDesc);
        return pipeline != nullptr;
    }

    class HelloTriangleSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            return CreateTrianglePipeline(ctx, m_shader, m_pipeline);
        }

        bool Execute(pwgpu::test::SampleContext &,
                     pwgpu::test::SampleFrame &frame) override
        {
            WGPURenderPassEncoder renderPass =
                BeginRenderPass(frame, "render_pass", {0.0, 0.0, 0.0, 1.0});
            wgpuRenderPassEncoderSetPipeline(renderPass, m_pipeline);
            wgpuRenderPassEncoderDraw(renderPass, 3, 1, 0, 0);
            wgpuRenderPassEncoderEnd(renderPass);
            wgpuRenderPassEncoderRelease(renderPass);
            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            ReleaseResources();
        }

    private:
        void ReleaseResources()
        {
            if (m_pipeline)
            {
                wgpuRenderPipelineRelease(m_pipeline);
                m_pipeline = nullptr;
            }

            if (m_shader)
            {
                wgpuShaderModuleRelease(m_shader);
                m_shader = nullptr;
            }
        }

        WGPUShaderModule m_shader = nullptr;
        WGPURenderPipeline m_pipeline = nullptr;
    };

    pwgpu::test::SampleAppDesc MakeHelloTriangleDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "HelloTriangleTest";
        desc.windowTitle = "Phasma WebGPU Hello Triangle";
        desc.initialWidth = 1280;
        desc.initialHeight = 720;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    HelloTriangleSample sample;
    pwgpu::test::SampleApp app(sample, MakeHelloTriangleDesc());
    return app.Run(argc, argv);
}
