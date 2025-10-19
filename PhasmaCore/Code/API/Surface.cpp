#include "API/Surface.h"
#include "API/RHI.h"

namespace pe
{
    Surface::Surface(SDL_Window *window)
        : m_format{vk::Format::eUndefined},
          m_colorSpace{},
          m_presentMode{vk::PresentModeKHR::eFifo},
          m_supportedPresentModes{}
    {
        VkSurfaceKHR surfaceVK;
        SDL_bool res = SDL_Vulkan_CreateSurface(window, RHII.GetInstance(), &surfaceVK);
        PE_ERROR_IF(!res, SDL_GetError());

        m_apiHandle = surfaceVK;

        int w, h;
        SDL_Vulkan_GetDrawableSize(window, &w, &h);
        m_actualExtent = Rect2Du{0, 0, static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
    }

    Surface::~Surface()
    {
        if (m_apiHandle)
            RHII.GetInstance().destroySurfaceKHR(m_apiHandle);
    }

    void Surface::CheckTransfer()
    {
        auto capabilities = RHII.GetGpu().getSurfaceCapabilitiesKHR(m_apiHandle);

        // Ensure blit operations
        vk::ImageUsageFlags flags = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
        PE_ERROR_IF(!(capabilities.supportedUsageFlags & flags), "Surface doesnt support nessesary operations");
    }

    void Surface::FindFormat()
    {
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
    }

    bool Surface::SupportsPresentMode(vk::PresentModeKHR mode)
    {
        m_supportedPresentModes = RHII.GetGpu().getSurfacePresentModesKHR(m_apiHandle);
        for (const auto &presentMode : m_supportedPresentModes)
        {
            if (presentMode == mode)
                return true;
        }

        return false;
    }

    void Surface::SetPresentMode(vk::PresentModeKHR preferredMode)
    {
        if (SupportsPresentMode(preferredMode))
        {
            m_presentMode = preferredMode;
        }
        else
        {
            if (SupportsPresentMode(vk::PresentModeKHR::eMailbox))
                m_presentMode = vk::PresentModeKHR::eMailbox;
            else if (SupportsPresentMode(vk::PresentModeKHR::eImmediate))
                m_presentMode = vk::PresentModeKHR::eImmediate;
            else if (SupportsPresentMode(vk::PresentModeKHR::eFifoRelaxed))
                m_presentMode = vk::PresentModeKHR::eFifoRelaxed;
            else
                m_presentMode = vk::PresentModeKHR::eFifo;
        }
    }

    void Surface::FindProperties()
    {
        CheckTransfer();
        FindFormat();
        SetPresentMode(vk::PresentModeKHR::eMailbox);
    }
}
