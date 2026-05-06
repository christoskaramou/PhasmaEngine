#include "API/Vulkan/VulkanGpuTimerImpl.h"

#include "API/Command.h"
#include "API/Debug.h"
#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Vulkan/VulkanCommandBufferImpl.h"

namespace pe
{
    VulkanGpuTimerImpl::VulkanGpuTimerImpl(const std::string &name)
    {
        VkPhysicalDeviceProperties gpuProps;
        vkGetPhysicalDeviceProperties(VulkanRhi::Gpu(), &gpuProps);
        PE_ERROR_IF(!gpuProps.limits.timestampComputeAndGraphics, "Timestamps not supported");

        m_timestampPeriod = gpuProps.limits.timestampPeriod;

        VkQueryPoolCreateInfo qpci{};
        qpci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        qpci.queryType = VK_QUERY_TYPE_TIMESTAMP;
        qpci.queryCount = 2;

        VkQueryPool pool;
        PE_CHECK(vkCreateQueryPool(VulkanRhi::Device(), &qpci, nullptr, &pool));
        m_pool = pool;

        Debug::SetObjectName(m_pool, name);
    }

    VulkanGpuTimerImpl::~VulkanGpuTimerImpl()
    {
        if (m_pool)
            vkDestroyQueryPool(VulkanRhi::Device(), m_pool, nullptr);
    }

    void VulkanGpuTimerImpl::Start(CommandBuffer *cmd)
    {
        PE_ERROR_IF(m_inUse, "GpuTimer::Start() called before End()");

        m_cmd = cmd;
        vkCmdResetQueryPool(GetVulkanCommandBuffer(m_cmd), m_pool, 0, 2);
        vkCmdWriteTimestamp(GetVulkanCommandBuffer(m_cmd), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_pool, 0);
        m_resultsReady = false;
        m_inUse = true;
    }

    void VulkanGpuTimerImpl::End()
    {
        PE_ERROR_IF(!m_inUse, "GpuTimer::End() called before Start()");

        vkCmdWriteTimestamp(GetVulkanCommandBuffer(m_cmd), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_pool, 1);
        m_resultsReady = false;
        m_inUse = false;
    }

    float VulkanGpuTimerImpl::GetTime()
    {
        PE_ERROR_IF(m_inUse, "GpuTimer::GetTime() called before End()");

        if (!m_resultsReady)
        {
            VkResult res = vkGetQueryPoolResults(VulkanRhi::Device(),
                                                 m_pool,
                                                 0,
                                                 2,
                                                 2 * sizeof(uint64_t),
                                                 &m_queries,
                                                 sizeof(uint64_t),
                                                 VK_QUERY_RESULT_64_BIT);

            if (res != VK_SUCCESS)
                return 0.f;

            m_cmd = nullptr;
            m_resultsReady = true;
        }

        return static_cast<float>(m_queries[1] - m_queries[0]) * m_timestampPeriod * 1e-6f; // ms
    }

    double VulkanGpuTimerImpl::GetStartTimeMs() const
    {
        return static_cast<double>(m_queries[0]) * static_cast<double>(m_timestampPeriod) * 1e-6;
    }
} // namespace pe
