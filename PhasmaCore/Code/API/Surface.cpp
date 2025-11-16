#include "API/Surface.h"
#include "API/RHI.h"

namespace pe
{
    Surface::Surface(SDL_Window *window)
        : m_format{},
          m_colorSpace{},
          m_presentMode{}
    {
        VkSurfaceKHR surfaceVK;
        SDL_bool res = SDL_Vulkan_CreateSurface(window, RHII.GetInstance(), &surfaceVK);
        PE_ERROR_IF(!res, SDL_GetError());

        m_apiHandle = surfaceVK;

        int w, h;
        SDL_Vulkan_GetDrawableSize(window, &w, &h);
        m_actualExtent = Rect2Du{0, 0, static_cast<uint32_t>(w), static_cast<uint32_t>(h)};

        // Check transfer support
        auto capabilities = RHII.GetGpu().getSurfaceCapabilitiesKHR(m_apiHandle);
        // Ensure blit operations
        vk::ImageUsageFlags flags = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
        PE_ERROR_IF(!(capabilities.supportedUsageFlags & flags), "Surface doesnt support nessesary operations");

        // Find format
        auto formats = RHII.GetGpu().getSurfaceFormatsKHR(m_apiHandle);
        m_format = vk::Format::eUndefined;
        for (const auto &format : formats)
        {
            if ((format.format == vk::Format::eB8G8R8A8Unorm || format.format == vk::Format::eR8G8B8A8Unorm) &&
                format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
            {
                m_format = format.format;
                m_colorSpace = format.colorSpace;
                break;
            }
        }
        PE_ERROR_IF(m_format == vk::Format::eUndefined, "Surface format not found");

        SetPresentMode(vk::PresentModeKHR::eFifo);
    }

    Surface::~Surface()
    {
        if (m_apiHandle)
            RHII.GetInstance().destroySurfaceKHR(m_apiHandle);
    }

    std::vector<vk::PresentModeKHR> Surface::GetSupportedPresentModes() const
    {
        return RHII.GetGpu().getSurfacePresentModesKHR(m_apiHandle);
    }

    void Surface::SetPresentMode(vk::PresentModeKHR preferredMode)
    {
        const auto &presentModes = GetSupportedPresentModes();
        for (const auto &presentMode : presentModes)
        {
            if (presentMode == preferredMode)
                return void(m_presentMode = presentMode);
        }

        m_presentMode = vk::PresentModeKHR::eFifo;
    }
}
