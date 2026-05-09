#include "PipelineBinding.h"

#include "BindGroup.h"
#include "ComputePipeline.h"
#include "PipelineLayout.h"
#include "RenderPipeline.h"

#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Vulkan/VulkanCommandBufferImpl.h"

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
        if (!cmd || !layout || !group || !group->descriptor || layout->backendLayout == 0)
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
            vk::PipelineLayout vkLayout{
                PeFromBackendHandle<VkPipelineLayout>(layout->backendLayout)};
            vk::DescriptorSet ds = pe::GetVulkanDescriptorSet(group->descriptor);
            pe::GetVulkanCommandBuffer(cmd).bindDescriptorSets(
                ToVkBindPoint(point), vkLayout, groupIndex, 1, &ds,
                static_cast<uint32_t>(dynamicOffsetCount), dynamicOffsets);
            return true;
        }
        default:
            return BackendUnsupported("bind group bind");
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
