#include "API/QueryPool_Internal.h"

#include "API/RHI.h"
#include "API/Vulkan/VulkanQueryPool.h"

#if defined(PE_WIN32)
#include "API/DX12/Dx12QueryPool.h"
#endif

namespace pe
{
    std::unique_ptr<QueryPool::Impl> CreateQueryPoolImpl(const QueryPoolDesc &desc)
    {
        switch (RHII.GetApi())
        {
        case PE_GRAPHICS_API_VULKAN:
            return std::make_unique<VulkanQueryPool>(desc);
        case PE_GRAPHICS_API_DX12:
#if defined(PE_WIN32)
            return std::make_unique<Dx12QueryPool>(desc);
#else
            PE_ERROR("CreateQueryPoolImpl: DX12 backend is Windows-only");
            return nullptr;
#endif
        default:
            PE_ERROR("CreateQueryPoolImpl: unsupported graphics api %u", static_cast<uint32_t>(RHII.GetApi()));
            return nullptr;
        }
    }
} // namespace pe
