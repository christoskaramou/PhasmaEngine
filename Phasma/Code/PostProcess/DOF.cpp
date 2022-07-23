#include "DOF.h"
#include "GUI/GUI.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Surface.h"
#include "Shader/Shader.h"
#include "Renderer/RHI.h"
#include "Renderer/Command.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/Image.h"
#include "Renderer/Pipeline.h"
#include "Systems/CameraSystem.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    DOF::DOF()
    {
        DSet = {};
        pipeline = nullptr;
    }

    DOF::~DOF()
    {
    }

    void DOF::Init()
    {
        RendererSystem *rs = CONTEXT->GetSystem<RendererSystem>();

        displayRT = rs->GetRenderTarget("display");
        depth = rs->GetDepthTarget("depth");
        frameImage = rs->CreateFSSampledImage(false);
    }

    void DOF::UpdatePipelineInfo()
    {
        pipelineInfo = std::make_shared<PipelineCreateInfo>();
        PipelineCreateInfo &info = *pipelineInfo;

        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.vert", ShaderStage::VertexBit});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/DepthOfField/DOF.frag", ShaderStage::FragmentBit});
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {displayRT->blendAttachment};
        info.pushConstantStage = ShaderStage::FragmentBit;
        info.pushConstantSize = static_cast<uint32_t>(sizeof(vec4));
        info.descriptorSetLayouts = {DSet->GetLayout()};
        info.dynamicColorTargets = 1;
        info.colorFormats = &displayRT->imageInfo.format;
        info.name = "dof_pipeline";
    }

    void DOF::CreateUniforms(CommandBuffer *cmd)
    {
        DescriptorBindingInfo bindingInfos[2]{};

        bindingInfos[0].binding = 0;
        bindingInfos[0].type = DescriptorType::SampledImage;
        bindingInfos[0].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[0].pImage = frameImage;
        //bindingInfos[0].sampler = frameImage->sampler;

        bindingInfos[1].binding = 1;
        bindingInfos[1].type = DescriptorType::Sampler;
        bindingInfos[1].imageLayout = ImageLayout::DepthStencilReadOnly;
        bindingInfos[1].pImage = depth;
        bindingInfos[1].sampler = depth->sampler;

        DescriptorInfo info{};
        info.count = 2;
        info.bindingInfos = bindingInfos;
        info.stage = ShaderStage::FragmentBit;

        DSet = Descriptor::Create(&info, "DOF_descriptor");
    }

    void DOF::UpdateDescriptorSets()
    {
        DescriptorBindingInfo bindingInfos[2]{};

        bindingInfos[0].binding = 0;
        bindingInfos[0].type = DescriptorType::CombinedImageSampler;
        bindingInfos[0].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[0].pImage = frameImage;
        bindingInfos[0].sampler = frameImage->sampler;

        bindingInfos[1].binding = 1;
        bindingInfos[1].type = DescriptorType::CombinedImageSampler;
        bindingInfos[1].imageLayout = ImageLayout::DepthStencilReadOnly;
        bindingInfos[1].pImage = depth;
        bindingInfos[1].sampler = depth->sampler;

        DescriptorInfo info{};
        info.count = 2;
        info.bindingInfos = bindingInfos;
        info.stage = ShaderStage::FragmentBit;

        DSet->UpdateDescriptor(&info);
    }

    void DOF::Update(Camera *camera)
    {
    }

    void DOF::Draw(CommandBuffer *cmd, uint32_t imageIndex)
    {
        std::vector<float> values{GUI::DOF_focus_scale, GUI::DOF_blur_range, 0.0f, 0.0f};

        cmd->BeginDebugRegion("DOF");
        // Copy RT
        cmd->CopyImage(displayRT, frameImage);

        // DEPTH OF FIELD
        // Input
        cmd->ImageBarrier(frameImage, ImageLayout::ShaderReadOnly);
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
        cmd->PushConstants(pipeline, ShaderStage::FragmentBit, 0, uint32_t(sizeof(float) * values.size()),
                           values.data());
        cmd->BindDescriptors(pipeline, 1, &DSet);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
        cmd->EndDebugRegion();
    }

    void DOF::Resize(uint32_t width, uint32_t height)
    {
        Image::Destroy(frameImage);
        Init();
        UpdateDescriptorSets();
    }

    void DOF::Destroy()
    {
        Descriptor::Destroy(DSet);
        Pipeline::Destroy(pipeline);
        Image::Destroy(frameImage);
    }
}
