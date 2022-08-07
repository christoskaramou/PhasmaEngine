#include "Bloom.h"
#include "GUI/GUI.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Surface.h"
#include "Shader/Shader.h"
#include "Renderer/RHI.h"
#include "Renderer/Command.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Image.h"
#include "Renderer/Pipeline.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    Bloom::Bloom()
    {
        pipelineBrightFilter = nullptr;
        pipelineGaussianBlurHorizontal = nullptr;
        pipelineGaussianBlurVertical = nullptr;
        pipelineCombine = nullptr;
    }

    Bloom::~Bloom()
    {
    }

    void Bloom::Init()
    {
        RendererSystem *rs = CONTEXT->GetSystem<RendererSystem>();

        brightFilterRT = rs->GetRenderTarget("brightFilter");
        gaussianBlurHorizontalRT = rs->GetRenderTarget("gaussianBlurHorizontal");
        gaussianBlurVerticalRT = rs->GetRenderTarget("gaussianBlurVertical");
        displayRT = rs->GetRenderTarget("display");
        frameImage = rs->CreateFSSampledImage(false);
    }

    void Bloom::UpdatePipelineInfo()
    {
        UpdatePipelineInfoBrightFilter();
        UpdatePipelineInfoGaussianBlurHorizontal();
        UpdatePipelineInfoGaussianBlurVertical();
        UpdatePipelineInfoCombine();
    }

    void Bloom::UpdatePipelineInfoBrightFilter()
    {
        pipelineInfoBF = std::make_shared<PipelineCreateInfo>();
        PipelineCreateInfo &info = *pipelineInfoBF;

        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.hlsl", ShaderStage::VertexBit});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/Bloom/brightFilter.frag", ShaderStage::FragmentBit});
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {brightFilterRT->blendAttachment};
        info.pushConstantStage = ShaderStage::FragmentBit;
        info.pushConstantSize = 5 * sizeof(float);
        info.descriptorSetLayouts = {DSBrightFilter->GetLayout()};
        info.dynamicColorTargets = 1;
        info.colorFormats = &brightFilterRT->imageInfo.format;
        info.name = "BrightFilter_pipeline";
    }

    void Bloom::UpdatePipelineInfoGaussianBlurHorizontal()
    {
        pipelineInfoGBH = std::make_shared<PipelineCreateInfo>();
        PipelineCreateInfo &info = *pipelineInfoGBH;

        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.hlsl", ShaderStage::VertexBit});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/Bloom/gaussianBlurHorizontal.frag", ShaderStage::FragmentBit});
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {gaussianBlurHorizontalRT->blendAttachment};
        info.pushConstantStage = ShaderStage::FragmentBit;
        info.pushConstantSize = 5 * sizeof(float);
        info.descriptorSetLayouts = {DSGaussianBlurHorizontal->GetLayout()};
        info.dynamicColorTargets = 1;
        info.colorFormats = &gaussianBlurHorizontalRT->imageInfo.format;
        info.name = "GaussianBlurHorizontal_pipeline";
    }

    void Bloom::UpdatePipelineInfoGaussianBlurVertical()
    {
        pipelineInfoGBV = std::make_shared<PipelineCreateInfo>();
        PipelineCreateInfo &info = *pipelineInfoGBV;

        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.hlsl", ShaderStage::VertexBit});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/Bloom/gaussianBlurVertical.frag", ShaderStage::FragmentBit});
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {gaussianBlurVerticalRT->blendAttachment};
        info.pushConstantStage = ShaderStage::FragmentBit;
        info.pushConstantSize = 5 * sizeof(float);
        info.descriptorSetLayouts = {DSGaussianBlurVertical->GetLayout()};
        info.dynamicColorTargets = 1;
        info.colorFormats = &gaussianBlurVerticalRT->imageInfo.format;
        info.name = "GaussianBlurVertical_pipeline";
    }

    void Bloom::UpdatePipelineInfoCombine()
    {
        pipelineInfoCombine = std::make_shared<PipelineCreateInfo>();
        PipelineCreateInfo &info = *pipelineInfoCombine;

        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.hlsl", ShaderStage::VertexBit});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/Bloom/combine.frag", ShaderStage::FragmentBit});
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {displayRT->blendAttachment};
        info.pushConstantStage = ShaderStage::FragmentBit;
        info.pushConstantSize = 5 * sizeof(float);
        info.descriptorSetLayouts = {DSCombine->GetLayout()};
        info.dynamicColorTargets = 1;
        info.colorFormats = &displayRT->imageInfo.format;
        info.name = "BloomCombine_pipeline";
    }

    void Bloom::CreateUniforms(CommandBuffer *cmd)
    {
        std::vector<DescriptorBindingInfo> bindingInfos(1);
        bindingInfos[0].binding = 0;
        bindingInfos[0].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[0].type = DescriptorType::CombinedImageSampler;

        DSBrightFilter = Descriptor::Create(bindingInfos, ShaderStage::FragmentBit, "Bloom_BrightFilter_descriptor");
        DSGaussianBlurHorizontal = Descriptor::Create(bindingInfos, ShaderStage::FragmentBit, "Bloom_GaussianBlurHorizontal_descriptor");
        DSGaussianBlurVertical = Descriptor::Create(bindingInfos, ShaderStage::FragmentBit, "Bloom_GaussianBlurVertical_descriptor");

        bindingInfos.resize(2);
        bindingInfos[0].binding = 0;
        bindingInfos[0].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[0].type = DescriptorType::CombinedImageSampler;
        bindingInfos[1].binding = 1;
        bindingInfos[1].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[1].type = DescriptorType::CombinedImageSampler;
        DSCombine = Descriptor::Create(bindingInfos, ShaderStage::FragmentBit, "Bloom_Combine_descriptor");

        UpdateDescriptorSets();
    }

    void Bloom::UpdateDescriptorSets()
    {
        DSBrightFilter->SetImage(0, frameImage->GetSRV(), frameImage->sampler->Handle());
        DSBrightFilter->UpdateDescriptor();

        DSGaussianBlurHorizontal->SetImage(0, brightFilterRT->GetSRV(), brightFilterRT->sampler->Handle());
        DSGaussianBlurHorizontal->UpdateDescriptor();

        DSGaussianBlurVertical->SetImage(0, gaussianBlurHorizontalRT->GetSRV(), gaussianBlurHorizontalRT->sampler->Handle());
        DSGaussianBlurVertical->UpdateDescriptor();

        DSCombine->SetImage(0, frameImage->GetSRV(), frameImage->sampler->Handle());
        DSCombine->SetImage(1, gaussianBlurVerticalRT->GetSRV(), gaussianBlurVerticalRT->sampler->Handle());
        DSCombine->UpdateDescriptor();
    }

    void Bloom::Update(Camera *camera)
    {
    }

    void Bloom::Draw(CommandBuffer *cmd, uint32_t imageIndex)
    {
        uint32_t totalImages = static_cast<uint32_t>(SWAPCHAIN_IMAGES);

        std::vector<float> values{
            GUI::Bloom_Inv_brightness,
            GUI::Bloom_intensity,
            GUI::Bloom_range,
            GUI::Bloom_exposure,
            static_cast<float>(GUI::use_tonemap)};

        cmd->BeginDebugRegion("Bloom");

        cmd->BeginDebugRegion("BrightFilter");
        // Copy RT
        cmd->CopyImage(displayRT, frameImage);

        // BRIGHT FILTER
        // Input
        cmd->ImageBarrier(frameImage, ImageLayout::ShaderReadOnly);
        // Output
        cmd->ImageBarrier(brightFilterRT, ImageLayout::ColorAttachment);

        AttachmentInfo info{};
        info.image = brightFilterRT;
        info.initialLayout = brightFilterRT->GetCurrentLayout();
        info.finalLayout = ImageLayout::ColorAttachment;

        cmd->BeginPass(1, &info, nullptr, &pipelineInfoBF->renderPass);
        cmd->BindPipeline(*pipelineInfoBF, &pipelineBrightFilter);
        cmd->SetViewport(0.f, 0.f, brightFilterRT->width_f, brightFilterRT->height_f);
        cmd->SetScissor(0, 0, brightFilterRT->imageInfo.width, brightFilterRT->imageInfo.height);
        cmd->PushConstants(pipelineBrightFilter, ShaderStage::FragmentBit, 0,
                           uint32_t(sizeof(float) * values.size()), values.data());
        cmd->BindDescriptors(pipelineBrightFilter, 1, &DSBrightFilter);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
        cmd->EndDebugRegion();

        cmd->BeginDebugRegion("GaussianBlurHorizontal");
        // GUASSIAN BLUR HORIZONTAL
        // Input
        cmd->ImageBarrier(brightFilterRT, ImageLayout::ShaderReadOnly);
        // Output
        cmd->ImageBarrier(gaussianBlurHorizontalRT, ImageLayout::ColorAttachment);

        info.image = gaussianBlurHorizontalRT;
        info.initialLayout = gaussianBlurHorizontalRT->GetCurrentLayout();

        cmd->BeginPass(1, &info, nullptr, &pipelineInfoGBH->renderPass);
        cmd->BindPipeline(*pipelineInfoGBH, &pipelineGaussianBlurHorizontal);
        cmd->SetViewport(0.f, 0.f, gaussianBlurHorizontalRT->width_f, gaussianBlurHorizontalRT->height_f);
        cmd->SetScissor(0, 0, gaussianBlurHorizontalRT->imageInfo.width, gaussianBlurHorizontalRT->imageInfo.height);
        cmd->PushConstants(pipelineGaussianBlurHorizontal, ShaderStage::FragmentBit, 0,
                           uint32_t(sizeof(float) * values.size()), values.data());
        cmd->BindDescriptors(pipelineGaussianBlurHorizontal, 1, &DSGaussianBlurHorizontal);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
        cmd->EndDebugRegion();

        cmd->BeginDebugRegion("GaussianBlurVertical");
        // GAUSSIAN BLUR VERTICAL
        // Input
        cmd->ImageBarrier(gaussianBlurHorizontalRT, ImageLayout::ShaderReadOnly);
        // Output
        cmd->ImageBarrier(gaussianBlurVerticalRT, ImageLayout::ColorAttachment);

        info.image = gaussianBlurVerticalRT;
        info.initialLayout = gaussianBlurVerticalRT->GetCurrentLayout();

        cmd->BeginPass(1, &info, nullptr, &pipelineInfoGBV->renderPass);
        cmd->BindPipeline(*pipelineInfoGBV, &pipelineGaussianBlurVertical);
        cmd->SetViewport(0.f, 0.f, gaussianBlurVerticalRT->width_f, gaussianBlurVerticalRT->height_f);
        cmd->SetScissor(0, 0, gaussianBlurVerticalRT->imageInfo.width, gaussianBlurVerticalRT->imageInfo.height);
        cmd->PushConstants(pipelineGaussianBlurVertical, ShaderStage::FragmentBit, 0,
                           uint32_t(sizeof(float) * values.size()), values.data());
        cmd->BindDescriptors(pipelineGaussianBlurVertical, 1, &DSGaussianBlurVertical);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
        cmd->EndDebugRegion();

        cmd->BeginDebugRegion("Bloom Combine");
        // COMBINE
        // Input
        cmd->ImageBarrier(frameImage, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(gaussianBlurVerticalRT, ImageLayout::ShaderReadOnly);
        // Output
        cmd->ImageBarrier(displayRT, ImageLayout::ColorAttachment);

        info.image = displayRT;
        info.initialLayout = displayRT->GetCurrentLayout();

        cmd->BeginPass(1, &info, nullptr, &pipelineInfoCombine->renderPass);
        cmd->BindPipeline(*pipelineInfoCombine, &pipelineCombine);
        cmd->SetViewport(0.f, 0.f, displayRT->width_f, displayRT->height_f);
        cmd->SetScissor(0, 0, displayRT->imageInfo.width, displayRT->imageInfo.height);
        cmd->PushConstants(pipelineCombine, ShaderStage::FragmentBit, 0, uint32_t(sizeof(float) * values.size()),
                           values.data());
        cmd->BindDescriptors(pipelineCombine, 1, &DSCombine);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
        cmd->EndDebugRegion();

        cmd->EndDebugRegion();
    }

    void Bloom::Resize(uint32_t width, uint32_t height)
    {
        Image::Destroy(frameImage);
        Init();
        UpdateDescriptorSets();
    }

    void Bloom::Destroy()
    {
        Descriptor::Destroy(DSBrightFilter);
        Descriptor::Destroy(DSGaussianBlurHorizontal);
        Descriptor::Destroy(DSGaussianBlurVertical);
        Descriptor::Destroy(DSCombine);

        Pipeline::Destroy(pipelineBrightFilter);
        Pipeline::Destroy(pipelineGaussianBlurHorizontal);
        Pipeline::Destroy(pipelineGaussianBlurVertical);
        Pipeline::Destroy(pipelineCombine);

        Image::Destroy(frameImage);
    }
}
