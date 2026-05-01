#include "API/Swapchain.h"
#include "API/Image_Internal.h"
#include "API/RHI.h"
#include "API/Semaphore.h"
#include "API/Surface.h"
#include "API/Vulkan/VulkanImageImpl.h"

namespace pe
{
    Swapchain::Swapchain(Surface *surface, const std::string &name)
        : m_images{}
    {
        auto capabilities = RHII.GetGpu().getSurfaceCapabilitiesKHR(surface->ApiHandle());

        // Per Vulkan spec: use currentExtent when it is defined (not UINT32_MAX).
        // Only clamp to [min,max] when the surface lets us choose freely (Wayland etc.).
        vk::Extent2D chosenExtent;
        if (capabilities.currentExtent.width != UINT32_MAX)
        {
            chosenExtent = capabilities.currentExtent;
        }
        else
        {
            const Rect2Du &actualExtent = surface->GetActualExtent();
            chosenExtent.width = std::clamp(actualExtent.width,
                                            capabilities.minImageExtent.width,
                                            capabilities.maxImageExtent.width);
            chosenExtent.height = std::clamp(actualExtent.height,
                                             capabilities.minImageExtent.height,
                                             capabilities.maxImageExtent.height);
        }

        // Keep surface and swapchain extents in sync.
        surface->SetActualExtent({0, 0, chosenExtent.width, chosenExtent.height});
        m_extent.x = 0;
        m_extent.y = 0;
        m_extent.width = chosenExtent.width;
        m_extent.height = chosenExtent.height;

        vk::SwapchainCreateInfoKHR swapchainCreateInfo{};
        swapchainCreateInfo.surface = surface->ApiHandle();
        swapchainCreateInfo.minImageCount = capabilities.minImageCount + 1;
        swapchainCreateInfo.imageFormat = surface->GetFormat();
        swapchainCreateInfo.imageColorSpace = surface->GetColorSpace();
        swapchainCreateInfo.imageExtent = chosenExtent;
        swapchainCreateInfo.imageArrayLayers = 1;
        vk::ImageUsageFlags swapchainUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
        if (capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferSrc)
            swapchainUsage |= vk::ImageUsageFlagBits::eTransferSrc;
        swapchainCreateInfo.imageUsage = swapchainUsage;
        swapchainCreateInfo.preTransform = capabilities.currentTransform;
        swapchainCreateInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
        swapchainCreateInfo.presentMode = surface->GetPresentMode();
        swapchainCreateInfo.clipped = VK_TRUE;
        if (m_apiHandle)
            swapchainCreateInfo.oldSwapchain = m_apiHandle;

        // new swapchain with old create info
        auto swapchain = RHII.GetDevice().createSwapchainKHR(swapchainCreateInfo);
        auto imagesVK = RHII.GetDevice().getSwapchainImagesKHR(swapchain);

        m_images.resize(imagesVK.size());
        for (unsigned i = 0; i < m_images.size(); i++)
        {
            Image *img = new Image();
            img->m_impl = CreateSwapchainImageImpl(img, imagesVK[i]);
            VulkanImageImpl::From(img)->m_vkFormat = surface->GetFormat();
            img->m_width = chosenExtent.width;
            img->m_height = chosenExtent.height;
            img->m_format = pe::FromVkFormat(surface->GetFormat());
            img->m_usage = PE_IMAGE_USAGE_COLOR_ATTACHMENT | PE_IMAGE_USAGE_TRANSFER_DST;
            img->m_name = "Swapchain_image_" + std::to_string(i);
            img->m_trackInfos.resize(1);
            ImageTrackInfo info{};
            info.image = img;
            info.layout = vk::ImageLayout::eUndefined;
            info.stageFlags = vk::PipelineStageFlagBits2::eColorAttachmentOutput; // Acquire semaphore blocks this stage
            info.accessMask = vk::AccessFlagBits2::eNone;
            img->m_trackInfos[0].resize(1, info);
            m_images[i] = img;
        }

        // create image views for each swapchain image
        for (int i = 0; i < m_images.size(); i++)
        {
            vk::ImageViewCreateInfo imageViewCreateInfo{};
            imageViewCreateInfo.image = pe::GetVulkanImage(m_images[i]);
            imageViewCreateInfo.viewType = vk::ImageViewType::e2D;
            imageViewCreateInfo.format = surface->GetFormat();
            imageViewCreateInfo.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

            auto imageView = ImageView::Create(m_images[i], imageViewCreateInfo, "Swapchain_image_view" + std::to_string(i));
            m_images[i]->SetRTV(imageView);

            Debug::SetObjectName(pe::GetVulkanImage(m_images[i]), "Swapchain_image" + std::to_string(i));
            Debug::SetObjectName(m_images[i]->GetRTV()->ApiHandle(), "Swapchain_image_view" + std::to_string(i));
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
        for (auto *image : m_images)
        {
            // Image::Destroy is safe even though the underlying VkImage is
            // owned by the swapchain — VulkanImageImpl marks externally-owned
            // images and skips vmaDestroyImage in its destructor.
            Image::Destroy(image);
        }

        if (m_apiHandle)
            RHII.GetDevice().destroySwapchainKHR(m_apiHandle);
    }

    uint32_t Swapchain::AquireNextImage(Semaphore *semaphore)
    {
        auto result = RHII.GetDevice().acquireNextImageKHR(m_apiHandle, UINT64_MAX, semaphore->ApiHandle(), nullptr);
        PE_ERROR_IF(result.result != vk::Result::eSuccess && result.result != vk::Result::eSuboptimalKHR,
                    "Failed to acquire swapchain image");

        return result.value;
    }
} // namespace pe
