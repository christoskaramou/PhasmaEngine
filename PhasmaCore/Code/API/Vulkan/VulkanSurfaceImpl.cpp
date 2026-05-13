#include "API/Vulkan/VulkanSurfaceImpl.h"
#include "API/RHI.h"
#include "API/Surface.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Vulkan/VulkanRHITypeUtils.h"

#include "SDL2/SDL_vulkan.h"

namespace pe
{
    VulkanSurfaceImpl::VulkanSurfaceImpl(Surface *owner, SDL_Window *window)
        : m_owner{owner}
    {
        VkSurfaceKHR surfaceVK;
        SDL_bool res = SDL_Vulkan_CreateSurface(window, VulkanRhi::Instance(), &surfaceVK);
        PE_ERROR_IF(!res, SDL_GetError());

        m_apiHandle = surfaceVK;

        int w, h;
        SDL_Vulkan_GetDrawableSize(window, &w, &h);
        m_owner->m_actualExtent = Rect2Du{0, 0, static_cast<uint32_t>(w), static_cast<uint32_t>(h)};

        // Check transfer support
        auto capabilities = VulkanRhi::Gpu().getSurfaceCapabilitiesKHR(m_apiHandle);
        // Ensure blit operations
        vk::ImageUsageFlags flags = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
        PE_ERROR_IF(!(capabilities.supportedUsageFlags & flags), "Surface doesnt support nessesary operations");

        // Find format
        auto formats = VulkanRhi::Gpu().getSurfaceFormatsKHR(m_apiHandle);
        m_owner->m_format = PE_FORMAT_UNDEFINED;
        for (const auto &format : formats)
        {
            if ((format.format == vk::Format::eB8G8R8A8Unorm || format.format == vk::Format::eR8G8B8A8Unorm) &&
                format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
            {
                m_owner->m_format = FromVkFormat(format.format);
                m_owner->m_colorSpace = PE_COLOR_SPACE_SRGB_NONLINEAR;
                break;
            }
        }
        PE_ERROR_IF(m_owner->m_format == PE_FORMAT_UNDEFINED, "Surface format not found");
    }

    VulkanSurfaceImpl::~VulkanSurfaceImpl()
    {
        if (m_apiHandle)
            VulkanRhi::Instance().destroySurfaceKHR(m_apiHandle);
    }

    std::vector<PePresentMode> VulkanSurfaceImpl::GetSupportedPresentModes() const
    {
        if (RHII.UsesDozenVulkan())
            return {PE_PRESENT_MODE_FIFO};

        const auto vkModes = VulkanRhi::Gpu().getSurfacePresentModesKHR(m_apiHandle);
        std::vector<PePresentMode> out;
        out.reserve(vkModes.size());
        for (auto vkMode : vkModes)
        {
            // Skip modes outside the neutral set; FromVkPresentMode would collapse them to FIFO and dup it.
            switch (vkMode)
            {
            case vk::PresentModeKHR::eImmediate:
            case vk::PresentModeKHR::eMailbox:
            case vk::PresentModeKHR::eFifo:
            case vk::PresentModeKHR::eFifoRelaxed:
                out.push_back(FromVkPresentMode(vkMode));
                break;
            default:
                break;
            }
        }
        return out;
    }
} // namespace pe
