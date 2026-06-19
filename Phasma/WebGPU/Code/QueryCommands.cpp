#include "QueryCommands.h"

#include "Buffer.h"
#include "Device.h"
#include "QuerySet.h"

#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Vulkan/VulkanCommandBufferImpl.h"
#if defined(PE_WIN32)
#include "API/DX12/Dx12CommandBufferImpl.h"
#include "API/DX12/Dx12RhiImpl.h"
#endif

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

#if defined(PE_WIN32)
        D3D12_QUERY_HEAP_TYPE ToDx12QueryHeapType(WGPUQueryType type)
        {
            switch (type)
            {
            case WGPUQueryType_Occlusion:
                return D3D12_QUERY_HEAP_TYPE_OCCLUSION;
            case WGPUQueryType_Timestamp:
                return D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
            default:
                return D3D12_QUERY_HEAP_TYPE_OCCLUSION;
            }
        }

        D3D12_QUERY_TYPE ToDx12QueryType(WGPUQueryType type)
        {
            switch (type)
            {
            case WGPUQueryType_Occlusion:
                return D3D12_QUERY_TYPE_OCCLUSION;
            case WGPUQueryType_Timestamp:
                return D3D12_QUERY_TYPE_TIMESTAMP;
            default:
                return D3D12_QUERY_TYPE_OCCLUSION;
            }
        }

        ID3D12QueryHeap *ToDx12QueryHeap(WGPUQuerySetImpl *querySet)
        {
            return PeFromBackendHandle<ID3D12QueryHeap *>(querySet->backendQueryPool);
        }
#endif
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
#if defined(PE_WIN32)
        case PE_GRAPHICS_API_DX12:
        {
            ID3D12Device *dxDevice = pe::GetDx12Device();
            if (!dxDevice)
                return QueryInternalError("createQuerySet: DX12 device is not initialized");

            D3D12_QUERY_HEAP_DESC desc{};
            desc.Type = ToDx12QueryHeapType(type);
            desc.Count = count;
            desc.NodeMask = 0;

            ID3D12QueryHeap *heap = nullptr;
            HRESULT hr = dxDevice->CreateQueryHeap(&desc, IID_PPV_ARGS(&heap));
            if (FAILED(hr) || !heap)
                return QueryInternalError("createQuerySet: ID3D12QueryHeap creation failed");

            QueryBackendResult result;
            result.backendQueryPool = PeToBackendHandle(heap);
            return result;
        }
#endif
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
#if defined(PE_WIN32)
        case PE_GRAPHICS_API_DX12:
            ToDx12QueryHeap(querySet)->Release();
            break;
#endif
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
            MarkQueryRangeUnavailable(querySet, firstQuery, queryCount);
            return true;
#if defined(PE_WIN32)
        case PE_GRAPHICS_API_DX12:
            // D3D12 query heaps do not have an explicit reset command; a query slot
            // becomes available again when BeginQuery/EndQuery or EndQuery(timestamp)
            // overwrites it.
            MarkQueryRangeUnavailable(querySet, firstQuery, queryCount);
            return true;
#endif
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
            querySet->beganIndices.insert(queryIndex);
            return true;
#if defined(PE_WIN32)
        case PE_GRAPHICS_API_DX12:
            pe::GetDx12CommandList(cmd)->BeginQuery(
                ToDx12QueryHeap(querySet), D3D12_QUERY_TYPE_OCCLUSION, queryIndex);
            querySet->beganIndices.insert(queryIndex);
            return true;
#endif
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
#if defined(PE_WIN32)
        case PE_GRAPHICS_API_DX12:
            pe::GetDx12CommandList(cmd)->EndQuery(
                ToDx12QueryHeap(querySet), D3D12_QUERY_TYPE_OCCLUSION, queryIndex);
            return true;
#endif
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
            querySet->beganIndices.insert(queryIndex);
            return true;
#if defined(PE_WIN32)
        case PE_GRAPHICS_API_DX12:
            pe::GetDx12CommandList(cmd)->EndQuery(
                ToDx12QueryHeap(querySet), D3D12_QUERY_TYPE_TIMESTAMP, queryIndex);
            querySet->beganIndices.insert(queryIndex);
            return true;
#endif
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

            for (uint32_t idx : querySet->beganIndices)
            {
                if (!QueryIndexInRange(idx, firstQuery, queryCount))
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
#if defined(PE_WIN32)
        case PE_GRAPHICS_API_DX12:
        {
            cmd->FillBuffer(dst->peBuffer, static_cast<size_t>(dstOffset),
                            static_cast<size_t>(queryCount) * sizeof(uint64_t), 0u);

            ID3D12GraphicsCommandList *list = pe::GetDx12CommandList(cmd);
            ID3D12QueryHeap *heap = ToDx12QueryHeap(querySet);
            ID3D12Resource *dstResource = pe::GetDx12BufferResource(dst->peBuffer);
            const D3D12_QUERY_TYPE queryType = ToDx12QueryType(querySet->type);
            for (uint32_t idx : querySet->beganIndices)
            {
                if (!QueryIndexInRange(idx, firstQuery, queryCount))
                    continue;
                const uint64_t slotOffset = dstOffset +
                                            static_cast<uint64_t>(idx - firstQuery) * sizeof(uint64_t);
                list->ResolveQueryData(heap, queryType, idx, 1, dstResource, slotOffset);
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
