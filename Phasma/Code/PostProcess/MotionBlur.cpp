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

    void MotionBlur::UpdatePipelineInfo()
    {
        pipelineInfo = std::make_shared<PipelineCreateInfo>();
        PipelineCreateInfo &info = *pipelineInfo;

        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.hlsl", ShaderStage::VertexBit});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/MotionBlur/motionBlurPS.hlsl", ShaderStage::FragmentBit});
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {displayRT->blendAttachment};
        info.pushConstantStage = ShaderStage::FragmentBit;
        info.pushConstantSize = sizeof(vec4);
        info.descriptorSetLayouts = {DSet->GetLayout()};
        info.dynamicColorTargets = 1;
        info.colorFormats = &displayRT->imageInfo.format;
        info.name = "motionBlur_pipeline";
        
        info.UpdateHash();
    }

    void MotionBlur::CreateUniforms(CommandBuffer *cmd)
    {
        std::vector<DescriptorBindingInfo> bindingInfos(3);
        bindingInfos[0].binding = 0;
        bindingInfos[0].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[0].type = DescriptorType::CombinedImageSampler;
        bindingInfos[1].binding = 1;
        bindingInfos[1].imageLayout = ImageLayout::DepthStencilReadOnly;
        bindingInfos[1].type = DescriptorType::CombinedImageSampler;
        bindingInfos[2].binding = 2;
        bindingInfos[2].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[2].type = DescriptorType::CombinedImageSampler;
        DSet = Descriptor::Create(bindingInfos, ShaderStage::FragmentBit, "MotionBlur_descriptor");

        UpdateDescriptorSets(); 
    }

    void MotionBlur::UpdateDescriptorSets()
    {
        DSet->SetImage(0, frameImage->GetSRV(), frameImage->sampler->Handle());
        DSet->SetImage(1, depth->GetSRV(), depth->sampler->Handle());
        DSet->SetImage(2, velocityRT->GetSRV(), velocityRT->sampler->Handle());
        DSet->Update();
    }

    void MotionBlur::Update(Camera *camera)
    {
        if (GUI::show_motionBlur)
        {
            pushConstData[0] = 1.f / static_cast<float>(FrameTimer::Instance().GetDelta());
            pushConstData[1] = GUI::motionBlur_strength;
            pushConstData[2] = camera->prevProjJitter.x - camera->projJitter.x;
            pushConstData[3] = camera->prevProjJitter.y - camera->projJitter.y;
        }
    }

    void MotionBlur::Draw(CommandBuffer *cmd, uint32_t imageIndex)
    {

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
        info.initialLayout = displayRT->GetCurrentLayout();
        info.finalLayout = ImageLayout::ColorAttachment;

        cmd->BeginPass(1, &info, nullptr, &pipelineInfo->renderPass);
        cmd->BindPipeline(*pipelineInfo, &pipeline);
        cmd->SetViewport(0.f, 0.f, displayRT->width_f, displayRT->height_f);
        cmd->SetScissor(0, 0, displayRT->imageInfo.width, displayRT->imageInfo.height);
        cmd->PushConstants(pipeline, ShaderStage::FragmentBit, 0, sizeof(vec4), &pushConstData);
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
