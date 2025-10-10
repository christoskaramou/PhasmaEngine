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
        auto capabilities = RHII.GetGpu().getSurfaceCapabilitiesKHR(surface->ApiHandle());

        PE_ERROR_IF(SWAPCHAIN_IMAGES < capabilities.minImageCount, "Swapchain image count error");
        if (capabilities.maxImageCount > 0)
            PE_ERROR_IF(SWAPCHAIN_IMAGES > capabilities.maxImageCount, "Swapchain image count error");

        const Rect2Du &actualExtent = surface->GetActualExtent();
        m_extent.x = actualExtent.x;
        m_extent.y = actualExtent.y;
        m_extent.width = actualExtent.width;
        m_extent.height = actualExtent.height;

        vk::SwapchainCreateInfoKHR swapchainCreateInfo{};
        swapchainCreateInfo.surface = surface->ApiHandle();
        swapchainCreateInfo.minImageCount = SWAPCHAIN_IMAGES;
        swapchainCreateInfo.imageFormat = surface->GetFormat();
        swapchainCreateInfo.imageColorSpace = surface->GetColorSpace();
        swapchainCreateInfo.imageExtent = vk::Extent2D{m_extent.width, m_extent.height};
        swapchainCreateInfo.imageArrayLayers = 1;
        swapchainCreateInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
        swapchainCreateInfo.preTransform = capabilities.currentTransform;
        swapchainCreateInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
        swapchainCreateInfo.presentMode = surface->GetPresentMode();
        swapchainCreateInfo.clipped = VK_TRUE;
        if (m_apiHandle)
            swapchainCreateInfo.oldSwapchain = m_apiHandle;

        // new swapchain with old create info
        auto swapchain = RHII.GetDevice().createSwapchainKHR(swapchainCreateInfo);
        auto imagesVK = RHII.GetDevice().getSwapchainImagesKHR(swapchain);

        m_images.resize(SWAPCHAIN_IMAGES);
        for (unsigned i = 0; i < m_images.size(); i++)
        {
            m_images[i] = new Image();
            m_images[i]->ApiHandle() = imagesVK[i];
            m_images[i]->m_name = "Swapchain_image_" + std::to_string(i);
            m_images[i]->m_createInfo = Image::CreateInfoInit();
            m_images[i]->m_createInfo.format = surface->GetFormat();
            m_images[i]->m_createInfo.extent.width = actualExtent.width;
            m_images[i]->m_createInfo.extent.height = actualExtent.height;
            m_images[i]->m_trackInfos.resize(1);
            m_images[i]->m_trackInfos[0].resize(1, ImageTrackInfo{});
        }

        // create image views for each swapchain image
        for (int i = 0; i < m_images.size(); i++)
        {
            vk::ImageViewCreateInfo imageViewCreateInfo{};
            imageViewCreateInfo.image = m_images[i]->ApiHandle();
            imageViewCreateInfo.viewType = vk::ImageViewType::e2D;
            imageViewCreateInfo.format = surface->GetFormat();
            imageViewCreateInfo.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

            auto imageView = RHII.GetDevice().createImageView(imageViewCreateInfo);
            m_images[i]->SetRTV(imageView);

            Debug::SetObjectName(m_images[i]->ApiHandle(), "Swapchain_image" + std::to_string(i));
            Debug::SetObjectName(m_images[i]->GetRTV(), "Swapchain_image_view" + std::to_string(i));
        }

        if (m_apiHandle)
        {
            RHII.GetDevice().destroySwapchainKHR(m_apiHandle);
        }

        m_apiHandle = swapchain;

        Debug::SetObjectName(m_apiHandle, name);
    }

    Swapchain::~Swapchain()
    {
        if (m_apiHandle)
        {
            RHII.GetDevice().destroySwapchainKHR(m_apiHandle);
            m_apiHandle = vk::SwapchainKHR{};

        }

        for (auto *image : m_images)
        {
            // Invalidate the image handle, swapchain destroys it
            image->ApiHandle() = vk::Image{};
            delete image;
        }
    }

    uint32_t Swapchain::Aquire(Semaphore *semaphore)
    {
        auto result = RHII.GetDevice().acquireNextImageKHR(m_apiHandle, UINT64_MAX, semaphore->ApiHandle(), nullptr);
        PE_ERROR_IF(result.result != vk::Result::eSuccess && result.result != vk::Result::eSuboptimalKHR,
                    "Failed to acquire swapchain image");

        return result.value;
    }
}
