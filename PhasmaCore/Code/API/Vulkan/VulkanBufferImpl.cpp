#include "API/Vulkan/VulkanBufferImpl.h"
#if defined(PE_WIN32)
#include "API/DX12/Dx12BufferImpl.h"
#include "API/DX12/Dx12CommandBufferImpl.h"
#endif
#include "API/Command.h"
#include "API/Vulkan/Helpers_Vulkan.h"
#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Vulkan/VulkanCommandBufferImpl.h"
#include "API/Vulkan/VulkanRHITypeUtils.h"

namespace pe
{
    namespace
    {
        vk::BufferUsageFlags ToVkBufferUsage(PeBufferUsageFlags usage)
        {
            vk::BufferUsageFlags vkUsage{};
            if (usage & PE_BUFFER_USAGE_TRANSFER_SRC)
                vkUsage |= vk::BufferUsageFlagBits::eTransferSrc;
            if (usage & PE_BUFFER_USAGE_TRANSFER_DST)
                vkUsage |= vk::BufferUsageFlagBits::eTransferDst;
            if (usage & PE_BUFFER_USAGE_UNIFORM_BUFFER)
                vkUsage |= vk::BufferUsageFlagBits::eUniformBuffer;
            if (usage & PE_BUFFER_USAGE_STORAGE_BUFFER)
                vkUsage |= vk::BufferUsageFlagBits::eStorageBuffer;
            if (usage & PE_BUFFER_USAGE_INDEX_BUFFER)
                vkUsage |= vk::BufferUsageFlagBits::eIndexBuffer;
            if (usage & PE_BUFFER_USAGE_VERTEX_BUFFER)
                vkUsage |= vk::BufferUsageFlagBits::eVertexBuffer;
            if (usage & PE_BUFFER_USAGE_INDIRECT_BUFFER)
                vkUsage |= vk::BufferUsageFlagBits::eIndirectBuffer;
            if (usage & PE_BUFFER_USAGE_SHADER_DEVICE_ADDRESS)
                vkUsage |= vk::BufferUsageFlagBits::eShaderDeviceAddress;
            if (usage & PE_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_KHR)
                vkUsage |= vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR;
            if (usage & PE_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_KHR)
                vkUsage |= vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
            if (usage & PE_BUFFER_USAGE_SHADER_BINDING_TABLE_KHR)
                vkUsage |= vk::BufferUsageFlagBits::eShaderBindingTableKHR;
            if (usage & PE_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER)
                vkUsage |= vk::BufferUsageFlagBits::eResourceDescriptorBufferEXT;
            if (usage & PE_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER)
                vkUsage |= vk::BufferUsageFlagBits::eSamplerDescriptorBufferEXT;
            return vkUsage;
        }

        VmaAllocationCreateFlags ToVmaCreateFlags(PeMemoryUsage memUsage)
        {
            switch (memUsage)
            {
            case PE_MEMORY_USAGE_GPU_ONLY:
                return 0;
            case PE_MEMORY_USAGE_GPU_ONLY_DEDICATED:
                return VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
            case PE_MEMORY_USAGE_CPU_TO_GPU:
                return VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            case PE_MEMORY_USAGE_CPU_TO_GPU_PERSISTENT:
                return VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            case PE_MEMORY_USAGE_GPU_TO_CPU:
                return VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
            case PE_MEMORY_USAGE_GPU_TO_CPU_PERSISTENT:
                return VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            default:
                return 0;
            }
        }
    } // namespace

    VulkanBufferImpl::VulkanBufferImpl(Buffer *owner, const BufferDesc &desc)
        : m_owner{owner}
    {
        const vk::BufferUsageFlags vkUsage = ToVkBufferUsage(desc.usage);
        const VmaAllocationCreateFlags vmaFlags = ToVmaCreateFlags(desc.memoryUsage);
        size_t size = owner->m_size;

        vk::BufferCreateInfo bufferInfo{};
        bufferInfo.usage = vkUsage;
        bufferInfo.size = size;
        bufferInfo.sharingMode = vk::SharingMode::eExclusive;

        VmaAllocationCreateInfo allocationCreateInfo{};
        allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocationCreateInfo.flags = vmaFlags;

        VkBuffer vkBuffer = {};
        PE_CHECK(vmaCreateBuffer(VulkanRhi::Allocator(),
                                 reinterpret_cast<const VkBufferCreateInfo *>(&bufferInfo),
                                 &allocationCreateInfo,
                                 &vkBuffer,
                                 &m_allocation,
                                 &m_allocationInfo));
        m_buffer = vkBuffer;

        vmaSetAllocationName(VulkanRhi::Allocator(), m_allocation, owner->m_name.c_str());
        Debug::SetObjectName(m_buffer, owner->m_name);
    }

    VulkanBufferImpl::~VulkanBufferImpl()
    {
        if (m_owner && m_owner->Data())
            Unmap();
        vmaDestroyBuffer(VulkanRhi::Allocator(), m_buffer, m_allocation);
    }

    void *VulkanBufferImpl::Map()
    {
        void *data = nullptr;
        PE_CHECK(vmaMapMemory(VulkanRhi::Allocator(), m_allocation, &data));
        return data;
    }

    void VulkanBufferImpl::Unmap()
    {
        vmaUnmapMemory(VulkanRhi::Allocator(), m_allocation);
    }

    void VulkanBufferImpl::Flush(size_t size, size_t offset) const
    {
        PE_CHECK(vmaFlushAllocation(VulkanRhi::Allocator(), m_allocation, offset, size));
    }

    uint64_t VulkanBufferImpl::GetDeviceAddress() const
    {
        vk::BufferDeviceAddressInfo info{};
        info.buffer = m_buffer;
        return VulkanRhi::Device().getBufferAddress(info);
    }

    void VulkanBufferImpl::CopyBuffer(CommandBuffer *cmd, Buffer *src, size_t size, size_t srcOffset, size_t dstOffset)
    {
        PE_ERROR_IF(size + srcOffset > src->Size(), "Buffer::CopyBuffer: Source size is too big");
        PE_ERROR_IF(size + dstOffset > m_owner->Size(), "Buffer::CopyBuffer: Destination size is too small");

        if (RHII.GetCaps().copyCommands2)
        {
            vk::BufferCopy2 region{};
            region.srcOffset = srcOffset;
            region.dstOffset = dstOffset;
            region.size = size;

            vk::CopyBufferInfo2 copyInfo{};
            copyInfo.srcBuffer = From(src)->m_buffer;
            copyInfo.dstBuffer = m_buffer;
            copyInfo.regionCount = 1;
            copyInfo.pRegions = &region;

            GetVulkanCommandBuffer(cmd).copyBuffer2(copyInfo);
        }
        else
        {
            vk::BufferCopy region{};
            region.srcOffset = srcOffset;
            region.dstOffset = dstOffset;
            region.size = size;
            GetVulkanCommandBuffer(cmd).copyBuffer(From(src)->m_buffer, m_buffer, 1, &region);
        }

        BufferTrackInfo &trackInfo = m_owner->GetTrackInfo();
        trackInfo.buffer = m_owner;
        trackInfo.stageMask = PE_STAGE_TRANSFER;
        trackInfo.accessMask = PE_ACCESS_TRANSFER_WRITE;
        trackInfo.queueFamilyIndex = cmd->GetFamilyId();
        trackInfo.offset = dstOffset;
        trackInfo.size = size;
    }

    Buffer::Impl *CreateBufferImpl(Buffer *owner, const BufferDesc &desc)
    {
        switch (RHII.GetApi())
        {
        case PE_GRAPHICS_API_VULKAN:
            return new VulkanBufferImpl(owner, desc);
#if defined(PE_WIN32)
        case PE_GRAPHICS_API_DX12:
            return new Dx12BufferImpl(owner, desc);
#endif
        default:
            PE_ERROR("CreateBufferImpl: unsupported graphics api %u", static_cast<uint32_t>(RHII.GetApi()));
            return nullptr;
        }
    }

    void Buffer_Barrier_Backend(CommandBuffer *cmd, const BufferBarrierInfo &info)
    {
#if defined(PE_WIN32)
        if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
        {
            PE_ERROR_IF(!cmd, "Buffer::Barrier: no command buffer specified");
            Dx12CommandBufferImpl::From(cmd)->BufferBarrier(info);
            return;
        }
#endif
        PE_ERROR_IF(RHII.GetApi() != PE_GRAPHICS_API_VULKAN,
                    "Buffer_Barrier_Backend: unsupported graphics api %u",
                    static_cast<uint32_t>(RHII.GetApi()));
        PE_PROFILE_SCOPE("Vk BufferBarrier");

        PE_ERROR_IF(!info.buffer, "Buffer::Barrier: no buffer specified");

        BufferTrackInfo &trackInfo = info.buffer->GetTrackInfo();

        bool requestRead = IsReadOnlyAccess(info.accessMask);
        bool previousRead = IsReadOnlyAccess(trackInfo.accessMask);
        bool sameState = trackInfo.stageMask == info.stageMask &&
                         trackInfo.accessMask == info.accessMask &&
                         trackInfo.queueFamilyIndex == info.queueFamilyIndex;
        if (requestRead && previousRead && sameState)
            return;

        if (RHII.GetCaps().sync2)
        {
            vk::BufferMemoryBarrier2 barrier{};
            barrier.buffer = VulkanBufferImpl::From(info.buffer)->m_buffer;
            barrier.srcStageMask = ToVkPipelineStageFlags(trackInfo.stageMask);
            barrier.srcAccessMask = ToVkAccessFlags(trackInfo.accessMask);
            barrier.srcQueueFamilyIndex = trackInfo.queueFamilyIndex;
            barrier.dstStageMask = ToVkPipelineStageFlags(info.stageMask);
            barrier.dstAccessMask = ToVkAccessFlags(info.accessMask);
            barrier.dstQueueFamilyIndex = info.queueFamilyIndex;
            barrier.offset = info.offset;
            barrier.size = info.size;

            vk::DependencyInfo dependencyInfo{};
            dependencyInfo.bufferMemoryBarrierCount = 1;
            dependencyInfo.pBufferMemoryBarriers = &barrier;

            PE_PROFILE_COUNTER("Vk PipelineBarrier2 Buffer Items", 1);
            GetVulkanCommandBuffer(cmd).pipelineBarrier2(dependencyInfo);
        }
        else
        {
            vk::BufferMemoryBarrier barrier{};
            barrier.buffer = VulkanBufferImpl::From(info.buffer)->m_buffer;
            barrier.srcAccessMask = ToVkAccessFlagsLegacy(trackInfo.accessMask);
            barrier.srcQueueFamilyIndex = trackInfo.queueFamilyIndex;
            barrier.dstAccessMask = ToVkAccessFlagsLegacy(info.accessMask);
            barrier.dstQueueFamilyIndex = info.queueFamilyIndex;
            barrier.offset = info.offset;
            barrier.size = info.size;

            vk::PipelineStageFlags srcStageMask = ToVkPipelineStageFlagsLegacy(trackInfo.stageMask);
            vk::PipelineStageFlags dstStageMask = ToVkPipelineStageFlagsLegacy(info.stageMask);
            if (!srcStageMask)
                srcStageMask = vk::PipelineStageFlagBits::eTopOfPipe;
            if (!dstStageMask)
                dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;

            GetVulkanCommandBuffer(cmd).pipelineBarrier(srcStageMask, dstStageMask, {}, 0, nullptr, 1, &barrier, 0, nullptr);
        }

        trackInfo.stageMask = info.stageMask;
        trackInfo.accessMask = info.accessMask;
        trackInfo.queueFamilyIndex = info.queueFamilyIndex;
    }

    void Buffer_Barriers_Backend(CommandBuffer *cmd, const std::vector<BufferBarrierInfo> &infos)
    {
#if defined(PE_WIN32)
        if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
        {
            PE_ERROR_IF(!cmd, "Buffer::Barriers: no command buffer specified");
            Dx12CommandBufferImpl::From(cmd)->BufferBarriers(infos);
            return;
        }
#endif
        PE_ERROR_IF(RHII.GetApi() != PE_GRAPHICS_API_VULKAN,
                    "Buffer_Barriers_Backend: unsupported graphics api %u",
                    static_cast<uint32_t>(RHII.GetApi()));
        PE_PROFILE_SCOPE("Vk BufferBarriers");

        if (infos.empty())
            return;

        const bool sync2 = RHII.GetCaps().sync2;
        // Thread-local scratch avoids a heap allocation per barrier batch.
        thread_local std::vector<vk::BufferMemoryBarrier2> barriers;
        thread_local std::vector<vk::BufferMemoryBarrier> legacyBarriers;
        if (sync2)
            barriers.assign(infos.size(), vk::BufferMemoryBarrier2{});
        else
            legacyBarriers.assign(infos.size(), vk::BufferMemoryBarrier{});
        vk::PipelineStageFlags legacySrcStageMask{};
        vk::PipelineStageFlags legacyDstStageMask{};
        uint32_t barrierIndex = 0;
        {
            for (uint32_t i = 0; i < infos.size(); i++)
            {
                const auto &info = infos[i];
                PE_ERROR_IF(!info.buffer, "Buffer::Barriers: no buffer specified");

                BufferTrackInfo &trackInfo = info.buffer->GetTrackInfo();

                bool requestRead = IsReadOnlyAccess(info.accessMask);
                bool previousRead = IsReadOnlyAccess(trackInfo.accessMask);
                bool sameState = trackInfo.stageMask == info.stageMask &&
                                 trackInfo.accessMask == info.accessMask &&
                                 trackInfo.queueFamilyIndex == info.queueFamilyIndex;
                if (requestRead && previousRead && sameState)
                    continue;

                if (sync2)
                {
                    barriers[barrierIndex].buffer = VulkanBufferImpl::From(info.buffer)->m_buffer;
                    barriers[barrierIndex].srcStageMask = ToVkPipelineStageFlags(trackInfo.stageMask);
                    barriers[barrierIndex].srcAccessMask = ToVkAccessFlags(trackInfo.accessMask);
                    barriers[barrierIndex].srcQueueFamilyIndex = trackInfo.queueFamilyIndex;
                    barriers[barrierIndex].dstStageMask = ToVkPipelineStageFlags(info.stageMask);
                    barriers[barrierIndex].dstAccessMask = ToVkAccessFlags(info.accessMask);
                    barriers[barrierIndex].dstQueueFamilyIndex = info.queueFamilyIndex;
                    barriers[barrierIndex].offset = info.offset;
                    barriers[barrierIndex].size = info.size;
                }
                else
                {
                    legacyBarriers[barrierIndex].buffer = VulkanBufferImpl::From(info.buffer)->m_buffer;
                    legacyBarriers[barrierIndex].srcAccessMask = ToVkAccessFlagsLegacy(trackInfo.accessMask);
                    legacyBarriers[barrierIndex].srcQueueFamilyIndex = trackInfo.queueFamilyIndex;
                    legacyBarriers[barrierIndex].dstAccessMask = ToVkAccessFlagsLegacy(info.accessMask);
                    legacyBarriers[barrierIndex].dstQueueFamilyIndex = info.queueFamilyIndex;
                    legacyBarriers[barrierIndex].offset = info.offset;
                    legacyBarriers[barrierIndex].size = info.size;
                    legacySrcStageMask |= ToVkPipelineStageFlagsLegacy(trackInfo.stageMask);
                    legacyDstStageMask |= ToVkPipelineStageFlagsLegacy(info.stageMask);
                }
                barrierIndex++;

                trackInfo.stageMask = info.stageMask;
                trackInfo.accessMask = info.accessMask;
                trackInfo.queueFamilyIndex = info.queueFamilyIndex;
            }
        }

        if (!barrierIndex)
            return;

        if (sync2)
        {
            vk::DependencyInfo dependencyInfo{};
            dependencyInfo.bufferMemoryBarrierCount = barrierIndex;
            dependencyInfo.pBufferMemoryBarriers = barriers.data();

            PE_PROFILE_COUNTER("Vk PipelineBarrier2 Buffer Items", barrierIndex);
            GetVulkanCommandBuffer(cmd).pipelineBarrier2(dependencyInfo);
        }
        else
        {
            if (!legacySrcStageMask)
                legacySrcStageMask = vk::PipelineStageFlagBits::eTopOfPipe;
            if (!legacyDstStageMask)
                legacyDstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
            GetVulkanCommandBuffer(cmd).pipelineBarrier(legacySrcStageMask, legacyDstStageMask, {}, 0, nullptr, barrierIndex, legacyBarriers.data(), 0, nullptr);
        }
    }
} // namespace pe
