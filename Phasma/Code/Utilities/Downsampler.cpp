#include "Utilities/Downsampler.h"
#include "Shader/Shader.h"
#include "Renderer/RHI.h"
#include "Renderer/Command.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Semaphore.h"
#include "Renderer/Image.h"
#include "Renderer/Buffer.h"
#include "Renderer/Queue.h"

namespace pe
{
    void Downsampler::Init()
    {
        s_pipeline = nullptr;
        s_pipelineInfo = {};
        s_DSet = {};
        s_image = nullptr;
        memset(s_counter, 0, 6 * sizeof(uint32_t));
        s_pushConstants = {};

        CreateUniforms();
        UpdatePipelineInfo();
    }

    void Downsampler::Dispatch(CommandBuffer *cmd, Image *image)
    {
        std::lock_guard<std::mutex> guard(s_dispatchMutex);

        cmd->BeginDebugRegion("Downsampler::Dispatch Command");

        SetInputImage(image);
        UpdateDescriptorSet();

        uvec2 groupCount = SpdSetup();

        cmd->ImageBarrier(s_image, ImageLayout::General);

        cmd->BindPipeline(*s_pipelineInfo, &s_pipeline);
        cmd->BindComputeDescriptors(s_pipeline, 1, &s_DSet);
        cmd->PushConstants(s_pipeline, ShaderStage::ComputeBit, 0, sizeof(PushConstants), &s_pushConstants);
        cmd->Dispatch(groupCount.x, groupCount.y, s_image->imageInfo.arrayLayers);

        ResetInputImage();

        cmd->EndDebugRegion();
    }

    void Downsampler::Destroy()
    {
        Descriptor::Destroy(s_DSet);
        Buffer::Destroy(s_atomicCounter);
    }

    void Downsampler::UpdatePipelineInfo()
    {
        // SPD defines
        const std::vector<Define> defines{
            Define{"A_GPU", ""},
            Define{"A_GLSL", ""},
            // Define{"SPD_LINEAR_SAMPLER", ""},
            Define{"SPD_NO_WAVE_OPERATIONS", ""}};

        s_pipelineInfo = std::make_shared<PipelineCreateInfo>();
        s_pipelineInfo->pCompShader = Shader::Create(ShaderInfo{"Shaders/Compute/spd/spd.comp", ShaderStage::ComputeBit, defines});
        s_pipelineInfo->descriptorSetLayouts = {s_DSet->GetLayout()};
        s_pipelineInfo->pushConstantSize = sizeof(PushConstants);
        s_pipelineInfo->pushConstantStage = ShaderStage::ComputeBit;
        s_pipelineInfo->name = "Downsample_pipeline";
    }

    void Downsampler::CreateUniforms()
    {
        s_atomicCounter = Buffer::Create(
            sizeof(s_counter),
            BufferUsage::StorageBufferBit,
            AllocationCreate::HostAccessSequentialWriteBit,
            "Downsample_storage_buffer");

        std::vector<DescriptorBindingInfo> bindingInfos(3);
        bindingInfos[0].binding = 0;
        bindingInfos[0].type = DescriptorType::StorageImage;
        bindingInfos[0].imageLayout = ImageLayout::General;
        bindingInfos[0].count = 13;

        bindingInfos[1].binding = 1;
        bindingInfos[1].type = DescriptorType::StorageImage;
        bindingInfos[1].imageLayout = ImageLayout::General;

        bindingInfos[2].binding = 2;
        bindingInfos[2].type = DescriptorType::StorageBuffer;

        s_DSet = Descriptor::Create(bindingInfos, ShaderStage::ComputeBit, "Downsample_descriptor");
    }

    void Downsampler::SetInputImage(Image *image)
    {
        uint32_t mips = image->imageInfo.mipLevels;

        if (mips <= 1)
            PE_ERROR("Image has no mips!");

        for (uint32_t i = 0; i < mips; i++)
        {
            if (!image->HasUAV(i))
                image->CreateUAV(ImageViewType::Type2DArray, i);
        }

        s_image = image;
    }

    void Downsampler::ResetInputImage()
    {
        s_image = nullptr;
    }

    void Downsampler::UpdateDescriptorSet()
    {
        int mips = static_cast<int>(s_image->imageInfo.mipLevels);
        std::vector<ImageViewHandle> views(mips);
        for (int i = 0; i < mips; i++)
            views[i] = s_image->GetUAV(i);

        s_DSet->SetImages(0, views, {});
        if (mips >= 6)
            s_DSet->SetImage(1, s_image->GetUAV(6), {});
        s_DSet->SetBuffer(2, s_atomicCounter);

        s_DSet->UpdateDescriptor();
    }

    uvec2 Downsampler::SpdSetup()
    {
        const Rect2Du rectInfo{0, 0, s_image->imageInfo.width, s_image->imageInfo.height};

        s_pushConstants.workGroupOffset.x = rectInfo.x / 64;
        s_pushConstants.workGroupOffset.y = rectInfo.y / 64;

        const uint32_t endIndexX = (rectInfo.x + rectInfo.width - 1) / 64;
        const uint32_t endIndexY = (rectInfo.y + rectInfo.height - 1) / 64;

        uvec2 dispatchThreadGroupCount(endIndexX + 1 - s_pushConstants.workGroupOffset.x,
                                       endIndexY + 1 - s_pushConstants.workGroupOffset.y);

        s_pushConstants.numWorkGroupsPerSlice = dispatchThreadGroupCount.x * dispatchThreadGroupCount.y;
        s_pushConstants.mips = s_image->imageInfo.mipLevels - 1;

        return dispatchThreadGroupCount;
    }
}
