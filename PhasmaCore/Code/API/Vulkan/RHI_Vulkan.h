#pragma once

#include "API/RHI.h"

namespace pe
{
    // Vulkan-only seam: anything including this header is, by definition, not cross-backend.
    struct VulkanRhi
    {
        static vk::Instance Instance() { return RHII.m_instance; }
        static vk::PhysicalDevice Gpu() { return RHII.m_gpu; }
        static vk::Device Device() { return RHII.m_device; }
        static VmaAllocator Allocator() { return RHII.m_allocator; }
        static vk::Format DepthFormat() { return RHII.GetDepthFormat(); }
#ifdef PE_TRACY
        static TracyVkCtx TracyContext() { return RHII.m_tracyVkCtx; }
#endif
    };
} // namespace pe
