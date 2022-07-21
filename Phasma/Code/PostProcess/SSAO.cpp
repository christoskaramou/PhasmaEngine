#include "SSAO.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Surface.h"
#include "GUI/GUI.h"
#include "Shader/Shader.h"
#include "Renderer/RHI.h"
#include "Renderer/Command.h"
#include "Renderer/Queue.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Image.h"
#include "Renderer/Pipeline.h"
#include "Renderer/Buffer.h"
#include "Systems/CameraSystem.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    SSAO::SSAO()
    {
        pipeline = nullptr;
        pipelineBlur = nullptr;
    }

    SSAO::~SSAO()
    {

    }

    void SSAO::Init()
    {
        RendererSystem *rs = CONTEXT->GetSystem<RendererSystem>();

        ssaoRT = rs->GetRenderTarget("ssao");
        ssaoBlurRT = rs->GetRenderTarget("ssaoBlur");
        normalRT = rs->GetRenderTarget("normal");
        depth = rs->GetDepthTarget("depth");
    }

    void SSAO::UpdatePipelineInfo()
    {
        UpdatePipelineInfoSSAO();
        UpdatePipelineInfoBlur();
    }

    void SSAO::UpdatePipelineInfoSSAO()
    {
        pipelineInfo = std::make_shared<PipelineCreateInfo>();
        PipelineCreateInfo &info = *pipelineInfo;

        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.vert", ShaderStage::VertexBit});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/SSAO/ssao.frag", ShaderStage::FragmentBit});
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {ssaoRT->blendAttachment};
        info.descriptorSetLayouts = {DSet->GetLayout()};
        info.dynamicColorTargets = 1;
        info.colorFormats = &ssaoRT->imageInfo.format;
        info.name = "ssao_pipeline";
    }

    void SSAO::UpdatePipelineInfoBlur()
    {
        pipelineInfoBlur = std::make_shared<PipelineCreateInfo>();
        PipelineCreateInfo &info = *pipelineInfoBlur;

        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.vert", ShaderStage::VertexBit});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/SSAO/ssaoBlur.frag", ShaderStage::FragmentBit});
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {ssaoBlurRT->blendAttachment};
        info.descriptorSetLayouts = {DSBlur->GetLayout()};
        info.dynamicColorTargets = 1;
        info.colorFormats = &ssaoBlurRT->imageInfo.format;
        info.name = "ssaoBlur_pipeline";
    }

    void SSAO::CreateUniforms(CommandBuffer *cmd)
    {
        // kernel buffer
        std::vector<vec4> kernel{};
        for (unsigned i = 0; i < 16; i++)
        {
            vec3 sample(rand(-1.f, 1.f), rand(-1.f, 1.f), rand(0.f, 1.f));
            sample = normalize(sample);
            sample *= rand(0.f, 1.f);
            float scale = (float)i / 16.f;
            scale = mix(.1f, 1.f, scale * scale);
            kernel.emplace_back(sample * scale, 0.f);
        }

        UB_Kernel = Buffer::Create(
            RHII.AlignUniform(kernel.size() * sizeof(vec4)),
            BufferUsage::UniformBufferBit,
            AllocationCreate::HostAccessRandomBit,
            "ssao_UB_Kernel_buffer");

        MemoryRange mr{};
        mr.data = kernel.data();
        mr.size = kernel.size() * sizeof(vec4);
        mr.offset = 0;
        UB_Kernel->Copy(1, &mr, false);

        // noise image
        std::vector<vec4> noise{};
        for (unsigned int i = 0; i < 16; i++)
            noise.emplace_back(rand(-1.f, 1.f), rand(-1.f, 1.f), 0.f, 1.f);

        ImageCreateInfo imageInfo{};
        imageInfo.format = Format::RGBA16SFloat;
        imageInfo.width = 4;
        imageInfo.height = 4;
        imageInfo.usage = ImageUsage::TransferSrcBit | ImageUsage::TransferDstBit | ImageUsage::SampledBit;
        imageInfo.properties = MemoryProperty::DeviceLocalBit;
        imageInfo.initialLayout = ImageLayout::Preinitialized;
        imageInfo.name = "ssao_noise_image";
        noiseTex = Image::Create(imageInfo);

        ImageViewCreateInfo viewInfo{};
        viewInfo.image = noiseTex;
        noiseTex->CreateImageView(viewInfo);

        SamplerCreateInfo samplerInfo{};
        samplerInfo.minFilter = Filter::Nearest;
        samplerInfo.magFilter = Filter::Nearest;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;
        samplerInfo.maxAnisotropy = 1.0f;
        noiseTex->CreateSampler(samplerInfo);

        cmd->CopyDataToImageStaged(noiseTex, noise.data(), 16 * sizeof(vec4));

        // pvm uniform
        UB_PVM = Buffer::Create(
            RHII.AlignUniform(3 * sizeof(mat4)) * SWAPCHAIN_IMAGES,
            BufferUsage::UniformBufferBit,
            AllocationCreate::HostAccessRandomBit,
            "ssao_UB_PVM_buffer");
        UB_PVM->Map();
        UB_PVM->Zero();
        UB_PVM->Flush();
        UB_PVM->Unmap();

        // DESCRIPTOR SET FOR SSAO
        DescriptorBindingInfo bindingInfos[5]{};

        bindingInfos[0].binding = 0;
        bindingInfos[0].type = DescriptorType::CombinedImageSampler;
        bindingInfos[0].imageLayout = ImageLayout::DepthStencilReadOnly;
        bindingInfos[0].pImage = depth;

        bindingInfos[1].binding = 1;
        bindingInfos[1].type = DescriptorType::CombinedImageSampler;
        bindingInfos[1].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[1].pImage = normalRT;

        bindingInfos[2].binding = 2;
        bindingInfos[2].type = DescriptorType::CombinedImageSampler;
        bindingInfos[2].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[2].pImage = noiseTex;

        bindingInfos[3].binding = 3;
        bindingInfos[3].type = DescriptorType::UniformBuffer;
        bindingInfos[3].pBuffer = UB_Kernel;

        bindingInfos[4].binding = 4;
        bindingInfos[4].type = DescriptorType::UniformBufferDynamic;
        bindingInfos[4].pBuffer = UB_PVM;

        DescriptorInfo info{};
        info.count = 5;
        info.bindingInfos = bindingInfos;
        info.stage = ShaderStage::FragmentBit;

        DSet = Descriptor::Create(&info, "ssao_descriptor");

        // DESCRIPTOR SET FOR SSAO BLUR
        bindingInfos[0].binding = 0;
        bindingInfos[0].type = DescriptorType::CombinedImageSampler;
        bindingInfos[0].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[0].pImage = ssaoRT;

        info.count = 1;

        DSBlur = Descriptor::Create(&info, "ssao_blur_descriptor");
    }

    void SSAO::UpdateDescriptorSets()
    {
        DescriptorBindingInfo bindingInfos[5]{};

        bindingInfos[0].binding = 0;
        bindingInfos[0].type = DescriptorType::CombinedImageSampler;
        bindingInfos[0].imageLayout = ImageLayout::DepthStencilReadOnly;
        bindingInfos[0].pImage = depth;

        bindingInfos[1].binding = 1;
        bindingInfos[1].type = DescriptorType::CombinedImageSampler;
        bindingInfos[1].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[1].pImage = normalRT;

        bindingInfos[2].binding = 2;
        bindingInfos[2].type = DescriptorType::CombinedImageSampler;
        bindingInfos[2].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[2].pImage = noiseTex;

        bindingInfos[3].binding = 3;
        bindingInfos[3].type = DescriptorType::UniformBuffer;
        bindingInfos[3].pBuffer = UB_Kernel;

        bindingInfos[4].binding = 4;
        bindingInfos[4].type = DescriptorType::UniformBufferDynamic;
        bindingInfos[4].pBuffer = UB_PVM;

        DescriptorInfo info{};
        info.count = 5;
        info.bindingInfos = bindingInfos;
        info.stage = ShaderStage::FragmentBit;

        DSet->UpdateDescriptor(&info);

        // DESCRIPTOR SET FOR SSAO BLUR
        bindingInfos[0].binding = 0;
        bindingInfos[0].type = DescriptorType::CombinedImageSampler;
        bindingInfos[0].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[0].pImage = ssaoRT;

        info.count = 1;

        DSBlur->UpdateDescriptor(&info);
    }

    void SSAO::Update(Camera *camera)
    {
        if (GUI::show_ssao)
        {
            ubo.projection = camera->projection;
            ubo.view = camera->view;
            ubo.invProjection = camera->invProjection;

            MemoryRange mr{};
            mr.data = &ubo;
            mr.size = sizeof(UBO);
            mr.offset = RHII.GetFrameDynamicOffset(UB_PVM->Size(), RHII.GetFrameIndex());
            UB_PVM->Copy(1, &mr, false);
        }
    }

    void SSAO::Draw(CommandBuffer *cmd, uint32_t imageIndex)
    {
        cmd->BeginDebugRegion("SSAO");
        // SSAO
        // Input
        cmd->ImageBarrier(normalRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(noiseTex, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(depth, ImageLayout::DepthStencilReadOnly);
        // Output
        cmd->ImageBarrier(ssaoRT, ImageLayout::ColorAttachment);

        AttachmentInfo info{};
        info.image = ssaoRT;
        info.initialLayout = ssaoRT->GetCurrentLayout();
        info.finalLayout = ImageLayout::ColorAttachment;

        cmd->BeginPass(1, &info, nullptr, &pipelineInfo->renderPass);
        cmd->BindPipeline(*pipelineInfo, &pipeline);
        cmd->SetViewport(0.f, 0.f, ssaoRT->width_f, ssaoRT->height_f);
        cmd->SetScissor(0, 0, ssaoRT->imageInfo.width, ssaoRT->imageInfo.height);
        cmd->BindDescriptors(pipeline, 1, &DSet);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
        cmd->EndDebugRegion();

        cmd->BeginDebugRegion("SSAO Blur");
        // SSAO BLUR
        // Input
        cmd->ImageBarrier(ssaoRT, ImageLayout::ShaderReadOnly);
        // Output
        cmd->ImageBarrier(ssaoBlurRT, ImageLayout::ColorAttachment);

        info.image = ssaoBlurRT;
        info.initialLayout = ssaoBlurRT->GetCurrentLayout();

        cmd->BeginPass(1, &info, nullptr, &pipelineInfoBlur->renderPass);
        cmd->BindPipeline(*pipelineInfoBlur, &pipelineBlur);
        cmd->SetViewport(0.f, 0.f, ssaoBlurRT->width_f, ssaoBlurRT->height_f);
        cmd->SetScissor(0, 0, ssaoBlurRT->imageInfo.width, ssaoBlurRT->imageInfo.height);
        cmd->BindDescriptors(pipelineBlur, 1, &DSBlur);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
        cmd->EndDebugRegion();
    }

    void SSAO::Resize(uint32_t width, uint32_t height)
    {
        Init();
        UpdateDescriptorSets();
    }

    void SSAO::Destroy()
    {
        Descriptor::Destroy(DSet);
        Descriptor::Destroy(DSBlur);

        Pipeline::Destroy(pipeline);
        Pipeline::Destroy(pipelineBlur);

        Buffer::Destroy(UB_Kernel);
        Buffer::Destroy(UB_PVM);
        Image::Destroy(noiseTex);
    }
}
