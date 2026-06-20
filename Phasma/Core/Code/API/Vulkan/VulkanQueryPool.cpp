#include "API/Vulkan/VulkanQueryPool.h"

#include "API/Debug.h"
#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"

namespace pe
{
    namespace
    {
        vk::QueryType ToVkQueryType(PeQueryType type)
        {
            switch (type)
            {
            case PE_QUERY_TYPE_TIMESTAMP:
                return vk::QueryType::eTimestamp;
            case PE_QUERY_TYPE_OCCLUSION:
            default:
                return vk::QueryType::eOcclusion;
            }
        }
    } // namespace

    VulkanQueryPool::VulkanQueryPool(const QueryPoolDesc &desc)
        : m_type{desc.type}
    {
        PE_ERROR_IF(desc.count == 0, "VulkanQueryPool: query count must be non-zero");

        vk::QueryPoolCreateInfo ci{};
        ci.queryType = ToVkQueryType(desc.type);
        ci.queryCount = desc.count;

        m_pool = VulkanRhi::Device().createQueryPool(ci);
        PE_ERROR_IF(!m_pool, "VulkanQueryPool: vkCreateQueryPool failed");

        // A fresh VkQueryPool is in an undefined state and must be reset before use.
        // Reset on the host when available (the engine enables hostQueryReset at device
        // init); otherwise the first CommandBuffer::ResetQueryPool handles it.
        if (RHII.GetGpuFeatureSupport().queryPoolHostReset)
            VulkanRhi::Device().resetQueryPool(m_pool, 0, desc.count);

        if (!desc.name.empty())
            Debug::SetObjectName(m_pool, desc.name);
    }

    VulkanQueryPool::~VulkanQueryPool()
    {
        if (m_pool)
            VulkanRhi::Device().destroyQueryPool(m_pool);
    }
} // namespace pe
