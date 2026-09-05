#include "PipelineBinding.h"

#include "BindGroup.h"
#include "Buffer.h"
#include "ComputePipeline.h"
#include "Device.h"
#include "PipelineLayout.h"
#include "RenderPipeline.h"
#include "Utils.h"

#include "API/Buffer.h"
#include "API/RHI.h"
#include "API/Vulkan/VulkanBufferImpl.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Vulkan/VulkanCommandBufferImpl.h"
#include "API/Vulkan/VulkanRHITypeUtils.h"

#if defined(PE_WIN32)
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/DX12/Dx12CommandBufferImpl.h"
#include "API/DX12/Dx12DescriptorHeap.h"
#include "API/DX12/Dx12DescriptorImpl.h"
#include "API/DX12/Dx12RhiImpl.h"
#include "API/DX12/Dx12RootSignature.h"
#include <d3d12.h>
#endif

namespace pwgpu
{
    namespace
    {
        vk::PipelineBindPoint ToVkBindPoint(PipelineBindingPoint point)
        {
            return point == PipelineBindingPoint::Compute
                       ? vk::PipelineBindPoint::eCompute
                       : vk::PipelineBindPoint::eGraphics;
        }

        bool BackendUnsupported(const char *operation)
        {
            PE_ERROR("[WebGPU] %s has no %s backend binding path yet",
                     operation, PeGraphicsApiName(pe::GetRHI().GetApi()));
            return false;
        }

        std::vector<WebGPUBindGroupCacheEntry> &BindGroupCacheForPoint(
            WebGPUBindingCache &cache, PipelineBindingPoint point)
        {
            return point == PipelineBindingPoint::Compute
                       ? cache.computeBindGroups
                       : cache.renderBindGroups;
        }

        bool DynamicOffsetsEqual(const std::vector<uint32_t> &cached,
                                 size_t dynamicOffsetCount,
                                 const uint32_t *dynamicOffsets)
        {
            if (cached.size() != dynamicOffsetCount)
                return false;
            if (dynamicOffsetCount == 0)
                return true;
            if (!dynamicOffsets)
                return false;
            return std::equal(cached.begin(), cached.end(), dynamicOffsets);
        }

        bool IsCachedBindGroup(WebGPUBindingCache *cache,
                               PipelineBindingPoint point,
                               WGPUPipelineLayoutImpl *layout,
                               uint32_t groupIndex,
                               WGPUBindGroupImpl *group,
                               size_t dynamicOffsetCount,
                               const uint32_t *dynamicOffsets)
        {
            if (!cache)
                return false;

            const auto &entries =
                point == PipelineBindingPoint::Compute
                    ? cache->computeBindGroups
                    : cache->renderBindGroups;
            if (groupIndex >= entries.size())
                return false;

            const WebGPUBindGroupCacheEntry &entry = entries[groupIndex];
            return entry.layout == layout &&
                   entry.group == group &&
                   DynamicOffsetsEqual(entry.dynamicOffsets, dynamicOffsetCount, dynamicOffsets);
        }

        void CacheBindGroup(WebGPUBindingCache *cache,
                            PipelineBindingPoint point,
                            WGPUPipelineLayoutImpl *layout,
                            uint32_t groupIndex,
                            WGPUBindGroupImpl *group,
                            size_t dynamicOffsetCount,
                            const uint32_t *dynamicOffsets)
        {
            if (!cache)
                return;

            auto &entries = BindGroupCacheForPoint(*cache, point);
            if (entries.size() <= groupIndex)
                entries.resize(groupIndex + 1);

            WebGPUBindGroupCacheEntry &entry = entries[groupIndex];
            entry.layout = layout;
            entry.group = group;
            if (dynamicOffsetCount > 0 && dynamicOffsets)
                entry.dynamicOffsets.assign(dynamicOffsets, dynamicOffsets + dynamicOffsetCount);
            else
                entry.dynamicOffsets.clear();
        }

        bool IsCachedVertexBuffer(WebGPUBindingCache *cache,
                                  uint32_t firstBinding,
                                  pe::Buffer *buffer,
                                  size_t offset,
                                  uint32_t bindingCount)
        {
            if (!cache || firstBinding >= cache->vertexBuffers.size())
                return false;

            const WebGPUVertexBufferCacheEntry &entry =
                cache->vertexBuffers[firstBinding];
            return entry.buffer == buffer &&
                   entry.offset == offset &&
                   entry.bindingCount == bindingCount;
        }

        void CacheVertexBuffer(WebGPUBindingCache *cache,
                               uint32_t firstBinding,
                               pe::Buffer *buffer,
                               size_t offset,
                               uint32_t bindingCount)
        {
            if (!cache)
                return;

            if (cache->vertexBuffers.size() <= firstBinding)
                cache->vertexBuffers.resize(firstBinding + 1);

            WebGPUVertexBufferCacheEntry &entry = cache->vertexBuffers[firstBinding];
            entry.buffer = buffer;
            entry.offset = offset;
            entry.bindingCount = bindingCount;
        }

        bool IsCachedIndexBuffer(WebGPUBindingCache *cache,
                                 pe::Buffer *buffer,
                                 size_t offset,
                                 PeIndexType indexType)
        {
            return cache &&
                   cache->indexBuffer == buffer &&
                   cache->indexBufferOffset == offset &&
                   cache->indexBufferType == indexType;
        }

        void CacheIndexBuffer(WebGPUBindingCache *cache,
                              pe::Buffer *buffer,
                              size_t offset,
                              PeIndexType indexType)
        {
            if (!cache)
                return;

            cache->indexBuffer = buffer;
            cache->indexBufferOffset = offset;
            cache->indexBufferType = indexType;
        }

        void NoteBindingModel(WebGPUBindingCache *cache, WGPUPipelineLayoutImpl *layout)
        {
            if (!cache || !layout)
                return;

            const bool descriptorBufferModel =
                layout->bindingModel == WGPUBindingModel::DescriptorBuffer;
            if (cache->hasDescriptorBufferModel &&
                cache->descriptorBufferModel != descriptorBufferModel)
            {
                cache->renderBindGroups.clear();
                cache->computeBindGroups.clear();
            }
            cache->hasDescriptorBufferModel = true;
            cache->descriptorBufferModel = descriptorBufferModel;
        }

        bool EnsureDescriptorBufferBound(pe::CommandBuffer *cmd,
                                         WGPUDeviceImpl *device,
                                         WebGPUBindingCache *cache)
        {
            if (!cmd || !device || !device->descriptorBuffer.enabled ||
                !device->descriptorBuffer.buffer)
                return false;
            if (cache && cache->descriptorBufferBound)
                return true;

            vk::DescriptorBufferBindingInfoEXT bindingInfo{};
            bindingInfo.address = device->descriptorBuffer.buffer->GetDeviceAddress();
            bindingInfo.usage = vk::BufferUsageFlagBits::eResourceDescriptorBufferEXT |
                                vk::BufferUsageFlagBits::eSamplerDescriptorBufferEXT;
            pe::GetVulkanCommandBuffer(cmd).bindDescriptorBuffersEXT(1, &bindingInfo);
            if (cache)
                cache->descriptorBufferBound = true;
            return true;
        }

#if defined(PE_WIN32)
        D3D12_PRIMITIVE_TOPOLOGY ToDx12Topology(WGPUPrimitiveTopology topology)
        {
            switch (topology)
            {
            case WGPUPrimitiveTopology_PointList:
                return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
            case WGPUPrimitiveTopology_LineList:
                return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
            case WGPUPrimitiveTopology_LineStrip:
                return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
            case WGPUPrimitiveTopology_TriangleStrip:
                return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
            case WGPUPrimitiveTopology_TriangleList:
            default:
                return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            }
        }

        struct Dx12DynamicPatch
        {
            pe::Dx12DescriptorImpl::DynamicBufferBinding binding{};
        };

        struct Dx12DynamicTable
        {
            uint32_t dxSpace = 0;
            uint32_t firstSlot = pe::Dx12DescriptorImpl::InvalidSlot;
            uint32_t count = 0;
            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
        };

        const pe::DescriptorBindingInfo *FindBindingInfo(WGPUBindGroupImpl *group, uint32_t binding)
        {
            if (!group || !group->layout)
                return nullptr;

            for (const pe::DescriptorBindingInfo &info : group->layout->bindingInfos)
                if (info.binding == binding)
                    return &info;
            return nullptr;
        }

        bool BindDx12DynamicTables(pe::CommandBuffer *cmd,
                                   PipelineBindingPoint point,
                                   uint32_t sourceDxSpace,
                                   uint32_t targetDxSpace,
                                   WGPUBindGroupImpl *group,
                                   size_t dynamicOffsetCount,
                                   const uint32_t *dynamicOffsets)
        {
            if (!cmd || !group || dynamicOffsetCount == 0)
                return true;
            if (!dynamicOffsets || dynamicOffsetCount != group->dynamicBindings.size())
                return false;

            const pe::Dx12DescriptorImpl *descriptorImpl =
                pe::Dx12DescriptorImpl::TryFrom(group->descriptor);
            if (!descriptorImpl)
                return false;

            std::map<uint32_t, std::vector<Dx12DynamicPatch>> patchesBySpace;
            for (size_t i = 0; i < dynamicOffsetCount; ++i)
            {
                const WGPUBindGroupImpl::DynamicBinding &dynamicBinding =
                    group->dynamicBindings[i];
                const pe::DescriptorBindingInfo *bindingInfo =
                    FindBindingInfo(group, dynamicBinding.binding);
                if (!bindingInfo)
                    return false;

                Dx12DynamicPatch patch{};
                patch.binding.binding = dynamicBinding.binding;
                patch.binding.buffer = dynamicBinding.buffer ? dynamicBinding.buffer->peBuffer : nullptr;
                patch.binding.offset =
                    dynamicBinding.baseOffset + static_cast<uint64_t>(dynamicOffsets[i]);
                patch.binding.range = dynamicBinding.bindingSize;
                if (!patch.binding.buffer)
                    return false;
                patchesBySpace[bindingInfo->dxSpace].push_back(patch);
            }

            std::vector<Dx12DynamicTable> dynamicTables;
            dynamicTables.reserve(patchesBySpace.size());
            for (const auto &[dxSpace, patches] : patchesBySpace)
            {
                Dx12DynamicTable table{};
                std::vector<pe::Dx12DescriptorImpl::DynamicBufferBinding> dynamicBindings;
                dynamicBindings.reserve(patches.size());
                for (const Dx12DynamicPatch &patch : patches)
                    dynamicBindings.push_back(patch.binding);

                if (!descriptorImpl->CreateDynamicCbvSrvUavTable(
                        dxSpace,
                        dynamicBindings.data(),
                        static_cast<uint32_t>(dynamicBindings.size()),
                        table.firstSlot,
                        table.count,
                        table.gpuHandle))
                {
                    auto *rhi = static_cast<pe::Dx12RhiImpl *>(pe::GetRHI().GetImpl());
                    if (rhi && rhi->GetCbvSrvUavHeap())
                    {
                        for (const Dx12DynamicTable &allocated : dynamicTables)
                            rhi->GetCbvSrvUavHeap()->FreeRange(allocated.firstSlot, allocated.count);
                    }
                    return false;
                }
                table.dxSpace = dxSpace;
                dynamicTables.push_back(table);
            }

            auto freeDynamicTables = [&dynamicTables]()
            {
                auto *rhi = static_cast<pe::Dx12RhiImpl *>(pe::GetRHI().GetImpl());
                if (!rhi || !rhi->GetCbvSrvUavHeap())
                    return;
                for (const Dx12DynamicTable &table : dynamicTables)
                    rhi->GetCbvSrvUavHeap()->FreeRange(table.firstSlot, table.count);
            };

            ID3D12GraphicsCommandList *list = pe::GetDx12CommandList(cmd);
            if (!list)
            {
                freeDynamicTables();
                return false;
            }

            for (const Dx12DynamicTable &table : dynamicTables)
            {
                const uint32_t rootIdx = pe::Dx12CbvSrvUavRootIndex(
                    table.dxSpace == sourceDxSpace ? targetDxSpace : table.dxSpace);
                if (point == PipelineBindingPoint::Compute)
                    list->SetComputeRootDescriptorTable(rootIdx, table.gpuHandle);
                else
                    list->SetGraphicsRootDescriptorTable(rootIdx, table.gpuHandle);
            }

            cmd->AddAfterWaitCallback(
                [dynamicTables = std::move(dynamicTables)]()
                {
                    auto *rhi = static_cast<pe::Dx12RhiImpl *>(pe::GetRHI().GetImpl());
                    if (!rhi || !rhi->GetCbvSrvUavHeap())
                        return;
                    for (const Dx12DynamicTable &table : dynamicTables)
                        rhi->GetCbvSrvUavHeap()->FreeRange(table.firstSlot, table.count);
                });
            return true;
        }
#endif
    } // namespace

    bool BindWebGPURenderPipeline(pe::CommandBuffer *cmd,
                                  WGPURenderPipelineImpl *pipeline,
                                  WebGPUBindingCache *cache)
    {
        if (!cmd || !pipeline || pipeline->backendPipeline == 0)
            return false;

        switch (pe::GetRHI().GetApi())
        {
        case PE_GRAPHICS_API_VULKAN:
            if (cache && cache->renderPipeline == pipeline)
                return true;
            pe::GetVulkanCommandBuffer(cmd).bindPipeline(
                vk::PipelineBindPoint::eGraphics,
                vk::Pipeline{PeFromBackendHandle<VkPipeline>(pipeline->backendPipeline)});
            if (cache)
                cache->renderPipeline = pipeline;
            return true;
        case PE_GRAPHICS_API_DX12:
#if defined(PE_WIN32)
        {
            std::vector<uint32_t> strides;
            strides.resize(pipeline->vertexBufferLayouts.size(), 0u);
            for (size_t i = 0; i < pipeline->vertexBufferLayouts.size(); ++i)
            {
                const auto &layout = pipeline->vertexBufferLayouts[i];
                if (layout.used)
                    strides[i] = static_cast<uint32_t>(layout.arrayStride);
            }
            pe::Dx12CommandBufferImpl::From(cmd)->BindExternalRenderPipeline(
                PeFromBackendHandle<ID3D12PipelineState *>(pipeline->backendPipeline),
                ToDx12Topology(pipeline->primitiveTopology),
                strides);
            return true;
        }
#else
            return BackendUnsupported("render pipeline bind");
#endif
        default:
            return BackendUnsupported("render pipeline bind");
        }
    }

    bool BindWebGPUComputePipeline(pe::CommandBuffer *cmd,
                                   WGPUComputePipelineImpl *pipeline,
                                   WebGPUBindingCache *cache)
    {
        if (!cmd || !pipeline || pipeline->backendPipeline == 0)
            return false;

        switch (pe::GetRHI().GetApi())
        {
        case PE_GRAPHICS_API_VULKAN:
            if (cache && cache->computePipeline == pipeline)
                return true;
            pe::GetVulkanCommandBuffer(cmd).bindPipeline(
                vk::PipelineBindPoint::eCompute,
                vk::Pipeline{PeFromBackendHandle<VkPipeline>(pipeline->backendPipeline)});
            if (cache)
                cache->computePipeline = pipeline;
            return true;
        case PE_GRAPHICS_API_DX12:
#if defined(PE_WIN32)
            pe::Dx12CommandBufferImpl::From(cmd)->BindExternalComputePipeline(
                PeFromBackendHandle<ID3D12PipelineState *>(pipeline->backendPipeline));
            return true;
#else
            return BackendUnsupported("compute pipeline bind");
#endif
        default:
            return BackendUnsupported("compute pipeline bind");
        }
    }

    bool BindWebGPUBindGroup(pe::CommandBuffer *cmd,
                             PipelineBindingPoint point,
                             WGPUPipelineLayoutImpl *layout,
                             uint32_t groupIndex,
                             WGPUBindGroupImpl *group,
                             size_t dynamicOffsetCount,
                             const uint32_t *dynamicOffsets,
                             WebGPUBindingCache *cache)
    {
        if (!cmd || !layout || !group)
            return false;

        const auto &bgls = layout->bindGroupLayouts;
        if (groupIndex >= bgls.size() || !bgls[groupIndex])
            return false;
        if (!BglGroupEquivalent(group->layout, bgls[groupIndex]))
            return false;

        switch (pe::GetRHI().GetApi())
        {
        case PE_GRAPHICS_API_VULKAN:
        {
            if (layout->backendLayout == 0)
                return false;

            NoteBindingModel(cache, layout);

            if (layout->bindingModel == WGPUBindingModel::DescriptorBuffer)
            {
                if (!group->descriptorBufferValid || dynamicOffsetCount != 0)
                    return false;

                if (IsCachedBindGroup(cache, point, layout, groupIndex, group,
                                      dynamicOffsetCount, dynamicOffsets))
                    return true;

                if (!EnsureDescriptorBufferBound(cmd, layout->device, cache))
                    return false;

                vk::PipelineLayout vkLayout{
                    PeFromBackendHandle<VkPipelineLayout>(layout->backendLayout)};
                const uint32_t bufferIndex = 0;
                const vk::DeviceSize offset =
                    static_cast<vk::DeviceSize>(group->descriptorBufferOffset);
                pe::GetVulkanCommandBuffer(cmd).setDescriptorBufferOffsetsEXT(
                    ToVkBindPoint(point), vkLayout, groupIndex, 1,
                    &bufferIndex, &offset);
                CacheBindGroup(cache, point, layout, groupIndex, group,
                               dynamicOffsetCount, dynamicOffsets);
                return true;
            }

            if (!group->descriptor)
                return false;

            if (IsCachedBindGroup(cache, point, layout, groupIndex, group,
                                  dynamicOffsetCount, dynamicOffsets))
                return true;

            vk::PipelineLayout vkLayout{
                PeFromBackendHandle<VkPipelineLayout>(layout->backendLayout)};
            vk::DescriptorSet ds = pe::GetVulkanDescriptorSet(group->descriptor);
            pe::GetVulkanCommandBuffer(cmd).bindDescriptorSets(
                ToVkBindPoint(point), vkLayout, groupIndex, 1, &ds,
                static_cast<uint32_t>(dynamicOffsetCount), dynamicOffsets);
            CacheBindGroup(cache, point, layout, groupIndex, group,
                           dynamicOffsetCount, dynamicOffsets);
            return true;
        }
        case PE_GRAPHICS_API_DX12:
#if defined(PE_WIN32)
        {
            constexpr uint32_t kWebGPUDescriptorSourceSpace = 0;
            if (groupIndex >= pe::DX12_DESCRIPTOR_SPACE_COUNT)
                return false;
            if (!group->descriptor)
                return false;
            if (point == PipelineBindingPoint::Compute)
            {
                pe::Dx12CommandBufferImpl::From(cmd)->BindExternalComputeDescriptorSpace(
                    group->descriptor, kWebGPUDescriptorSourceSpace, groupIndex);
            }
            else
            {
                pe::Dx12CommandBufferImpl::From(cmd)->BindExternalRenderDescriptorSpace(
                    group->descriptor, kWebGPUDescriptorSourceSpace, groupIndex);
            }
            return BindDx12DynamicTables(
                cmd, point, kWebGPUDescriptorSourceSpace, groupIndex, group,
                dynamicOffsetCount, dynamicOffsets);
        }
#else
            return BackendUnsupported("bind group bind");
#endif
        default:
            return BackendUnsupported("bind group bind");
        }
    }

    void FlushPendingBarriers(pe::CommandBuffer *cmd)
    {
        if (cmd && pe::GetRHI().GetApi() == PE_GRAPHICS_API_VULKAN)
            pe::VulkanCommandBufferImpl::From(cmd)->FlushBarriers();
    }

    bool DispatchWebGPUCompute(pe::CommandBuffer *cmd, uint32_t x, uint32_t y, uint32_t z)
    {
        if (!cmd)
            return false;

        switch (pe::GetRHI().GetApi())
        {
        case PE_GRAPHICS_API_VULKAN:
            FlushPendingBarriers(cmd);
            pe::GetVulkanCommandBuffer(cmd).dispatch(x, y, z);
            return true;
        case PE_GRAPHICS_API_DX12:
#if defined(PE_WIN32)
            pe::Dx12CommandBufferImpl::From(cmd)->DispatchExternalCompute(x, y, z);
            return true;
#else
            return BackendUnsupported("compute dispatch");
#endif
        default:
            return BackendUnsupported("compute dispatch");
        }
    }

    bool BindWebGPUVertexBuffer(pe::CommandBuffer *cmd,
                                pe::Buffer *buffer,
                                size_t offset,
                                uint32_t firstBinding,
                                uint32_t bindingCount,
                                WebGPUBindingCache *cache)
    {
        if (!cmd || !buffer)
            return false;

        switch (pe::GetRHI().GetApi())
        {
        case PE_GRAPHICS_API_VULKAN:
        {
            if (IsCachedVertexBuffer(cache, firstBinding, buffer, offset, bindingCount))
                return true;

            vk::Buffer vkBuffer = pe::GetVulkanBuffer(buffer);
            vk::DeviceSize vkOffset = static_cast<vk::DeviceSize>(offset);
            pe::GetVulkanCommandBuffer(cmd).bindVertexBuffers(
                firstBinding, bindingCount, &vkBuffer, &vkOffset);
            CacheVertexBuffer(cache, firstBinding, buffer, offset, bindingCount);
            return true;
        }
        case PE_GRAPHICS_API_DX12:
#if defined(PE_WIN32)
            cmd->BindVertexBuffer(buffer, offset, firstBinding, bindingCount);
            return true;
#else
            return false;
#endif
        default:
            return false;
        }
    }

    bool BindWebGPUIndexBuffer(pe::CommandBuffer *cmd,
                               pe::Buffer *buffer,
                               size_t offset,
                               PeIndexType indexType,
                               WebGPUBindingCache *cache)
    {
        if (!cmd || !buffer)
            return false;

        switch (pe::GetRHI().GetApi())
        {
        case PE_GRAPHICS_API_VULKAN:
        {
            if (IsCachedIndexBuffer(cache, buffer, offset, indexType))
                return true;

            pe::GetVulkanCommandBuffer(cmd).bindIndexBuffer(
                pe::GetVulkanBuffer(buffer), offset, pe::ToVkIndexType(indexType));
            CacheIndexBuffer(cache, buffer, offset, indexType);
            return true;
        }
        case PE_GRAPHICS_API_DX12:
#if defined(PE_WIN32)
            cmd->BindIndexBuffer(buffer, offset, indexType);
            return true;
#else
            return false;
#endif
        default:
            return false;
        }
    }

    void DrawWebGPU(pe::CommandBuffer *cmd,
                    uint32_t vertexCount,
                    uint32_t instanceCount,
                    uint32_t firstVertex,
                    uint32_t firstInstance)
    {
        if (!cmd)
            return;

        switch (pe::GetRHI().GetApi())
        {
        case PE_GRAPHICS_API_VULKAN:
            pe::GetVulkanCommandBuffer(cmd).draw(
                vertexCount, instanceCount, firstVertex, firstInstance);
            return;
        case PE_GRAPHICS_API_DX12:
#if defined(PE_WIN32)
            cmd->Draw(vertexCount, instanceCount, firstVertex, firstInstance);
#endif
            return;
        default:
            return;
        }
    }

    void DrawIndexedWebGPU(pe::CommandBuffer *cmd,
                           uint32_t indexCount,
                           uint32_t instanceCount,
                           uint32_t firstIndex,
                           int32_t vertexOffset,
                           uint32_t firstInstance)
    {
        if (!cmd)
            return;

        switch (pe::GetRHI().GetApi())
        {
        case PE_GRAPHICS_API_VULKAN:
            pe::GetVulkanCommandBuffer(cmd).drawIndexed(
                indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
            return;
        case PE_GRAPHICS_API_DX12:
#if defined(PE_WIN32)
            cmd->DrawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
#endif
            return;
        default:
            return;
        }
    }

    void RebindWebGPUCompatibleBindGroups(
        pe::CommandBuffer *cmd,
        PipelineBindingPoint point,
        WGPUPipelineLayoutImpl *layout,
        const std::vector<WGPUBindGroupImpl *> &currentBindGroups,
        const std::vector<std::vector<uint32_t>> *currentDynamicOffsets,
        WebGPUBindingCache *cache)
    {
        if (!cmd || !layout)
            return;

        const auto &bgls = layout->bindGroupLayouts;
        for (size_t i = 0; i < currentBindGroups.size() && i < bgls.size(); ++i)
        {
            WGPUBindGroupImpl *bg = currentBindGroups[i];
            if (!bg || !bg->descriptor || !bgls[i])
                continue;
            if (!BglGroupEquivalent(bg->layout, bgls[i]))
                continue;

            const std::vector<uint32_t> *dynOffsets =
                (currentDynamicOffsets && i < currentDynamicOffsets->size())
                    ? &(*currentDynamicOffsets)[i]
                    : nullptr;
            BindWebGPUBindGroup(cmd, point, layout, static_cast<uint32_t>(i), bg,
                                dynOffsets ? dynOffsets->size() : 0u,
                                (dynOffsets && !dynOffsets->empty()) ? dynOffsets->data() : nullptr,
                                cache);
        }
    }
} // namespace pwgpu
