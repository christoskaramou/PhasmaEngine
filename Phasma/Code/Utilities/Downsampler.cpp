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
        UpdatePassInfo();
        CreateUniforms();
    }

    void Downsampler::Dispatch(CommandBuffer *cmd, Image *image)
    {
        std::lock_guard<std::mutex> guard(s_dispatchMutex);

        SetInputImage(image);
        UpdateDescriptorSet();

        uvec2 groupCount = SpdSetup();

        ImageBarrierInfo barrier{};
        barrier.image = s_image;
        barrier.layout = ImageLayout::General;
        barrier.stageFlags = PipelineStage::ComputeShaderBit;
        barrier.accessMask = Access::ShaderWriteBit;

        cmd->BeginDebugRegion("Downsampler::Dispatch Command_" + std::to_string(s_currentIndex));
        cmd->ImageBarrier(barrier);
        cmd->BindPipeline(*s_passInfo, false);
        cmd->BindDescriptors(1, &s_DSet[s_currentIndex]);
        cmd->SetConstant(0, s_pushConstants);
        cmd->PushConstants();
        cmd->Dispatch(groupCount.x, groupCount.y, s_image->GetArrayLayers());
        cmd->EndDebugRegion();

        s_image = nullptr;
        s_currentIndex = (s_currentIndex + 1) % MAX_DESCRIPTORS_PER_CMD;
    }

    void Downsampler::Destroy()
    {
        for (uint32_t i = 0; i < MAX_DESCRIPTORS_PER_CMD; i++)
        {
            Descriptor::Destroy(s_DSet[i]);
            s_DSet[i] = nullptr;
            Buffer::Destroy(s_atomicCounter[i]);
            s_atomicCounter[i] = nullptr;
        }
    }

    void Downsampler::UpdatePassInfo()
    {
        // SPD defines
        const std::vector<Define> defines{
            Define{"A_GPU", ""},
            Define{"A_HLSL", ""},
            // Define{"SPD_LINEAR_SAMPLER", ""},
            Define{"SPD_NO_WAVE_OPERATIONS", ""}};

        s_passInfo = std::make_shared<PassInfo>();
        s_passInfo->pCompShader = Shader::Create("Shaders/Compute/spd/spd.hlsl", ShaderStage::ComputeBit, defines);
        s_passInfo->name = "Downsample_pipeline";
        
        s_passInfo->UpdateHash();
    }

    void Downsampler::CreateUniforms()
    {
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

        for (uint32_t i = 0; i < MAX_DESCRIPTORS_PER_CMD; i++)
        {
            s_atomicCounter[i] = Buffer::Create(
                sizeof(s_counter),
                BufferUsage::StorageBufferBit,
                AllocationCreate::HostAccessSequentialWriteBit,
                "Downsample_storage_buffer_" + std::to_string(i));

            s_DSet[i] = Descriptor::Create(bindingInfos, ShaderStage::ComputeBit, false, "Downsample_descriptor_" + std::to_string(i));
        }
    }

    void Downsampler::SetInputImage(Image *image)
    {
        uint32_t mips = image->GetMipLevels();

        PE_ERROR_IF(mips <= 1, "Image has no extra mips!");

        for (uint32_t i = 0; i < mips; i++)
        {
            if (!image->HasUAV(i))
                image->CreateUAV(ImageViewType::Type2DArray, i);
        }

        s_image = image;
    }

    void Downsampler::UpdateDescriptorSet()
    {
        int mips = static_cast<int>(s_image->GetMipLevels());
        std::vector<ImageViewHandle> views(mips);
        for (int i = 0; i < mips; i++)
            views[i] = s_image->GetUAV(i);

        Descriptor &dSet = *s_DSet[s_currentIndex];
        dSet.SetImageViews(0, views, {});
        if (mips >= 6)
            dSet.SetImageView(1, s_image->GetUAV(6), {});
        dSet.SetBuffer(2, s_atomicCounter[s_currentIndex]);

        dSet.Update();
    }

    uvec2 Downsampler::SpdSetup()
    {
        const Rect2Du rectInfo{0, 0, s_image->GetWidth(), s_image->GetHeight()};

        s_pushConstants.workGroupOffset.x = rectInfo.x / 64;
        s_pushConstants.workGroupOffset.y = rectInfo.y / 64;

        const uint32_t endIndexX = (rectInfo.x + rectInfo.width - 1) / 64;
        const uint32_t endIndexY = (rectInfo.y + rectInfo.height - 1) / 64;

        uvec2 dispatchThreadGroupCount(endIndexX + 1 - s_pushConstants.workGroupOffset.x,
                                       endIndexY + 1 - s_pushConstants.workGroupOffset.y);

        s_pushConstants.numWorkGroupsPerSlice = dispatchThreadGroupCount.x * dispatchThreadGroupCount.y;
        s_pushConstants.mips = s_image->GetMipLevels() - 1;

        return dispatchThreadGroupCount;
    }
}
