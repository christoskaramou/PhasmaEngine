#pragma once

#include "API/Vulkan/VulkanHeaders.h"

#include "API/QueryPool_Internal.h"

namespace pe
{
    class VulkanQueryPool final : public QueryPool::Impl
    {
    public:
        VulkanQueryPool(const QueryPoolDesc &desc);
        ~VulkanQueryPool() override;

        vk::QueryPool Handle() const { return m_pool; }
        PeQueryType Type() const { return m_type; }

        static VulkanQueryPool *From(QueryPool *pool)
        {
            return pool ? static_cast<VulkanQueryPool *>(pool->GetImpl()) : nullptr;
        }

    private:
        vk::QueryPool m_pool{};
        PeQueryType m_type = PE_QUERY_TYPE_OCCLUSION;
    };
} // namespace pe
