#ifdef PE_VULKAN
#include "API/Swapchain.h"
#include "API/RHI.h"
#include "API/Image.h"
#include "API/Semaphore.h"
#include "API/Surface.h"

namespace pe
{
    Swapchain::Swapchain(Surface *surface, const std::string &name)
        : m_images{}
    {
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(RHII.GetGpu(), surface->ApiHandle(), &capabilities);

        PE_ERROR_IF(SWAPCHAIN_IMAGES < capabilities.minImageCount, "Swapchain image count error");
        if (capabilities.maxImageCount > 0)
            PE_ERROR_IF(SWAPCHAIN_IMAGES > capabilities.maxImageCount, "Swapchain image count error");

        const Rect2Du &actualExtent = surface->GetActualExtent();
        m_extent.x = actualExtent.x;
        m_extent.y = actualExtent.y;
        m_extent.width = actualExtent.width;
        m_extent.height = actualExtent.height;

        VkSwapchainCreateInfoKHR swapchainCreateInfo{};
        swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainCreateInfo.surface = surface->ApiHandle();
        swapchainCreateInfo.minImageCount = SWAPCHAIN_IMAGES;
        swapchainCreateInfo.imageFormat = Translate<VkFormat>(surface->GetFormat());
        swapchainCreateInfo.imageColorSpace = Translate<VkColorSpaceKHR>(surface->GetColorSpace());
        swapchainCreateInfo.imageExtent = VkExtent2D{m_extent.width, m_extent.height};
        swapchainCreateInfo.imageArrayLayers = 1;
        swapchainCreateInfo.imageUsage = Translate<VkImageUsageFlags>(ImageUsage::ColorAttachmentBit | ImageUsage::TransferDstBit);
        swapchainCreateInfo.preTransform = capabilities.currentTransform;
        swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapchainCreateInfo.presentMode = Translate<VkPresentModeKHR>(surface->GetPresentMode());
        swapchainCreateInfo.clipped = VK_TRUE;
        if (m_apiHandle)
            swapchainCreateInfo.oldSwapchain = m_apiHandle;

        // new swapchain with old create info
        VkSwapchainKHR schain;
        PE_CHECK(vkCreateSwapchainKHR(RHII.GetDevice(), &swapchainCreateInfo, nullptr, &schain));

        uint32_t imageCount = 0;
        PE_CHECK(vkGetSwapchainImagesKHR(RHII.GetDevice(), schain, &imageCount, nullptr));
        std::vector<VkImage> imagesVK(imageCount);
        PE_CHECK(vkGetSwapchainImagesKHR(RHII.GetDevice(), schain, &imageCount, imagesVK.data()));

        m_images.resize(SWAPCHAIN_IMAGES);
        for (unsigned i = 0; i < m_images.size(); i++)
        {
            m_images[i] = new Image();
            m_images[i]->ApiHandle() = imagesVK[i];
            m_images[i]->m_createInfo.name = "Swapchain_image_" + std::to_string(i);
            m_images[i]->m_createInfo.format = surface->GetFormat();
            m_images[i]->m_createInfo.width = actualExtent.width;
            m_images[i]->m_createInfo.height = actualExtent.height;
            m_images[i]->m_trackInfos.resize(1);
            m_images[i]->m_trackInfos[0].resize(1, ImageTrackInfo{});
        }

        // create image views for each swapchain image
        for (int i = 0; i < m_images.size(); i++)
        {
            VkImageViewCreateInfo imageViewCreateInfo{};
            imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            imageViewCreateInfo.image = m_images[i]->ApiHandle();
            imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            imageViewCreateInfo.format = Translate<VkFormat>(surface->GetFormat());
            imageViewCreateInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

            VkImageView imageView;
            PE_CHECK(vkCreateImageView(RHII.GetDevice(), &imageViewCreateInfo, nullptr, &imageView));
            m_images[i]->SetRTV(imageView);

            Debug::SetObjectName(m_images[i]->ApiHandle(), "Swapchain_image" + std::to_string(i));
            Debug::SetObjectName(m_images[i]->GetRTV(), "Swapchain_image_view" + std::to_string(i));
        }

        if (m_apiHandle)
        {
            vkDestroySwapchainKHR(RHII.GetDevice(), m_apiHandle, nullptr);
            m_apiHandle = {};
        }

        m_apiHandle = schain;

        Debug::SetObjectName(m_apiHandle, name);
    }

    Swapchain::~Swapchain()
    {
        if (m_apiHandle)
        {
            vkDestroySwapchainKHR(RHII.GetDevice(), m_apiHandle, nullptr);
            m_apiHandle = {};
        }

        for (auto *image : m_images)
        {
            // Invalidate the image handle, swapchain destroys it
            image->ApiHandle() = {};
            delete image;
        }
    }

    uint32_t Swapchain::Aquire(Semaphore *semaphore)
    {
        uint32_t imageIndex = 0;
        PE_CHECK(vkAcquireNextImageKHR(RHII.GetDevice(), m_apiHandle, UINT64_MAX, semaphore->ApiHandle(), nullptr, &imageIndex));

        return imageIndex;
    }
}
#endif
