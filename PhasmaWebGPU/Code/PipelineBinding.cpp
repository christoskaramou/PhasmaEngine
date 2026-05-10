#include "PipelineBinding.h"

#include "BindGroup.h"
#include "Buffer.h"
#include "ComputePipeline.h"
#include "PipelineLayout.h"
#include "RenderPipeline.h"

#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Vulkan/VulkanCommandBufferImpl.h"

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
                     operation, PeGraphicsApiName(pe::RHII.GetApi()));
            return false;
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
                    auto *rhi = static_cast<pe::Dx12RhiImpl *>(pe::RHII.GetImpl());
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
                auto *rhi = static_cast<pe::Dx12RhiImpl *>(pe::RHII.GetImpl());
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
                const uint32_t rootIdx = pe::Dx12CbvSrvUavRootIndex(table.dxSpace);
                if (point == PipelineBindingPoint::Compute)
                    list->SetComputeRootDescriptorTable(rootIdx, table.gpuHandle);
                else
                    list->SetGraphicsRootDescriptorTable(rootIdx, table.gpuHandle);
            }

            cmd->AddAfterWaitCallback(
                [dynamicTables = std::move(dynamicTables)]()
                {
                    auto *rhi = static_cast<pe::Dx12RhiImpl *>(pe::RHII.GetImpl());
                    if (!rhi || !rhi->GetCbvSrvUavHeap())
                        return;
                    for (const Dx12DynamicTable &table : dynamicTables)
                        rhi->GetCbvSrvUavHeap()->FreeRange(table.firstSlot, table.count);
                });
            return true;
        }
#endif
    } // namespace

    bool BindWebGPURenderPipeline(pe::CommandBuffer *cmd, WGPURenderPipelineImpl *pipeline)
    {
        if (!cmd || !pipeline || pipeline->backendPipeline == 0)
            return false;

        switch (pe::RHII.GetApi())
        {
        case PE_GRAPHICS_API_VULKAN:
            pe::GetVulkanCommandBuffer(cmd).bindPipeline(
                vk::PipelineBindPoint::eGraphics,
                vk::Pipeline{PeFromBackendHandle<VkPipeline>(pipeline->backendPipeline)});
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

    bool BindWebGPUComputePipeline(pe::CommandBuffer *cmd, WGPUComputePipelineImpl *pipeline)
    {
        if (!cmd || !pipeline || pipeline->backendPipeline == 0)
            return false;

        switch (pe::RHII.GetApi())
        {
        case PE_GRAPHICS_API_VULKAN:
            pe::GetVulkanCommandBuffer(cmd).bindPipeline(
                vk::PipelineBindPoint::eCompute,
                vk::Pipeline{PeFromBackendHandle<VkPipeline>(pipeline->backendPipeline)});
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
                             const uint32_t *dynamicOffsets)
    {
        if (!cmd || !layout || !group || !group->descriptor)
            return false;

        const auto &bgls = layout->bindGroupLayouts;
        if (groupIndex >= bgls.size() || !bgls[groupIndex])
            return false;
        if (!BglGroupEquivalent(group->layout, bgls[groupIndex]))
            return false;

        switch (pe::RHII.GetApi())
        {
        case PE_GRAPHICS_API_VULKAN:
        {
            if (layout->backendLayout == 0)
                return false;

            vk::PipelineLayout vkLayout{
                PeFromBackendHandle<VkPipelineLayout>(layout->backendLayout)};
            vk::DescriptorSet ds = pe::GetVulkanDescriptorSet(group->descriptor);
            pe::GetVulkanCommandBuffer(cmd).bindDescriptorSets(
                ToVkBindPoint(point), vkLayout, groupIndex, 1, &ds,
                static_cast<uint32_t>(dynamicOffsetCount), dynamicOffsets);
            return true;
        }
        case PE_GRAPHICS_API_DX12:
#if defined(PE_WIN32)
            if (point == PipelineBindingPoint::Compute)
            {
                pe::Dx12CommandBufferImpl::From(cmd)->BindExternalComputeDescriptors(
                    1, &group->descriptor);
            }
            else
            {
                pe::Dx12CommandBufferImpl::From(cmd)->BindExternalRenderDescriptors(
                    1, &group->descriptor);
            }
            return BindDx12DynamicTables(
                cmd, point, group, dynamicOffsetCount, dynamicOffsets);
#else
            return BackendUnsupported("bind group bind");
#endif
        default:
            return BackendUnsupported("bind group bind");
        }
    }

    bool DispatchWebGPUCompute(pe::CommandBuffer *cmd, uint32_t x, uint32_t y, uint32_t z)
    {
        if (!cmd)
            return false;

        switch (pe::RHII.GetApi())
        {
        case PE_GRAPHICS_API_VULKAN:
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

    void RebindWebGPUCompatibleBindGroups(
        pe::CommandBuffer *cmd,
        PipelineBindingPoint point,
        WGPUPipelineLayoutImpl *layout,
        const std::vector<WGPUBindGroupImpl *> &currentBindGroups,
        const std::vector<std::vector<uint32_t>> *currentDynamicOffsets)
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
                                (dynOffsets && !dynOffsets->empty()) ? dynOffsets->data() : nullptr);
        }
    }
} // namespace pwgpu
