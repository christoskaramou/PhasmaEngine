#include "FXAA.h"
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
#include "Systems/RendererSystem.h"

namespace pe
{
    FXAA::FXAA()
    {
        DSet = {};
    }

    FXAA::~FXAA()
    {
    }

    void FXAA::Init()
    {
        RendererSystem *rs = CONTEXT->GetSystem<RendererSystem>();

        viewportRT = rs->GetRenderTarget("viewport");
        frameImage = rs->CreateFSSampledImage();
    }

    void FXAA::UpdatePassInfo()
    {
        passInfo = std::make_shared<PassInfo>();
        PassInfo &info = *passInfo;

        info.name = "fxaa_pipeline";
        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.hlsl", ShaderStage::VertexBit});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/FXAA/fxaaPS.hlsl", ShaderStage::FragmentBit});
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {viewportRT->blendAttachment};
        info.descriptorSetLayouts = {DSet->GetLayout()};
        info.dynamicColorTargets = 1;
        info.colorFormats = &viewportRT->imageInfo.format;

        AttachmentInfo colorInfo{};
        colorInfo.image = viewportRT;
        colorInfo.initialLayout = ImageLayout::ColorAttachment;
        colorInfo.finalLayout = ImageLayout::ColorAttachment;
        info.renderPass = CommandBuffer::GetRenderPass(1, &colorInfo, nullptr);
        
        info.UpdateHash();
    }

    void FXAA::CreateUniforms(CommandBuffer *cmd)
    {
        std::vector<DescriptorBindingInfo> bindingInfos(1);
        bindingInfos[0].binding = 0;
        bindingInfos[0].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[0].type = DescriptorType::CombinedImageSampler;
        DSet = Descriptor::Create(bindingInfos, ShaderStage::FragmentBit, "FXAA_descriptor");

        UpdateDescriptorSets(); 
    }

    void FXAA::UpdateDescriptorSets()
    {
        DSet->SetImage(0, frameImage->GetSRV(), frameImage->sampler->Handle());
        DSet->Update();
    }

    void FXAA::Update(Camera *camera)
    {
    }

    void FXAA::Draw(CommandBuffer *cmd, uint32_t imageIndex)
    {
        cmd->BeginDebugRegion("FXAA");
        // Copy RT
        cmd->CopyImage(viewportRT, frameImage);

        // FAST APPROXIMATE ANTI-ALIASING
        // Input
        cmd->ImageBarrier(frameImage, ImageLayout::ShaderReadOnly);
        // Output
        cmd->ImageBarrier(viewportRT, ImageLayout::ColorAttachment);

        cmd->BeginPass(passInfo->renderPass, &viewportRT, nullptr);
        cmd->BindPipeline(*passInfo);
        cmd->SetViewport(0.f, 0.f, viewportRT->width_f, viewportRT->height_f);
        cmd->SetScissor(0, 0, viewportRT->imageInfo.width, viewportRT->imageInfo.height);
        cmd->BindDescriptors(1, &DSet);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
        cmd->EndDebugRegion();
    }

    void FXAA::Resize(uint32_t width, uint32_t height)
    {
        Image::Destroy(frameImage);
        Init();
        UpdateDescriptorSets();
    }

    void FXAA::Destroy()
    {
        Descriptor::Destroy(DSet);
        Image::Destroy(frameImage);
    }
}
