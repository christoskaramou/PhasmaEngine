#include "MotionBlur.h"
#include "Renderer/Surface.h"
#include "Renderer/Swapchain.h"
#include "GUI/GUI.h"
#include "Shader/Shader.h"
#include "Renderer/RHI.h"
#include "Renderer/Command.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/Image.h"
#include "Renderer/Pipeline.h"
#include "Renderer/Buffer.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    MotionBlur::MotionBlur()
    {
        DSet = {};
        pipeline = nullptr;
    }

    MotionBlur::~MotionBlur()
    {
    }

    void MotionBlur::Init()
    {
        RendererSystem *rs = CONTEXT->GetSystem<RendererSystem>();

        displayRT = rs->GetRenderTarget("display");
        velocityRT = rs->GetRenderTarget("velocity");
        depth = rs->GetDepthTarget("depth");
        frameImage = rs->CreateFSSampledImage(false);
    }

    void MotionBlur::CreatePipeline()
    {
        pipelineInfo = std::make_shared<PipelineCreateInfo>();
        PipelineCreateInfo &info = *pipelineInfo;

        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.vert", ShaderStage::VertexBit});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/MotionBlur/motionBlur.frag", ShaderStage::FragmentBit});
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {displayRT->blendAttachment};
        info.pushConstantStage = ShaderStage::FragmentBit;
        info.pushConstantSize = sizeof(vec4);
        info.descriptorSetLayouts = {DSet->GetLayout()};
        info.dynamicColorTargets = 1;
        info.colorFormats = &displayRT->imageInfo.format;
        info.name = "motionBlur_pipeline";
    }

    void MotionBlur::CreateUniforms(CommandBuffer *cmd)
    {
        DescriptorBindingInfo bindingInfos[3]{};

        bindingInfos[0].binding = 0;
        bindingInfos[0].type = DescriptorType::CombinedImageSampler;
        bindingInfos[0].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[0].pImage = frameImage;

        bindingInfos[1].binding = 1;
        bindingInfos[1].type = DescriptorType::CombinedImageSampler;
        bindingInfos[1].imageLayout = ImageLayout::DepthStencilReadOnly;
        bindingInfos[1].pImage = depth;

        bindingInfos[2].binding = 2;
        bindingInfos[2].type = DescriptorType::CombinedImageSampler;
        bindingInfos[2].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[2].pImage = velocityRT;

        DescriptorInfo info{};
        info.count = 3;
        info.bindingInfos = bindingInfos;
        info.stage = ShaderStage::FragmentBit;

        DSet = Descriptor::Create(&info, "MotionBlur_descriptor");
        UpdateDescriptorSets();
    }

    void MotionBlur::UpdateDescriptorSets()
    {
        DescriptorBindingInfo bindingInfos[3]{};

        bindingInfos[0].binding = 0;
        bindingInfos[0].type = DescriptorType::CombinedImageSampler;
        bindingInfos[0].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[0].pImage = frameImage;

        bindingInfos[1].binding = 1;
        bindingInfos[1].type = DescriptorType::CombinedImageSampler;
        bindingInfos[1].imageLayout = ImageLayout::DepthStencilReadOnly;
        bindingInfos[1].pImage = depth;

        bindingInfos[2].binding = 2;
        bindingInfos[2].type = DescriptorType::CombinedImageSampler;
        bindingInfos[2].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[2].pImage = velocityRT;

        DescriptorInfo info{};
        info.count = 3;
        info.bindingInfos = bindingInfos;
        info.stage = ShaderStage::FragmentBit;

        DSet->UpdateDescriptor(&info);
    }

    void MotionBlur::Update(Camera *camera)
    {
        if (GUI::show_motionBlur)
        {
        }
    }

    void MotionBlur::Draw(CommandBuffer *cmd, uint32_t imageIndex)
    {
        const vec4 values{
            1.f / static_cast<float>(FrameTimer::Instance().GetDelta()),
            0.f, // sin(static_cast<float>(FrameTimer::Instance().Count()) * 0.125f),
            GUI::motionBlur_strength,
            0.f};

        cmd->BeginDebugRegion("MotionBlur");
        // Copy RT
        cmd->CopyImage(displayRT, frameImage);

        // MOTION BLUR
        // Input
        cmd->ImageBarrier(frameImage, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(velocityRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(depth, ImageLayout::DepthStencilReadOnly);
        // Output
        cmd->ImageBarrier(displayRT, ImageLayout::ColorAttachment);
        
        AttachmentInfo info{};
        info.image = displayRT;

        cmd->BeginPass(1, &info, nullptr, &pipelineInfo->renderPass);
        cmd->BindPipeline(*pipelineInfo, &pipeline);
        cmd->SetViewport(0.f, 0.f, displayRT->width_f, displayRT->height_f);
        cmd->SetScissor(0, 0, displayRT->imageInfo.width, displayRT->imageInfo.height);
        cmd->PushConstants(pipeline, ShaderStage::FragmentBit, 0, sizeof(vec4), &values);
        cmd->BindDescriptors(pipeline, 1, &DSet);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
        cmd->EndDebugRegion();
    }

    void MotionBlur::Resize(uint32_t width, uint32_t height)
    {
        Image::Destroy(frameImage);
        Init();
        UpdateDescriptorSets();
    }

    void MotionBlur::Destroy()
    {
        Descriptor::Destroy(DSet);
        Pipeline::Destroy(pipeline);
        Image::Destroy(frameImage);
    }
}
