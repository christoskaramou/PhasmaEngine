#include "API/Vulkan/VulkanRhiImpl.h"
#include "API/Vulkan/RHI_Vulkan.h"

namespace pe
{
    void VulkanRhiImpl::WaitDeviceIdle()
    {
        VulkanRhi::Device().waitIdle();
    }
} // namespace pe
