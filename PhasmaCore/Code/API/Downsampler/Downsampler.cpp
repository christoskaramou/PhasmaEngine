#include "API/Downsampler/Downsampler.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/Shader.h"

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
        barrier.layout = PE_IMAGE_LAYOUT_GENERAL;
        barrier.stageFlags = PE_STAGE_COMPUTE_SHADER;
        barrier.accessMask = PE_ACCESS_SHADER_WRITE;

        cmd->BeginDebugRegion("Downsampler::Dispatch Command_" + std::to_string(s_currentIndex));
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
        // Downsampler::Init runs from RHII::Init, before the editor module's FileWatcher::Add loop
        // registers shader files. Self-register the SPD integration shader so Shader::Create's
        // FileWatcher presence check passes; vendored FFX headers don't need hot-reload.
        const std::string spdShaderPath = Path::Assets + "Shaders/Downsampler/SPDIntegration.hlsl";
        if (!FileWatcher::Get(spdShaderPath))
            FileWatcher::Add(spdShaderPath, [](size_t) {});

        s_passInfo = std::make_shared<PassInfo>();
        s_passInfo->pCompShader = Shader::Create({
            .sourcePath = spdShaderPath,
            .entryPoint = "main",
            .stage = PE_SHADER_STAGE_COMPUTE,
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

        for (uint32_t i = 0; i < MAX_DESCRIPTORS_PER_CMD; i++)
        {
            s_atomicCounter[i] = Buffer::Create({
                .size = sizeof(s_counter),
                .usage = PE_BUFFER_USAGE_STORAGE_BUFFER,
                .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU,
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
