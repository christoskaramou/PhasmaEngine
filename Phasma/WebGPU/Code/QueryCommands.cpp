#include "QueryCommands.h"

#include "Buffer.h"
#include "Device.h"
#include "QuerySet.h"

#include "API/Command.h"
#include "API/QueryPool.h"
#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Vulkan/VulkanBufferImpl.h"
#include "API/Vulkan/VulkanCommandBufferImpl.h"

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
            return (device && device->rhi) ? device->rhi->GetApi() : pe::GetRHI().GetApi();
        }

        PeGraphicsApi QuerySetApi(WGPUQuerySetImpl *querySet)
        {
            return querySet ? DeviceApi(querySet->device) : pe::GetRHI().GetApi();
        }

        bool BackendUnsupported(const char *operation, PeGraphicsApi api)
        {
            PE_ERROR("[WebGPU] query %s has no %s backend path yet",
                     operation, PeGraphicsApiName(api));
            return false;
        }

        PeQueryType ToPeQueryType(WGPUQueryType type)
        {
            return type == WGPUQueryType_Timestamp ? PE_QUERY_TYPE_TIMESTAMP : PE_QUERY_TYPE_OCCLUSION;
        }

        void MarkQueryRangeUnavailable(WGPUQuerySetImpl *querySet,
                                       uint32_t firstQuery,
                                       uint32_t queryCount)
        {
            if (!querySet || queryCount == 0)
                return;

            const uint64_t rangeBegin = firstQuery;
            const uint64_t rangeEnd = rangeBegin + queryCount;
            for (auto it = querySet->beganIndices.begin(); it != querySet->beganIndices.end();)
            {
                const uint64_t idx = *it;
                if (idx >= rangeBegin && idx < rangeEnd)
                    it = querySet->beganIndices.erase(it);
                else
                    ++it;
            }
        }

        bool QueryIndexInRange(uint32_t queryIndex, uint32_t firstQuery, uint32_t queryCount)
        {
            const uint64_t idx = queryIndex;
            const uint64_t rangeBegin = firstQuery;
            const uint64_t rangeEnd = rangeBegin + queryCount;
            return idx >= rangeBegin && idx < rangeEnd;
        }
    } // namespace

    QueryBackendResult CreateWebGPUQuerySetBackend(WGPUDeviceImpl *device,
                                                   WGPUQueryType type,
                                                   uint32_t count)
    {
        if (!device || count == 0)
            return QueryInternalError("createQuerySet: invalid backend creation descriptor");

        pe::QueryPoolDesc desc{};
        desc.type = ToPeQueryType(type);
        desc.count = count;
        desc.name = "WebGPUQuerySet";

        pe::QueryPool *pool = nullptr;
        try
        {
            pool = pe::QueryPool::Create(desc);
        }
        catch (...)
        {
            return QueryInternalError("createQuerySet: backend query pool creation failed");
        }
        if (!pool)
            return QueryInternalError("createQuerySet: backend query pool creation failed");

        QueryBackendResult result;
        result.backendQueryPool = pool;
        return result;
    }

    void DestroyWebGPUQuerySetBackend(WGPUQuerySetImpl *querySet)
    {
        if (!querySet || !querySet->backendQueryPool)
            return;

        pe::QueryPool::Destroy(querySet->backendQueryPool); // also nulls the pointer
    }

    bool ResetWebGPUQuerySet(pe::CommandBuffer *cmd,
                             WGPUQuerySetImpl *querySet,
                             uint32_t firstQuery,
                             uint32_t queryCount)
    {
        if (!cmd || !querySet || !querySet->backendQueryPool || queryCount == 0)
            return false;

        // Vulkan records vkCmdResetQueryPool; DX12 has no reset (slots are reused on
        // the next Begin/EndQuery). Both honored by the neutral CommandBuffer method.
        cmd->ResetQueryPool(querySet->backendQueryPool, firstQuery, queryCount);
        MarkQueryRangeUnavailable(querySet, firstQuery, queryCount);
        return true;
    }

    bool BeginWebGPUOcclusionQuery(pe::CommandBuffer *cmd,
                                   WGPUQuerySetImpl *querySet,
                                   uint32_t queryIndex)
    {
        if (!cmd || !querySet || !querySet->backendQueryPool)
            return false;

        cmd->BeginQuery(querySet->backendQueryPool, queryIndex);
        querySet->beganIndices.insert(queryIndex);
        return true;
    }

    bool EndWebGPUOcclusionQuery(pe::CommandBuffer *cmd,
                                 WGPUQuerySetImpl *querySet,
                                 uint32_t queryIndex)
    {
        if (!cmd || !querySet || !querySet->backendQueryPool)
            return false;

        cmd->EndQuery(querySet->backendQueryPool, queryIndex);
        return true;
    }

    bool WriteWebGPUTimestamp(pe::CommandBuffer *cmd,
                              WGPUQuerySetImpl *querySet,
                              uint32_t queryIndex)
    {
        if (!cmd || !querySet || !querySet->backendQueryPool)
            return false;

        cmd->WriteTimestamp(querySet->backendQueryPool, queryIndex);
        querySet->beganIndices.insert(queryIndex);
        return true;
    }

    bool ResolveWebGPUQuerySet(pe::CommandBuffer *cmd,
                               WGPUQuerySetImpl *querySet,
                               uint32_t firstQuery,
                               uint32_t queryCount,
                               WGPUBufferImpl *dst,
                               uint64_t dstOffset)
    {
        if (!cmd || !querySet || !querySet->backendQueryPool ||
            !dst || !dst->peBuffer || queryCount == 0)
            return false;

        // WebGPU requires unbegun slots to resolve to zero; only begun indices carry a
        // real result. Zero-init the range, then copy just the begun slots. The native
        // copy is issued through the neutral CommandBuffer::ResolveQueryPool; the
        // surrounding zero-fill + state handling stays backend-specific.
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

            for (uint32_t idx : querySet->beganIndices)
            {
                if (!QueryIndexInRange(idx, firstQuery, queryCount))
                    continue;
                const uint64_t slotOffset = dstOffset +
                                            static_cast<uint64_t>(idx - firstQuery) * sizeof(uint64_t);
                cmd->ResolveQueryPool(querySet->backendQueryPool, idx, 1,
                                      dst->peBuffer, slotOffset, sizeof(uint64_t),
                                      PE_QUERY_RESULT_64_BIT | PE_QUERY_RESULT_WAIT);
            }
            return true;
        }
#if defined(PE_WIN32)
        case PE_GRAPHICS_API_DX12:
        {
            cmd->FillBuffer(dst->peBuffer, static_cast<size_t>(dstOffset),
                            static_cast<size_t>(queryCount) * sizeof(uint64_t), 0u);

            for (uint32_t idx : querySet->beganIndices)
            {
                if (!QueryIndexInRange(idx, firstQuery, queryCount))
                    continue;
                const uint64_t slotOffset = dstOffset +
                                            static_cast<uint64_t>(idx - firstQuery) * sizeof(uint64_t);
                cmd->ResolveQueryPool(querySet->backendQueryPool, idx, 1,
                                      dst->peBuffer, slotOffset, sizeof(uint64_t),
                                      PE_QUERY_RESULT_64_BIT);
            }

            pe::BufferTrackInfo &trackInfo = dst->peBuffer->GetTrackInfo();
            trackInfo.stageMask = PE_STAGE_TRANSFER;
            trackInfo.accessMask = PE_ACCESS_TRANSFER_WRITE;
            return true;
        }
#endif
        default:
            return BackendUnsupported("resolve", QuerySetApi(querySet));
        }
    }
} // namespace pwgpu
