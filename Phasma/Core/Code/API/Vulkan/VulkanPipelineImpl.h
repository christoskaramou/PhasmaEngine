#pragma once

#include "API/Vulkan/VulkanHeaders.h"

#include "API/Pipeline_Internal.h"

namespace pe
{
    struct VulkanPipelineImpl final : public Pipeline::Impl
    {
        VulkanPipelineImpl(Pipeline *owner, RenderPass *renderPass, PassInfo &info);
        ~VulkanPipelineImpl() override;

        static VulkanPipelineImpl *From(Pipeline *pipeline) { return static_cast<VulkanPipelineImpl *>(pipeline->m_impl); }
        static const VulkanPipelineImpl *From(const Pipeline *pipeline) { return static_cast<const VulkanPipelineImpl *>(pipeline->m_impl); }

        void CreateRayTracingPipeline();
        void CreateComputePipeline();
        void CreateGraphicsPipeline(RenderPass *renderPass);
        void CreateSBT();

        Pipeline *m_owner{};
        PassInfo &m_info;
        vk::Pipeline m_pipeline{};
        vk::PipelineLayout m_layout{};
        vk::PipelineCache m_cache{};
        Buffer *m_sbtBuffer = nullptr;
        vk::StridedDeviceAddressRegionKHR m_rgenRegion{};
        vk::StridedDeviceAddressRegionKHR m_missRegion{};
        vk::StridedDeviceAddressRegionKHR m_hitRegion{};
        vk::StridedDeviceAddressRegionKHR m_callRegion{};
    };

    inline vk::Pipeline GetVulkanPipeline(Pipeline *pipeline)
    {
        return pipeline ? VulkanPipelineImpl::From(pipeline)->m_pipeline : vk::Pipeline{};
    }

    inline vk::PipelineLayout GetVulkanPipelineLayout(Pipeline *pipeline)
    {
        return pipeline ? VulkanPipelineImpl::From(pipeline)->m_layout : vk::PipelineLayout{};
    }

    inline const vk::StridedDeviceAddressRegionKHR &GetVulkanRgenRegion(Pipeline *pipeline)
    {
        return VulkanPipelineImpl::From(pipeline)->m_rgenRegion;
    }

    inline const vk::StridedDeviceAddressRegionKHR &GetVulkanMissRegion(Pipeline *pipeline)
    {
        return VulkanPipelineImpl::From(pipeline)->m_missRegion;
    }

    inline const vk::StridedDeviceAddressRegionKHR &GetVulkanHitRegion(Pipeline *pipeline)
    {
        return VulkanPipelineImpl::From(pipeline)->m_hitRegion;
    }

    inline const vk::StridedDeviceAddressRegionKHR &GetVulkanCallRegion(Pipeline *pipeline)
    {
        return VulkanPipelineImpl::From(pipeline)->m_callRegion;
    }
} // namespace pe
