#ifdef PE_VULKAN
#include "Renderer/Surface.h"
#include "Renderer/RHI.h"

namespace pe
{
    Surface::Surface(SDL_Window *window)
    {
        VkSurfaceKHR surfaceVK;
        if (!SDL_Vulkan_CreateSurface(window, RHII.GetInstance(), &surfaceVK))
            PE_ERROR(SDL_GetError());

        m_apiHandle = surfaceVK;

        int w, h;
        SDL_Vulkan_GetDrawableSize(window, &w, &h);
        m_actualExtent = Rect2Du{0, 0, static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
    }

    Surface::~Surface()
    {
        if (m_apiHandle)
        {
            vkDestroySurfaceKHR(RHII.GetInstance(), m_apiHandle, nullptr);
            m_apiHandle = {};
        }
    }

    void Surface::CheckTransfer()
    {
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(RHII.GetGpu(), m_apiHandle, &capabilities);

        // Ensure eTransferSrc bit for blit operations
        if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
            PE_ERROR("Surface doesnt support VK_IMAGE_USAGE_TRANSFER_SRC_BIT");
    }

    void Surface::FindFormat()
    {
        uint32_t formatsCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(RHII.GetGpu(), m_apiHandle, &formatsCount, nullptr);

        std::vector<VkSurfaceFormatKHR> formats(formatsCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(RHII.GetGpu(), m_apiHandle, &formatsCount, formats.data());

        m_format = Format::Undefined;
        for (const auto &f : formats)
        {
            VkFormatProperties fProps;
            vkGetPhysicalDeviceFormatProperties(RHII.GetGpu(), Translate<VkFormat>(m_format), &fProps);

            if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                m_format = Translate<Format>(f.format);
                m_colorSpace = Translate<ColorSpace>(f.colorSpace);
                break;
            }
        }

        PE_ERROR_IF(m_format == Format::Undefined, "Surface format not found");
    }

    bool SupportsPresentMode(VkSurfaceKHR surface, VkPresentModeKHR mode)
    {
        static std::vector<VkPresentModeKHR> presentModes{};
        if (presentModes.empty())
        {
            uint32_t presentModesCount;
            vkGetPhysicalDeviceSurfacePresentModesKHR(RHII.GetGpu(), surface, &presentModesCount, nullptr);
            presentModes.resize(presentModesCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(RHII.GetGpu(), surface, &presentModesCount, presentModes.data());
        }

        for (const auto &presentMode : presentModes)
        {
            if (presentMode == mode)
                return true;
        }

        return false;
    }

    void Surface::SetPresentMode(PresentMode preferredMode)
    {
        if (SupportsPresentMode(m_apiHandle, Translate<VkPresentModeKHR>(preferredMode)))
        {
            m_presentMode = preferredMode;
        }
        else
        {
            if (SupportsPresentMode(m_apiHandle, VK_PRESENT_MODE_MAILBOX_KHR))
                m_presentMode = PresentMode::Mailbox;
            else if (SupportsPresentMode(m_apiHandle, VK_PRESENT_MODE_IMMEDIATE_KHR))
                m_presentMode = PresentMode::Immediate;
            else if (SupportsPresentMode(m_apiHandle, VK_PRESENT_MODE_FIFO_RELAXED_KHR))
                m_presentMode = PresentMode::FifoRelaxed;
            else
                m_presentMode = PresentMode::Fifo;
        }
    }

    void Surface::FindProperties()
    {
        CheckTransfer();
        FindFormat();
        SetPresentMode(PresentMode::Mailbox);
    }
}
#endif
