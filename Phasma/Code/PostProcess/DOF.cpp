#include "DOF.h"
#include "GUI/GUI.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Surface.h"
#include "Shader/Shader.h"
#include "Renderer/RHI.h"
#include "Renderer/Command.h"
#include "Renderer/RenderPass.h"
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

    void DOF::UpdatePassInfo()
    {
        passInfo = std::make_shared<PassInfo>();
        PassInfo &info = *passInfo;

        info.name = "dof_pipeline";
        info.pVertShader = Shader::Create("Shaders/Common/quad.hlsl", ShaderStage::VertexBit);
        info.pFragShader = Shader::Create("Shaders/DepthOfField/dofPS.hlsl", ShaderStage::FragmentBit);
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {displayRT->blendAttachment};
        info.pushConstantStage = ShaderStage::FragmentBit;
        info.pushConstantSize = static_cast<uint32_t>(sizeof(vec4));
        info.descriptorSetLayouts = {DSet->GetLayout()};
        info.colorFormats = &displayRT->imageInfo.format;
        
        AttachmentInfo colorInfo{};
        colorInfo.image = displayRT;
        colorInfo.initialLayout = ImageLayout::Attachment;
        colorInfo.finalLayout = ImageLayout::Attachment;
        info.renderPass = CommandBuffer::GetRenderPass(1, &colorInfo, nullptr);

        info.UpdateHash();
    }

    void DOF::CreateUniforms(CommandBuffer *cmd)
    {
        std::vector<DescriptorBindingInfo> bindingInfos(2);
        bindingInfos[0].binding = 0;
        bindingInfos[0].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[0].type = DescriptorType::CombinedImageSampler;
        bindingInfos[1].binding = 1;
        bindingInfos[1].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[1].type = DescriptorType::CombinedImageSampler;
        DSet = Descriptor::Create(bindingInfos, ShaderStage::FragmentBit, "DOF_descriptor");

        UpdateDescriptorSets();
    }

    void DOF::UpdateDescriptorSets()
    {
        DSet->SetImage(0, frameImage->GetSRV(), frameImage->sampler->Handle());
        DSet->SetImage(1, depth->GetSRV(), depth->sampler->Handle());
        DSet->Update();
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
        cmd->ImageBarrier(depth, ImageLayout::ShaderReadOnly);
        
        cmd->BeginPass(passInfo->renderPass, &displayRT, nullptr);
        cmd->BindPipeline(*passInfo);
        cmd->SetViewport(0.f, 0.f, displayRT->width_f, displayRT->height_f);
        cmd->SetScissor(0, 0, displayRT->imageInfo.width, displayRT->imageInfo.height);
        cmd->PushConstants(ShaderStage::FragmentBit, 0, uint32_t(sizeof(float) * values.size()), values.data());
        cmd->BindDescriptors(1, &DSet);
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
        Image::Destroy(frameImage);
    }
}
