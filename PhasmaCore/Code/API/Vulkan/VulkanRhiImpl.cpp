#include "API/Vulkan/VulkanRhiImpl.h"
#include "API/Vulkan/RHI_Vulkan.h"

namespace pe
{
    // Empty: Vulkan setup still lives in RHI::CreateInstance/FindGpu/CreateDevice/...
    bool VulkanRhiImpl::Init(SDL_Window *)
    {
        return true;
    }

    void VulkanRhiImpl::Shutdown() {}

    void VulkanRhiImpl::WaitDeviceIdle()
    {
        VulkanRhi::Device().waitIdle();
    }

    void VulkanRhiImpl::NextFrame() {}
} // namespace pe
