#include "PostProcess/SSGI.h"
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
    SSGI::SSGI()
    {
        DSet = {};
        pipeline = nullptr;
    }

    SSGI::~SSGI()
    {
    }

    void SSGI::Init()
    {
        RendererSystem *rs = CONTEXT->GetSystem<RendererSystem>();

        viewportRT = rs->GetRenderTarget("viewport");
        frameImage = rs->CreateFSSampledImage();
        depth = rs->GetDepthTarget("depth");
    }

    void SSGI::UpdatePipelineInfo()
    {
        pipelineInfo = std::make_shared<PipelineCreateInfo>();
        PipelineCreateInfo &info = *pipelineInfo;

        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.hlsl", ShaderStage::VertexBit});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/SSGI/SSGI.frag", ShaderStage::FragmentBit});
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {viewportRT->blendAttachment};
        info.pushConstantStage = ShaderStage::FragmentBit;
        info.pushConstantSize = 5 * sizeof(vec4);
        info.descriptorSetLayouts = {DSet->GetLayout()};
        info.dynamicColorTargets = 1;
        info.colorFormats = &viewportRT->imageInfo.format;
        info.name = "ssgi_pipeline";
    }

    void SSGI::CreateUniforms(CommandBuffer *cmd)
    {
        std::vector<DescriptorBindingInfo> bindingInfos(2);
        bindingInfos[0].binding = 0;
        bindingInfos[0].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[0].type = DescriptorType::CombinedImageSampler;
        bindingInfos[1].binding = 1;
        bindingInfos[1].imageLayout = ImageLayout::DepthStencilReadOnly;
        bindingInfos[1].type = DescriptorType::CombinedImageSampler;
        DSet = Descriptor::Create(bindingInfos, ShaderStage::FragmentBit, "SSGI_descriptor");

        UpdateDescriptorSets();
    }

    void SSGI::UpdateDescriptorSets()
    {
        DSet->SetImage(0, frameImage->GetSRV(), frameImage->sampler);
        DSet->SetImage(1, depth->GetSRV(), depth->sampler);
        DSet->UpdateDescriptor();
    }

    void SSGI::Update(Camera *camera)
    {
    }

    void SSGI::Draw(CommandBuffer *cmd, uint32_t imageIndex)
    {
        cmd->BeginDebugRegion("SSGI");

        Camera &camera = *CONTEXT->GetSystem<CameraSystem>()->GetCamera(0);

        // Copy RT
        cmd->CopyImage(viewportRT, frameImage);

        // SCREEN SPACE GLOBAL ILLUMINATION
        // Input
        cmd->ImageBarrier(frameImage, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(depth, ImageLayout::DepthStencilReadOnly);
        // Output
        cmd->ImageBarrier(viewportRT, ImageLayout::ColorAttachment);

        AttachmentInfo info{};
        info.image = viewportRT;
        info.initialLayout = viewportRT->GetCurrentLayout();
        info.finalLayout = ImageLayout::ColorAttachment;

        cmd->BeginPass(1, &info, nullptr, &pipelineInfo->renderPass);
        cmd->BindPipeline(*pipelineInfo, &pipeline);
        cmd->SetViewport(0.f, 0.f, viewportRT->width_f, viewportRT->height_f);
        cmd->SetScissor(0, 0, viewportRT->imageInfo.width, viewportRT->imageInfo.height);
        cmd->PushConstants(pipeline, ShaderStage::FragmentBit, 0, uint32_t(sizeof(mat4)), &camera.invViewProjection);
        cmd->BindDescriptors(pipeline, 1, &DSet);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();

        cmd->EndDebugRegion();
    }

    void SSGI::Resize(uint32_t width, uint32_t height)
    {
        Image::Destroy(frameImage);
        Init();
        UpdateDescriptorSets();
    }

    void SSGI::Destroy()
    {
        Descriptor::Destroy(DSet);
        Pipeline::Destroy(pipeline);
        Image::Destroy(frameImage);
    }
}
