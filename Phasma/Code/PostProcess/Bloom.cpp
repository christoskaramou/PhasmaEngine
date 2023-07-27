#include "Bloom.h"
#include "GUI/GUI.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Surface.h"
#include "Shader/Shader.h"
#include "Renderer/RHI.h"
#include "Renderer/Command.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Image.h"
#include "Renderer/Pipeline.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    Bloom::Bloom()
    {
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

    void Bloom::UpdatePassInfo()
    {
        UpdatePassInfoBrightFilter();
        UpdatePassInfoGaussianBlurHorizontal();
        UpdatePassInfoGaussianBlurVertical();
        UpdatePassInfoCombine();
    }

    void Bloom::UpdatePassInfoBrightFilter()
    {
        passInfoBF = std::make_shared<PassInfo>();
        PassInfo &info = *passInfoBF;

        info.name = "BrightFilter_pipeline";
        info.pVertShader = Shader::Create("Shaders/Common/quad.hlsl", ShaderStage::VertexBit);
        info.pFragShader = Shader::Create("Shaders/Bloom/brightFilterPS.hlsl", ShaderStage::FragmentBit);
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {brightFilterRT->blendAttachment};
        info.pushConstantStage = ShaderStage::FragmentBit;
        info.pushConstantSize = 5 * sizeof(float);
        info.descriptorSetLayouts = {DSBrightFilter->GetLayout()};
        info.colorFormats = &brightFilterRT->imageInfo.format;

        AttachmentInfo colorInfo{};
        colorInfo.image = brightFilterRT;
        colorInfo.initialLayout = ImageLayout::ColorAttachment;
        colorInfo.finalLayout = ImageLayout::ColorAttachment;
        info.renderPass = CommandBuffer::GetRenderPass(1, &colorInfo, nullptr);

        info.UpdateHash();
    }

    void Bloom::UpdatePassInfoGaussianBlurHorizontal()
    {
        passInfoGBH = std::make_shared<PassInfo>();
        PassInfo &info = *passInfoGBH;

        info.name = "GaussianBlurHorizontal_pipeline";
        info.pVertShader = Shader::Create("Shaders/Common/quad.hlsl", ShaderStage::VertexBit);
        info.pFragShader = Shader::Create("Shaders/Bloom/gaussianBlurHorizontalPS.hlsl", ShaderStage::FragmentBit);
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {gaussianBlurHorizontalRT->blendAttachment};
        info.pushConstantStage = ShaderStage::FragmentBit;
        info.pushConstantSize = 5 * sizeof(float);
        info.descriptorSetLayouts = {DSGaussianBlurHorizontal->GetLayout()};
        info.colorFormats = &gaussianBlurHorizontalRT->imageInfo.format;

        AttachmentInfo colorInfo{};
        colorInfo.image = gaussianBlurHorizontalRT;
        colorInfo.initialLayout = ImageLayout::ColorAttachment;
        colorInfo.finalLayout = ImageLayout::ColorAttachment;
        info.renderPass = CommandBuffer::GetRenderPass(1, &colorInfo, nullptr);

        info.UpdateHash();
    }

    void Bloom::UpdatePassInfoGaussianBlurVertical()
    {
        passInfoGBV = std::make_shared<PassInfo>();
        PassInfo &info = *passInfoGBV;

        info.name = "GaussianBlurVertical_pipeline";
        info.pVertShader = Shader::Create("Shaders/Common/quad.hlsl", ShaderStage::VertexBit);
        info.pFragShader = Shader::Create("Shaders/Bloom/gaussianBlurVerticalPS.hlsl", ShaderStage::FragmentBit);
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {gaussianBlurVerticalRT->blendAttachment};
        info.pushConstantStage = ShaderStage::FragmentBit;
        info.pushConstantSize = 5 * sizeof(float);
        info.descriptorSetLayouts = {DSGaussianBlurVertical->GetLayout()};
        info.colorFormats = &gaussianBlurVerticalRT->imageInfo.format;

        AttachmentInfo colorInfo{};
        colorInfo.image = gaussianBlurVerticalRT;
        colorInfo.initialLayout = ImageLayout::ColorAttachment;
        colorInfo.finalLayout = ImageLayout::ColorAttachment;
        info.renderPass = CommandBuffer::GetRenderPass(1, &colorInfo, nullptr);

        info.UpdateHash();
    }

    void Bloom::UpdatePassInfoCombine()
    {
        passInfoCombine = std::make_shared<PassInfo>();
        PassInfo &info = *passInfoCombine;

        info.name = "BloomCombine_pipeline";
        info.pVertShader = Shader::Create("Shaders/Common/quad.hlsl", ShaderStage::VertexBit);
        info.pFragShader = Shader::Create("Shaders/Bloom/combinePS.hlsl", ShaderStage::FragmentBit);
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {displayRT->blendAttachment};
        info.pushConstantStage = ShaderStage::FragmentBit;
        info.pushConstantSize = 5 * sizeof(float);
        info.descriptorSetLayouts = {DSCombine->GetLayout()};
        info.colorFormats = &displayRT->imageInfo.format;

        AttachmentInfo colorInfo{};
        colorInfo.image = displayRT;
        colorInfo.initialLayout = ImageLayout::ColorAttachment;
        colorInfo.finalLayout = ImageLayout::ColorAttachment;
        info.renderPass = CommandBuffer::GetRenderPass(1, &colorInfo, nullptr);

        info.UpdateHash();
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
        DSBrightFilter->Update();

        DSGaussianBlurHorizontal->SetImage(0, brightFilterRT->GetSRV(), brightFilterRT->sampler->Handle());
        DSGaussianBlurHorizontal->Update();

        DSGaussianBlurVertical->SetImage(0, gaussianBlurHorizontalRT->GetSRV(), gaussianBlurHorizontalRT->sampler->Handle());
        DSGaussianBlurVertical->Update();

        DSCombine->SetImage(0, frameImage->GetSRV(), frameImage->sampler->Handle());
        DSCombine->SetImage(1, gaussianBlurVerticalRT->GetSRV(), gaussianBlurVerticalRT->sampler->Handle());
        DSCombine->Update();
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

        cmd->BeginPass(passInfoBF->renderPass, &brightFilterRT, nullptr);
        cmd->BindPipeline(*passInfoBF);
        cmd->SetViewport(0.f, 0.f, brightFilterRT->width_f, brightFilterRT->height_f);
        cmd->SetScissor(0, 0, brightFilterRT->imageInfo.width, brightFilterRT->imageInfo.height);
        cmd->PushConstants(ShaderStage::FragmentBit, 0, uint32_t(sizeof(float) * values.size()), values.data());
        cmd->BindDescriptors(1, &DSBrightFilter);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
        cmd->EndDebugRegion();

        cmd->BeginDebugRegion("GaussianBlurHorizontal");
        // GUASSIAN BLUR HORIZONTAL
        // Input
        cmd->ImageBarrier(brightFilterRT, ImageLayout::ShaderReadOnly);

        cmd->BeginPass(passInfoGBH->renderPass, &gaussianBlurHorizontalRT, nullptr);
        cmd->BindPipeline(*passInfoGBH);
        cmd->SetViewport(0.f, 0.f, gaussianBlurHorizontalRT->width_f, gaussianBlurHorizontalRT->height_f);
        cmd->SetScissor(0, 0, gaussianBlurHorizontalRT->imageInfo.width, gaussianBlurHorizontalRT->imageInfo.height);
        cmd->PushConstants(ShaderStage::FragmentBit, 0, uint32_t(sizeof(float) * values.size()), values.data());
        cmd->BindDescriptors(1, &DSGaussianBlurHorizontal);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
        cmd->EndDebugRegion();

        cmd->BeginDebugRegion("GaussianBlurVertical");
        // GAUSSIAN BLUR VERTICAL
        // Input
        cmd->ImageBarrier(gaussianBlurHorizontalRT, ImageLayout::ShaderReadOnly);

        cmd->BeginPass(passInfoGBV->renderPass, &gaussianBlurVerticalRT, nullptr);
        cmd->BindPipeline(*passInfoGBV);
        cmd->SetViewport(0.f, 0.f, gaussianBlurVerticalRT->width_f, gaussianBlurVerticalRT->height_f);
        cmd->SetScissor(0, 0, gaussianBlurVerticalRT->imageInfo.width, gaussianBlurVerticalRT->imageInfo.height);
        cmd->PushConstants(ShaderStage::FragmentBit, 0, uint32_t(sizeof(float) * values.size()), values.data());
        cmd->BindDescriptors(1, &DSGaussianBlurVertical);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
        cmd->EndDebugRegion();

        cmd->BeginDebugRegion("Bloom Combine");
        // COMBINE
        // Input
        cmd->ImageBarrier(frameImage, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(gaussianBlurVerticalRT, ImageLayout::ShaderReadOnly);

        cmd->BeginPass(passInfoCombine->renderPass, &displayRT, nullptr);
        cmd->BindPipeline(*passInfoCombine);
        cmd->SetViewport(0.f, 0.f, displayRT->width_f, displayRT->height_f);
        cmd->SetScissor(0, 0, displayRT->imageInfo.width, displayRT->imageInfo.height);
        cmd->PushConstants(ShaderStage::FragmentBit, 0, uint32_t(sizeof(float) * values.size()), values.data());
        cmd->BindDescriptors(1, &DSCombine);
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

        Image::Destroy(frameImage);
    }
}
