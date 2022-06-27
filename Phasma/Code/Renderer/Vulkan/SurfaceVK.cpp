/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

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

        m_handle = surfaceVK;

        int w, h;
        SDL_Vulkan_GetDrawableSize(window, &w, &h);

        actualExtent = Rect2D{0, 0, w, h};
    }

    Surface::~Surface()
    {
        if (m_handle)
        {
            vkDestroySurfaceKHR(RHII.GetInstance(), m_handle, nullptr);
            m_handle = {};
        }
    }

    void Surface::CheckTransfer()
    {
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(RHII.GetGpu(), m_handle, &capabilities);

        // Ensure eTransferSrc bit for blit operations
        if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
            PE_ERROR("Surface doesnt support VK_IMAGE_USAGE_TRANSFER_SRC_BIT");
    }

    void Surface::FindFormat()
    {
        uint32_t formatsCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(RHII.GetGpu(), m_handle, &formatsCount, nullptr);

        std::vector<VkSurfaceFormatKHR> formats(formatsCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(RHII.GetGpu(), m_handle, &formatsCount, formats.data());

        format = Translate<Format>(formats[0].format);
        colorSpace = Translate<ColorSpace>(formats[0].colorSpace);
        for (const auto &f : formats)
        {
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                format = Translate<Format>(f.format);
                colorSpace = Translate<ColorSpace>(f.colorSpace);
            }
        }

        // Check for blit operation
        VkFormatProperties fProps;
        vkGetPhysicalDeviceFormatProperties(RHII.GetGpu(), Translate<VkFormat>(format), &fProps);
        if (!(fProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT))
            PE_ERROR("No blit source operation supported");
        if (!(fProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT))
            PE_ERROR("No blit destination operation supported");
    }

    void Surface::FindPresentationMode()
    {
        uint32_t presentModesCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(RHII.GetGpu(), m_handle, &presentModesCount, nullptr);

        std::vector<VkPresentModeKHR> presentModes(presentModesCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(RHII.GetGpu(), m_handle, &presentModesCount, presentModes.data());

        for (const auto &i : presentModes)
            if (i == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                presentMode = Translate<PresentMode>(i);
                return;
            }

        for (const auto &i : presentModes)
            if (i == VK_PRESENT_MODE_IMMEDIATE_KHR)
            {
                presentMode = Translate<PresentMode>(i);
                return;
            }

        presentMode = Translate<PresentMode>(VK_PRESENT_MODE_FIFO_KHR);
    }

    void Surface::FindProperties()
    {
        CheckTransfer();
        FindFormat();
        FindPresentationMode();
    }
}
#endif
