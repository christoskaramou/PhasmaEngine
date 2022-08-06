#include "SSR.h"
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
#include "Renderer/Buffer.h"
#include "Systems/CameraSystem.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    SSR::SSR()
    {
        DSet = {};
        pipeline = nullptr;
    }

    SSR::~SSR()
    {
    }

    void SSR::Init()
    {
        RendererSystem *rs = CONTEXT->GetSystem<RendererSystem>();
        Deferred *deferred = WORLD_ENTITY->GetComponent<Deferred>();

        ssrRT = rs->GetRenderTarget("ssr");
        albedoRT = rs->GetRenderTarget("albedo");
        normalRT = rs->GetRenderTarget("normal");
        srmRT = rs->GetRenderTarget("srm");
        depth = rs->GetDepthTarget("depth");
    }

    void SSR::UpdatePipelineInfo()
    {
        pipelineInfo = std::make_shared<PipelineCreateInfo>();
        PipelineCreateInfo &info = *pipelineInfo;

        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.hlsl", ShaderStage::VertexBit});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/SSR/ssr.frag", ShaderStage::FragmentBit});
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.cullMode = CullMode::Back;
        info.colorBlendAttachments = {ssrRT->blendAttachment};
        info.descriptorSetLayouts = {DSet->GetLayout()};
        info.dynamicColorTargets = 1;
        info.colorFormats = &ssrRT->imageInfo.format;
        info.name = "ssr_pipeline";
    }

    void SSR::CreateUniforms(CommandBuffer *cmd)
    {
        UBReflection = Buffer::Create(
            RHII.AlignUniform(4 * sizeof(mat4)) * SWAPCHAIN_IMAGES,
            BufferUsage::UniformBufferBit,
            AllocationCreate::HostAccessSequentialWriteBit,
            "SSR_UB_Reflection_buffer");
        UBReflection->Map();
        UBReflection->Zero();
        UBReflection->Flush();
        UBReflection->Unmap();

        std::vector<DescriptorBindingInfo> bindingInfos(5);
        bindingInfos[0].binding = 0;
        bindingInfos[0].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[0].type = DescriptorType::CombinedImageSampler;
        bindingInfos[1].binding = 1;
        bindingInfos[1].imageLayout = ImageLayout::DepthStencilReadOnly;
        bindingInfos[1].type = DescriptorType::CombinedImageSampler;
        bindingInfos[2].binding = 2;
        bindingInfos[2].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[2].type = DescriptorType::CombinedImageSampler;
        bindingInfos[3].binding = 3;
        bindingInfos[3].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[3].type = DescriptorType::CombinedImageSampler;
        bindingInfos[4].binding = 4;
        bindingInfos[4].type = DescriptorType::UniformBufferDynamic;
        DSet = Descriptor::Create(bindingInfos, ShaderStage::FragmentBit, "SSR_descriptor");

        UpdateDescriptorSets();
    }

    void SSR::UpdateDescriptorSets()
    {
        DSet->SetImage(0, albedoRT->GetSRV(), albedoRT->sampler);
        DSet->SetImage(1, depth->GetSRV(), depth->sampler);
        DSet->SetImage(2, normalRT->GetSRV(), normalRT->sampler);
        DSet->SetImage(3, srmRT->GetSRV(), srmRT->sampler);
        DSet->SetBuffer(4, UBReflection);
        DSet->UpdateDescriptor();
    }

    void SSR::Update(Camera *camera)
    {
        if (GUI::show_ssr)
        {
            reflectionInput[0][0] = vec4(camera->position, 1.0f);
            reflectionInput[0][1] = vec4(camera->front, 1.0f);
            reflectionInput[0][2] = vec4();
            reflectionInput[0][3] = vec4();
            reflectionInput[1] = camera->projection;
            reflectionInput[2] = camera->view;
            reflectionInput[3] = camera->invProjection;

            MemoryRange mr{};
            mr.data = &reflectionInput;
            mr.size = sizeof(reflectionInput);
            mr.offset = RHII.GetFrameDynamicOffset(UBReflection->Size(), RHII.GetFrameIndex());
            UBReflection->Copy(1, &mr, false);
        }
    }

    void SSR::Draw(CommandBuffer *cmd, uint32_t imageIndex)
    {
        cmd->BeginDebugRegion("SSR");

        // SCREEN SPACE REFLECTION
        // Input
        cmd->ImageBarrier(albedoRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(normalRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(srmRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(depth, ImageLayout::DepthStencilReadOnly);
        // Output
        cmd->ImageBarrier(ssrRT, ImageLayout::ColorAttachment);

        AttachmentInfo info{};
        info.image = ssrRT;
        info.initialLayout = ssrRT->GetCurrentLayout();
        info.finalLayout = ImageLayout::ColorAttachment;

        cmd->BeginPass(1, &info, nullptr, &pipelineInfo->renderPass);
        cmd->BindPipeline(*pipelineInfo, &pipeline);
        cmd->SetViewport(0.f, 0.f, ssrRT->width_f, ssrRT->height_f);
        cmd->SetScissor(0, 0, ssrRT->imageInfo.width, ssrRT->imageInfo.height);
        cmd->BindDescriptors(pipeline, 1, &DSet);
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();

        cmd->ImageBarrier(ssrRT, ImageLayout::ShaderReadOnly);

        cmd->EndDebugRegion();
    }

    void SSR::Resize(uint32_t width, uint32_t height)
    {
        Init();
        UpdateDescriptorSets();
    }

    void SSR::Destroy()
    {
        Descriptor::Destroy(DSet);
        Pipeline::Destroy(pipeline);
        Buffer::Destroy(UBReflection);
    }
}