#include "API/Downsampler/Downsampler.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/Shader.h"
#include "API/RHI.h"

namespace pe
{
    namespace
    {
#include "API/Downsampler/DownsamplerShaders.inl"

        static_assert(kDownsamplerSpirv_len == sizeof(kDownsamplerSpirv));
        static_assert(kDownsamplerDxil_len == sizeof(kDownsamplerDxil));

        std::vector<DescriptorBindingInfo> s_bindingInfos;
        struct ShaderVariant
        {
            ::PeFormat format;
            const uint8_t *spirv;
            size_t size;
        };
        constexpr ShaderVariant s_variants[] = {
            {PE_FORMAT_R32G32B32A32_SFLOAT, kDownsamplerSpirv, sizeof(kDownsamplerSpirv)},
            {PE_FORMAT_R8G8B8A8_UNORM, kDownsamplerSpirvRgba8, sizeof(kDownsamplerSpirvRgba8)},
            {PE_FORMAT_R16G16B16A16_SFLOAT, kDownsamplerSpirvRgba16f, sizeof(kDownsamplerSpirvRgba16f)},
            {PE_FORMAT_R16G16_SFLOAT, kDownsamplerSpirvRg16f, sizeof(kDownsamplerSpirvRg16f)},
        };
        std::array<std::shared_ptr<PassInfo>, std::size(s_variants)> s_passInfos;
    } // namespace

    void Downsampler::Init()
    {
        CreateUniforms();
    }

    void Downsampler::Dispatch(CommandBuffer *cmd, Image *image)
    {
        std::lock_guard<std::mutex> guard(s_dispatchMutex);

        PassInfo *passInfo = GetPassInfo(image->GetFormat());
        SetInputImage(image);

        // One set per dispatch: a shared ring gets re-written while an unsubmitted or in-flight cmd
        // still references the slot. The GPU reads the set when this cmd executes, so free it after its wait.
        Descriptor *dSet = Descriptor::Create(s_bindingInfos, PE_SHADER_STAGE_COMPUTE, false, "Downsample_descriptor");
        UpdateDescriptorSet(*dSet);
        cmd->AddAfterWaitCallback([dSet]() mutable
                                  { Descriptor::Destroy(dSet); });

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
        cmd->BindPipeline(*passInfo, false);
        cmd->BindDescriptors(1, &dSet);
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
            Buffer::Destroy(s_atomicCounter[i]);

        for (auto &passInfo : s_passInfos)
            passInfo.reset();
    }

    PassInfo *Downsampler::GetPassInfo(::PeFormat format)
    {
        // DXIL's typed UAV uses the view format; SPIR-V requires an exact storage-image format.
        if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
            format = PE_FORMAT_R32G32B32A32_SFLOAT;
        size_t index = 0;
        while (index < std::size(s_variants) && s_variants[index].format != format)
            ++index;
        PE_ERROR_IF(index == std::size(s_variants), "Downsampler: unsupported storage-image format %u", static_cast<uint32_t>(format));
        auto &passInfo = s_passInfos[index];
        if (passInfo)
            return passInfo.get();

        const std::string name = "Downsample_" + std::to_string(format);
        passInfo = std::make_shared<PassInfo>();
        passInfo->pCompShader = Shader::CreateFromBytecode({
            .spirv = s_variants[index].spirv,
            .spirvSizeBytes = s_variants[index].size,
            .dxil = kDownsamplerDxil,
            .dxilSizeBytes = sizeof(kDownsamplerDxil),
            .stage = PE_SHADER_STAGE_COMPUTE,
            .entryPoint = "main",
            .debugName = name,
            .reflectionSource = kDownsamplerReflectionSource,
        });
        passInfo->name = name;
        passInfo->Update();
        return passInfo.get();
    }

    void Downsampler::CreateUniforms()
    {
        s_bindingInfos.assign(3, {});
        s_bindingInfos[0].binding = 0;
        s_bindingInfos[0].type = PE_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        s_bindingInfos[0].imageLayout = PE_IMAGE_LAYOUT_GENERAL;
        s_bindingInfos[0].count = 13;

        s_bindingInfos[1].binding = 13;
        s_bindingInfos[1].type = PE_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        s_bindingInfos[1].imageLayout = PE_IMAGE_LAYOUT_GENERAL;

        s_bindingInfos[2].binding = 14;
        s_bindingInfos[2].type = PE_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        s_bindingInfos[2].structuredStride = sizeof(s_counter);

        for (uint32_t i = 0; i < MAX_DESCRIPTORS_PER_CMD; i++)
        {
            s_atomicCounter[i] = Buffer::Create({
                .size = sizeof(s_counter),
                .usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST,
                .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY,
                .name = "Downsample_storage_buffer_" + std::to_string(i),
            });
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

    void Downsampler::UpdateDescriptorSet(Descriptor &dSet)
    {
        int mips = static_cast<int>(s_image->GetMipLevels());
        std::vector<ImageView *> views(mips);
        for (int i = 0; i < mips; i++)
            views[i] = s_image->GetUAV(i);

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
