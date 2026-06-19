#include "DescriptorBuffer.h"

#include "BindGroup.h"
#include "Device.h"
#include "PipelineLayout.h"
#include "API/Buffer.h"
#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"


namespace pwgpu
{
    namespace
    {
        bool IsDescriptorBufferEligible(const WGPUBindGroupLayoutImpl *bgl)
        {
            if (!bgl || !bgl->device || !bgl->device->descriptorBuffer.enabled)
                return false;
            if (bgl->entries.empty() || bgl->dynamicOffsetCount != 0)
                return false;
            for (const auto &info : bgl->bindingInfos)
            {
                if (info.count != 1)
                    return false;
                if (info.type == PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
                    info.type == PE_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
                    return false;
            }
            return true;
        }

        bool CanUseDescriptorBufferModel(WGPUDeviceImpl *device,
                                         const std::vector<WGPUBindGroupLayoutImpl *> &bgls)
        {
            if (!device || !device->descriptorBuffer.enabled)
                return false;

            for (auto *bgl : bgls)
            {
                if (!bgl || bgl->entries.empty())
                    continue;
                if (bgl->dynamicOffsetCount != 0 || bgl->descriptorBufferLayout == 0)
                    return false;
            }
            return true;
        }

        vk::DescriptorSetLayout CreateEmptyDescriptorSetLayout(bool descriptorBuffer)
        {
            vk::DescriptorSetLayoutCreateInfo ci{};
            if (descriptorBuffer)
                ci.flags |= vk::DescriptorSetLayoutCreateFlagBits::eDescriptorBufferEXT;
            return pe::VulkanRhi::Device().createDescriptorSetLayout(ci);
        }
    } // namespace

    uint64_t AlignDescriptorBufferOffset(uint64_t value, uint64_t alignment)
    {
        if (alignment <= 1)
            return value;
        return (value + alignment - 1) & ~(alignment - 1);
    }

    size_t DescriptorBufferDescriptorSize(const WGPUDeviceImpl *device, PeBindingType type)
    {
        if (!device)
            return 0;

        const WGPUDescriptorBufferState &db = device->descriptorBuffer;
        switch (type)
        {
        case PE_DESCRIPTOR_TYPE_SAMPLER:
            return db.samplerDescriptorSize;
        case PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            return db.combinedImageSamplerDescriptorSize;
        case PE_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            return db.sampledImageDescriptorSize;
        case PE_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            return db.storageImageDescriptorSize;
        case PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            return db.robustBufferAccess && db.robustUniformBufferDescriptorSize != 0
                       ? db.robustUniformBufferDescriptorSize
                       : db.uniformBufferDescriptorSize;
        case PE_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        case PE_DESCRIPTOR_TYPE_STRUCTURED_BUFFER:
        case PE_DESCRIPTOR_TYPE_BYTE_ADDRESS_BUFFER:
            return db.robustBufferAccess && db.robustStorageBufferDescriptorSize != 0
                       ? db.robustStorageBufferDescriptorSize
                       : db.storageBufferDescriptorSize;
        default:
            return 0;
        }
    }

    bool CreateDescriptorBufferLayout(WGPUBindGroupLayoutImpl *bgl)
    {
        if (!IsDescriptorBufferEligible(bgl))
            return false;
        if (bgl->device->rhi && bgl->device->rhi->GetApi() != PE_GRAPHICS_API_VULKAN)
            return false;

        std::vector<vk::DescriptorSetLayoutBinding> bindings;
        bindings.reserve(bgl->bindingInfos.size());
        for (const auto &bindingInfo : bgl->bindingInfos)
        {
            if (DescriptorBufferDescriptorSize(bgl->device, bindingInfo.type) == 0)
                return false;

            vk::DescriptorSetLayoutBinding layoutBinding{};
            layoutBinding.binding = bindingInfo.binding;
            layoutBinding.descriptorType = pe::ToVkDescriptorType(bindingInfo.type);
            layoutBinding.descriptorCount = bindingInfo.count;
            layoutBinding.stageFlags = pe::ToVkShaderStageFlags(bgl->stage);
            bindings.push_back(layoutBinding);
        }

        vk::DescriptorSetLayoutCreateInfo ci{};
        ci.flags = vk::DescriptorSetLayoutCreateFlagBits::eDescriptorBufferEXT;
        ci.bindingCount = static_cast<uint32_t>(bindings.size());
        ci.pBindings = bindings.empty() ? nullptr : bindings.data();

        try
        {
            vk::DescriptorSetLayout layout =
                pe::VulkanRhi::Device().createDescriptorSetLayout(ci);
            bgl->descriptorBufferLayout =
                PeToBackendHandle(static_cast<VkDescriptorSetLayout>(layout));

            vk::DeviceSize layoutSize = 0;
            pe::VulkanRhi::Device().getDescriptorSetLayoutSizeEXT(layout, &layoutSize);
            bgl->descriptorBufferLayoutSize = static_cast<uint64_t>(layoutSize);

            bgl->descriptorBufferBindingOffsets.clear();
            bgl->descriptorBufferBindingOffsets.reserve(bgl->bindingInfos.size());
            for (const auto &bindingInfo : bgl->bindingInfos)
            {
                vk::DeviceSize bindingOffset = 0;
                pe::VulkanRhi::Device().getDescriptorSetLayoutBindingOffsetEXT(
                    layout, bindingInfo.binding, &bindingOffset);
                bgl->descriptorBufferBindingOffsets.push_back(
                    static_cast<uint64_t>(bindingOffset));
            }
        }
        catch (...)
        {
            DestroyDescriptorBufferLayout(bgl);
            return false;
        }

        return bgl->descriptorBufferLayout != 0 && bgl->descriptorBufferLayoutSize != 0;
    }

    bool CreateWebGPUPipelineLayoutBackend(WGPUPipelineLayoutImpl *pl)
    {
        if (!pl || !pl->device)
            return false;
        if (pl->device->rhi && pl->device->rhi->GetApi() != PE_GRAPHICS_API_VULKAN)
            return true;

        while (!pl->bindGroupLayouts.empty() && pl->bindGroupLayouts.back() == nullptr)
            pl->bindGroupLayouts.pop_back();

        const bool useDescriptorBuffer =
            CanUseDescriptorBufferModel(pl->device, pl->bindGroupLayouts);
        pl->bindingModel = useDescriptorBuffer
                               ? WGPUBindingModel::DescriptorBuffer
                               : WGPUBindingModel::Classic;

        std::vector<vk::DescriptorSetLayout> vkSetLayouts;
        vkSetLayouts.reserve(pl->bindGroupLayouts.size());

        try
        {
            for (auto *bgl : pl->bindGroupLayouts)
            {
                if (useDescriptorBuffer)
                {
                    if (bgl && !bgl->entries.empty())
                    {
                        vkSetLayouts.push_back(vk::DescriptorSetLayout{
                            PeFromBackendHandle<VkDescriptorSetLayout>(
                                bgl->descriptorBufferLayout)});
                    }
                    else
                    {
                        vk::DescriptorSetLayout emptyLayout =
                            CreateEmptyDescriptorSetLayout(true);
                        pl->ownedEmptyBackendLayouts.push_back(
                            PeToBackendHandle(static_cast<VkDescriptorSetLayout>(emptyLayout)));
                        vkSetLayouts.push_back(emptyLayout);
                    }
                    continue;
                }

                if (bgl && bgl->layout)
                {
                    vkSetLayouts.push_back(pe::GetVulkanDescriptorLayout(bgl->layout));
                }
                else
                {
                    vk::DescriptorSetLayout emptyLayout =
                        CreateEmptyDescriptorSetLayout(false);
                    pl->ownedEmptyBackendLayouts.push_back(
                        PeToBackendHandle(static_cast<VkDescriptorSetLayout>(emptyLayout)));
                    vkSetLayouts.push_back(emptyLayout);
                }
            }

            vk::PipelineLayoutCreateInfo ci{};
            ci.setLayoutCount = static_cast<uint32_t>(vkSetLayouts.size());
            ci.pSetLayouts = vkSetLayouts.empty() ? nullptr : vkSetLayouts.data();
            vk::PipelineLayout layout = pe::VulkanRhi::Device().createPipelineLayout(ci);
            pl->backendLayout = PeToBackendHandle(static_cast<VkPipelineLayout>(layout));
        }
        catch (...)
        {
            return false;
        }

        return pl->backendLayout != 0;
    }

    void DestroyDescriptorBufferLayout(WGPUBindGroupLayoutImpl *bgl)
    {
        if (!bgl || bgl->descriptorBufferLayout == 0)
            return;
        if (bgl->device && bgl->device->rhi &&
            bgl->device->rhi->GetApi() == PE_GRAPHICS_API_VULKAN)
        {
            pe::VulkanRhi::Device().destroyDescriptorSetLayout(
                vk::DescriptorSetLayout{
                    PeFromBackendHandle<VkDescriptorSetLayout>(
                        bgl->descriptorBufferLayout)});
        }
        bgl->descriptorBufferLayout = 0;
        bgl->descriptorBufferLayoutSize = 0;
        bgl->descriptorBufferBindingOffsets.clear();
    }

    bool AllocateDescriptorBufferSlice(WGPUDeviceImpl *device,
                                       uint64_t requestedSize,
                                       uint64_t &outOffset,
                                       uint64_t &outSize)
    {
        outOffset = 0;
        outSize = 0;
        if (!device || !device->descriptorBuffer.enabled ||
            !device->descriptorBuffer.buffer || requestedSize == 0)
            return false;

        WGPUDescriptorBufferState &db = device->descriptorBuffer;
        const uint64_t alignment = std::max<uint64_t>(1, db.descriptorBufferOffsetAlignment);
        const uint64_t size = AlignDescriptorBufferOffset(requestedSize, alignment);

        std::lock_guard<std::mutex> lock(db.mutex);
        for (auto it = db.freeSlices.begin(); it != db.freeSlices.end(); ++it)
        {
            if (it->size < size)
                continue;
            outOffset = it->offset;
            outSize = size;
            it->offset += size;
            it->size -= size;
            if (it->size == 0)
                db.freeSlices.erase(it);
            return true;
        }

        const uint64_t offset = AlignDescriptorBufferOffset(db.cursor, alignment);
        if (offset > db.bufferSize || size > db.bufferSize - offset)
            return false;

        db.cursor = offset + size;
        outOffset = offset;
        outSize = size;
        return true;
    }

    void FreeDescriptorBufferSlice(WGPUDeviceImpl *device, uint64_t offset, uint64_t size)
    {
        if (!device || !device->descriptorBuffer.buffer || size == 0)
            return;

        WGPUDescriptorBufferState &db = device->descriptorBuffer;
        std::lock_guard<std::mutex> lock(db.mutex);
        db.freeSlices.push_back({offset, size});
        std::sort(db.freeSlices.begin(), db.freeSlices.end(),
                  [](const WGPUDescriptorBufferState::FreeSlice &a,
                     const WGPUDescriptorBufferState::FreeSlice &b)
                  { return a.offset < b.offset; });

        std::vector<WGPUDescriptorBufferState::FreeSlice> merged;
        merged.reserve(db.freeSlices.size());
        for (const auto &slice : db.freeSlices)
        {
            if (!merged.empty() &&
                merged.back().offset + merged.back().size == slice.offset)
            {
                merged.back().size += slice.size;
            }
            else
            {
                merged.push_back(slice);
            }
        }
        db.freeSlices = std::move(merged);
        if (!db.freeSlices.empty())
        {
            const auto &tail = db.freeSlices.back();
            if (tail.offset + tail.size == db.cursor)
            {
                db.cursor = tail.offset;
                db.freeSlices.pop_back();
            }
        }
    }
} // namespace pwgpu
