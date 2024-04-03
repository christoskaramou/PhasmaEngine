#if PE_VULKAN
#include "Renderer/Surface.h"
#include "Systems/RendererSystem.h"
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

        VkFormat formatVK = formats[0].format;
        VkColorSpaceKHR colorSpaceVK = formats[0].colorSpace;
        for (const auto &f : formats)
        {
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                formatVK = f.format;
                colorSpaceVK = f.colorSpace;
                break;
            }
        }
        
        m_format = Translate<Format>(formatVK);
        m_colorSpace = Translate<ColorSpace>(colorSpaceVK);

        // Check for blit operation
        VkFormatProperties fProps;
        vkGetPhysicalDeviceFormatProperties(RHII.GetGpu(), Translate<VkFormat>(m_format), &fProps);
        if (!(fProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT))
            PE_ERROR("No blit source operation supported");
        if (!(fProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT))
            PE_ERROR("No blit destination operation supported");
    }

    void Surface::FindPresentationMode()
    {
        uint32_t presentModesCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(RHII.GetGpu(), m_apiHandle, &presentModesCount, nullptr);

        std::vector<VkPresentModeKHR> presentModes(presentModesCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(RHII.GetGpu(), m_apiHandle, &presentModesCount, presentModes.data());

        for (const auto &i : presentModes)
        {
            if (i == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                m_presentMode = Translate<PresentMode>(i);
                return;
            }
        }

        for (const auto &i : presentModes)
        {
            if (i == VK_PRESENT_MODE_IMMEDIATE_KHR)
            {
                m_presentMode = Translate<PresentMode>(i);
                return;
            }
        }

        m_presentMode = Translate<PresentMode>(VK_PRESENT_MODE_FIFO_KHR);
    }

    void Surface::FindProperties()
    {
        CheckTransfer();
        FindFormat();
        FindPresentationMode();
    }
}
#endif
