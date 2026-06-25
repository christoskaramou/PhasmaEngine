#include "API/Downsampler/Downsampler.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/Shader.h"

namespace pe
{
    namespace
    {
#include "API/Downsampler/DownsamplerShaders.inl"

        static_assert(kDownsamplerSpirv_len == sizeof(kDownsamplerSpirv));
        static_assert(kDownsamplerDxil_len == sizeof(kDownsamplerDxil));
    } // namespace

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
        barrier.layout = PE_IMAGE_LAYOUT_GENERAL;
        barrier.stageFlags = PE_STAGE_COMPUTE_SHADER;
        barrier.accessMask = PE_ACCESS_SHADER_STORAGE_WRITE;

        cmd->BeginDebugRegion("Downsampler::Dispatch Command_" + std::to_string(s_currentIndex));
        cmd->FillBuffer(s_atomicCounter[s_currentIndex], 0, sizeof(s_counter), 0);

        BufferBarrierInfo counterBarrier{};
        counterBarrier.buffer = s_atomicCounter[s_currentIndex];
        counterBarrier.stageMask = PE_STAGE_COMPUTE_SHADER;
        counterBarrier.accessMask = PE_ACCESS_SHADER_STORAGE_READ | PE_ACCESS_SHADER_STORAGE_WRITE;
        counterBarrier.size = PE_WHOLE_SIZE;
        cmd->BufferBarrier(counterBarrier);

        cmd->ImageBarrier(barrier);
        cmd->BindPipeline(*s_passInfo, false);
        cmd->BindDescriptors(1, &s_DSet[s_currentIndex]);
        cmd->SetConstants(s_pushConstants);
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
            Buffer::Destroy(s_atomicCounter[i]);
        }

        s_passInfo.reset();
    }

    void Downsampler::UpdatePassInfo()
    {
        s_passInfo = std::make_shared<PassInfo>();
        s_passInfo->pCompShader = Shader::CreateFromBytecode({
            .spirv = kDownsamplerSpirv,
            .spirvSizeBytes = sizeof(kDownsamplerSpirv),
            .dxil = kDownsamplerDxil,
            .dxilSizeBytes = sizeof(kDownsamplerDxil),
            .stage = PE_SHADER_STAGE_COMPUTE,
            .entryPoint = "main",
            .debugName = "Downsample_compute",
            .reflectionSource = kDownsamplerReflectionSource,
        });
        s_passInfo->name = "Downsample_pipeline";
        s_passInfo->Update();
    }

    void Downsampler::CreateUniforms()
    {
        std::vector<DescriptorBindingInfo> bindingInfos(3);
        bindingInfos[0].binding = 0;
        bindingInfos[0].type = PE_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindingInfos[0].imageLayout = PE_IMAGE_LAYOUT_GENERAL;
        bindingInfos[0].count = 13;

        bindingInfos[1].binding = 13;
        bindingInfos[1].type = PE_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindingInfos[1].imageLayout = PE_IMAGE_LAYOUT_GENERAL;

        bindingInfos[2].binding = 14;
        bindingInfos[2].type = PE_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindingInfos[2].structuredStride = sizeof(s_counter);

        for (uint32_t i = 0; i < MAX_DESCRIPTORS_PER_CMD; i++)
        {
            s_atomicCounter[i] = Buffer::Create({
                .size = sizeof(s_counter),
                .usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST,
                .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY,
                .name = "Downsample_storage_buffer_" + std::to_string(i),
            });

            s_DSet[i] = Descriptor::Create(bindingInfos, PE_SHADER_STAGE_COMPUTE, false, "Downsample_descriptor_" + std::to_string(i));
        }
    }

    void Downsampler::SetInputImage(Image *image)
    {
        uint32_t mips = image->GetMipLevels();

        PE_ERROR_IF(mips <= 1, "Image has no extra mips!");

        for (uint32_t i = 0; i < mips; i++)
        {
            if (!image->HasUAV(i))
                image->CreateUAV(PE_IMAGE_VIEW_TYPE_2D_ARRAY, i);
        }

        s_image = image;
    }

    void Downsampler::UpdateDescriptorSet()
    {
        int mips = static_cast<int>(s_image->GetMipLevels());
        std::vector<ImageView *> views(mips);
        for (int i = 0; i < mips; i++)
            views[i] = s_image->GetUAV(i);

        Descriptor &dSet = *s_DSet[s_currentIndex];
        dSet.SetImageViews(0, views);
        if (mips >= 7)
            dSet.SetImageView(13, s_image->GetUAV(6));
        dSet.SetBuffer(14, s_atomicCounter[s_currentIndex]);

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
} // namespace pe
