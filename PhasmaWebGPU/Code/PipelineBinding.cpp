#include "PipelineBinding.h"

#include "BindGroup.h"
#include "ComputePipeline.h"
#include "PipelineLayout.h"
#include "RenderPipeline.h"

#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Vulkan/VulkanCommandBufferImpl.h"

#if defined(PE_WIN32)
#include "API/DX12/Dx12CommandBufferImpl.h"
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
            if (dynamicOffsetCount != 0)
                return BackendUnsupported("dynamic-offset bind group bind");
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
            return true;
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
