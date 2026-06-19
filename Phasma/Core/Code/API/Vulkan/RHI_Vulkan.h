#pragma once

#include "API/Vulkan/VulkanHeaders.h"

#include "API/RHI.h"
#include "API/Vulkan/VulkanBufferImpl.h"
#include "API/Vulkan/VulkanDescriptorImpl.h"
#include "API/Vulkan/VulkanImageImpl.h"
#include "API/Vulkan/VulkanImageViewImpl.h"
#include "API/Vulkan/VulkanRhiImpl.h"
#include "API/Vulkan/VulkanSamplerImpl.h"
#include "API/Vulkan/VulkanShaderImpl.h"

#ifdef PE_TRACY
namespace tracy
{
    class VkCtx;
}
using TracyVkCtx = tracy::VkCtx *;
#endif

namespace pe
{
    class AccelerationStructure;

    vk::Format GetVulkanDepthFormat();

    struct VulkanRhi
    {
        static VulkanRhiImpl *Impl() { return static_cast<VulkanRhiImpl *>(RHII.GetImpl()); }
        static vk::Instance Instance() { return Impl()->m_instance; }
        static vk::PhysicalDevice Gpu() { return Impl()->m_gpu; }
        static vk::Device Device() { return Impl()->m_device; }
        static VmaAllocator Allocator() { return Impl()->m_allocator; }
        static vk::Format DepthFormat() { return GetVulkanDepthFormat(); }
#ifdef PE_TRACY
        static TracyVkCtx TracyContext() { return Impl()->m_tracyVkCtx; }
#endif
    };
} // namespace pe
