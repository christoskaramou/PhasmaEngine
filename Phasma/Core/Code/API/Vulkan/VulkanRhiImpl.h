#pragma once

#include "API/Vulkan/VulkanHeaders.h"

#include "API/RHI_Internal.h"

#ifdef PE_TRACY
namespace tracy
{
    class VkCtx;
}
#endif

namespace pe
{
    struct VulkanRhiImpl final : public RHI::Impl
    {
        bool Init(SDL_Window *window) override;
        void Shutdown() override;
        void WaitDeviceIdle() override;
        void NextFrame() override;
        GpuMemorySnapshot GetGpuMemorySnapshot() override;

        vk::Instance m_instance{};
        vk::PhysicalDevice m_gpu{};
        vk::Device m_device{};
        VmaAllocator m_allocator = nullptr;
#ifdef PE_TRACY
        tracy::VkCtx *m_tracyVkCtx = nullptr;
#endif
    };
} // namespace pe
