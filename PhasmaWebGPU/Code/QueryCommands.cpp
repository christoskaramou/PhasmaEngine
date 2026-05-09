#include "QueryCommands.h"

#include "Buffer.h"
#include "Device.h"
#include "QuerySet.h"

#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Vulkan/VulkanCommandBufferImpl.h"

#include <utility>

namespace pwgpu
{
    namespace
    {
        QueryBackendResult QueryInternalError(std::string message)
        {
            QueryBackendResult result;
            result.errorType = WGPUErrorType_Internal;
            result.message = std::move(message);
            return result;
        }

        PeGraphicsApi DeviceApi(WGPUDeviceImpl *device)
        {
            return (device && device->rhi) ? device->rhi->GetApi() : pe::RHII.GetApi();
        }

        PeGraphicsApi QuerySetApi(WGPUQuerySetImpl *querySet)
        {
            return querySet ? DeviceApi(querySet->device) : pe::RHII.GetApi();
        }

        bool BackendUnsupported(const char *operation, PeGraphicsApi api)
        {
            PE_ERROR("[WebGPU] query %s has no %s backend path yet",
                     operation, PeGraphicsApiName(api));
            return false;
        }

        vk::QueryType ToVkQueryType(WGPUQueryType type)
        {
            switch (type)
            {
            case WGPUQueryType_Occlusion:
                return vk::QueryType::eOcclusion;
            case WGPUQueryType_Timestamp:
                return vk::QueryType::eTimestamp;
            default:
                return vk::QueryType::eOcclusion;
            }
        }

        vk::QueryPool ToVkQueryPool(WGPUQuerySetImpl *querySet)
        {
            return vk::QueryPool{PeFromBackendHandle<VkQueryPool>(querySet->backendQueryPool)};
        }
    } // namespace

    QueryBackendResult CreateWebGPUQuerySetBackend(WGPUDeviceImpl *device,
                                                   WGPUQueryType type,
                                                   uint32_t count)
    {
        if (!device || count == 0)
            return QueryInternalError("createQuerySet: invalid backend creation descriptor");

        switch (DeviceApi(device))
        {
        case PE_GRAPHICS_API_VULKAN:
        {
            vk::QueryPool pool{};
            try
            {
                vk::QueryPoolCreateInfo ci{};
                ci.queryType = ToVkQueryType(type);
                ci.queryCount = count;
                pool = pe::VulkanRhi::Device().createQueryPool(ci);
                if (!pool)
                    return QueryInternalError("createQuerySet: VkQueryPool creation failed");
                pe::VulkanRhi::Device().resetQueryPool(pool, 0, count);
            }
            catch (...)
            {
                if (pool)
                    pe::VulkanRhi::Device().destroyQueryPool(pool);
                return QueryInternalError("createQuerySet: VkQueryPool creation failed");
            }

            QueryBackendResult result;
            result.backendQueryPool = PeToBackendHandle(static_cast<VkQueryPool>(pool));
            return result;
        }
        default:
            return QueryInternalError(std::string("createQuerySet: ") +
                                      PeGraphicsApiName(DeviceApi(device)) +
                                      " backend query-set creation is not implemented");
        }
    }

    void DestroyWebGPUQuerySetBackend(WGPUQuerySetImpl *querySet)
    {
        if (!querySet || !querySet->device || !querySet->device->rhi ||
            querySet->backendQueryPool == 0)
            return;

        switch (QuerySetApi(querySet))
        {
        case PE_GRAPHICS_API_VULKAN:
            pe::VulkanRhi::Device().destroyQueryPool(ToVkQueryPool(querySet));
            break;
        default:
            PE_ERROR("[WebGPU] %s backend query-set destruction is not implemented",
                     PeGraphicsApiName(QuerySetApi(querySet)));
            break;
        }
    }

    bool ResetWebGPUQuerySet(pe::CommandBuffer *cmd,
                             WGPUQuerySetImpl *querySet,
                             uint32_t firstQuery,
                             uint32_t queryCount)
    {
        if (!cmd || !querySet || querySet->backendQueryPool == 0 || queryCount == 0)
            return false;

        switch (QuerySetApi(querySet))
        {
        case PE_GRAPHICS_API_VULKAN:
            pe::GetVulkanCommandBuffer(cmd).resetQueryPool(
                ToVkQueryPool(querySet), firstQuery, queryCount);
            return true;
        default:
            return BackendUnsupported("reset", QuerySetApi(querySet));
        }
    }

    bool BeginWebGPUOcclusionQuery(pe::CommandBuffer *cmd,
                                   WGPUQuerySetImpl *querySet,
                                   uint32_t queryIndex)
    {
        if (!cmd || !querySet || querySet->backendQueryPool == 0)
            return false;

        switch (QuerySetApi(querySet))
        {
        case PE_GRAPHICS_API_VULKAN:
            pe::GetVulkanCommandBuffer(cmd).beginQuery(
                ToVkQueryPool(querySet), queryIndex, vk::QueryControlFlags{});
            return true;
        default:
            return BackendUnsupported("begin occlusion", QuerySetApi(querySet));
        }
    }

    bool EndWebGPUOcclusionQuery(pe::CommandBuffer *cmd,
                                 WGPUQuerySetImpl *querySet,
                                 uint32_t queryIndex)
    {
        if (!cmd || !querySet || querySet->backendQueryPool == 0)
            return false;

        switch (QuerySetApi(querySet))
        {
        case PE_GRAPHICS_API_VULKAN:
            pe::GetVulkanCommandBuffer(cmd).endQuery(ToVkQueryPool(querySet), queryIndex);
            return true;
        default:
            return BackendUnsupported("end occlusion", QuerySetApi(querySet));
        }
    }

    bool WriteWebGPUTimestamp(pe::CommandBuffer *cmd,
                              WGPUQuerySetImpl *querySet,
                              uint32_t queryIndex)
    {
        if (!cmd || !querySet || querySet->backendQueryPool == 0)
            return false;

        switch (QuerySetApi(querySet))
        {
        case PE_GRAPHICS_API_VULKAN:
            pe::GetVulkanCommandBuffer(cmd).writeTimestamp2(
                vk::PipelineStageFlagBits2::eAllCommands,
                ToVkQueryPool(querySet),
                queryIndex);
            return true;
        default:
            return BackendUnsupported("write timestamp", QuerySetApi(querySet));
        }
    }

    bool ResolveWebGPUQuerySet(pe::CommandBuffer *cmd,
                               WGPUQuerySetImpl *querySet,
                               uint32_t firstQuery,
                               uint32_t queryCount,
                               WGPUBufferImpl *dst,
                               uint64_t dstOffset)
    {
        if (!cmd || !querySet || querySet->backendQueryPool == 0 ||
            !dst || !dst->peBuffer || queryCount == 0)
            return false;

        switch (QuerySetApi(querySet))
        {
        case PE_GRAPHICS_API_VULKAN:
        {
            pe::GetVulkanCommandBuffer(cmd).fillBuffer(
                pe::GetVulkanBuffer(dst->peBuffer), dstOffset,
                static_cast<uint64_t>(queryCount) * sizeof(uint64_t), 0u);

            vk::MemoryBarrier2 fillToCopy{};
            fillToCopy.srcStageMask = vk::PipelineStageFlagBits2::eClear;
            fillToCopy.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
            fillToCopy.dstStageMask = vk::PipelineStageFlagBits2::eCopy;
            fillToCopy.dstAccessMask = vk::AccessFlagBits2::eTransferWrite;
            vk::MemoryBarrier2 workToCopy{};
            workToCopy.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands;
            workToCopy.srcAccessMask = vk::AccessFlagBits2::eMemoryWrite;
            workToCopy.dstStageMask = vk::PipelineStageFlagBits2::eCopy;
            workToCopy.dstAccessMask =
                vk::AccessFlagBits2::eTransferRead | vk::AccessFlagBits2::eTransferWrite;
            vk::MemoryBarrier2 mbs[2] = {fillToCopy, workToCopy};
            vk::DependencyInfo dep{};
            dep.memoryBarrierCount = 2;
            dep.pMemoryBarriers = mbs;
            pe::GetVulkanCommandBuffer(cmd).pipelineBarrier2(dep);

            const uint32_t rangeEnd = firstQuery + queryCount;
            for (uint32_t idx : querySet->beganIndices)
            {
                if (idx < firstQuery || idx >= rangeEnd)
                    continue;
                const uint64_t slotOffset = dstOffset +
                                            static_cast<uint64_t>(idx - firstQuery) * sizeof(uint64_t);
                pe::GetVulkanCommandBuffer(cmd).copyQueryPoolResults(
                    ToVkQueryPool(querySet), idx, 1,
                    pe::GetVulkanBuffer(dst->peBuffer), slotOffset,
                    sizeof(uint64_t),
                    vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);
            }
            return true;
        }
        default:
            return BackendUnsupported("resolve", QuerySetApi(querySet));
        }
    }
} // namespace pwgpu
