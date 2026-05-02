#include "API/Vulkan/VulkanSwapchainImpl.h"
#include "API/Image_Internal.h"
#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Semaphore.h"
#include "API/Surface.h"
#include "API/Vulkan/VulkanImageImpl.h"
#include "API/Vulkan/VulkanImageViewImpl.h"
#include "API/Vulkan/VulkanRHITypeUtils.h"

namespace pe
{
    VulkanSwapchainImpl::VulkanSwapchainImpl(Swapchain *owner, const SwapchainDesc &desc)
        : m_owner{owner}
    {
        Surface *surface = desc.surface;
        PE_ERROR_IF(!surface, "VulkanSwapchainImpl requires a non-null Surface");

        auto capabilities = VulkanRhi::Gpu().getSurfaceCapabilitiesKHR(surface->ApiHandle());

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

        surface->SetActualExtent({0, 0, chosenExtent.width, chosenExtent.height});
        m_owner->m_width = chosenExtent.width;
        m_owner->m_height = chosenExtent.height;

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
        swapchainCreateInfo.presentMode = ToVkPresentMode(surface->GetPresentMode());
        swapchainCreateInfo.clipped = VK_TRUE;

        m_swapchain = VulkanRhi::Device().createSwapchainKHR(swapchainCreateInfo);
        m_vkFormat = surface->GetFormat();

        auto imagesVK = VulkanRhi::Device().getSwapchainImagesKHR(m_swapchain);

        m_owner->m_images.resize(imagesVK.size());
        for (unsigned i = 0; i < m_owner->m_images.size(); i++)
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
            info.layout = PE_IMAGE_LAYOUT_UNDEFINED;
            info.stageFlags = PE_STAGE_COLOR_ATTACHMENT_OUTPUT;
            info.accessMask = PE_ACCESS_NONE;
            img->m_trackInfos[0].resize(1, info);
            m_owner->m_images[i] = img;
        }

        for (size_t i = 0; i < m_owner->m_images.size(); i++)
        {
            ImageViewDesc imageViewCreateInfo{};
            imageViewCreateInfo.viewType = PE_IMAGE_VIEW_TYPE_2D;
            imageViewCreateInfo.format = pe::FromVkFormat(surface->GetFormat());
            imageViewCreateInfo.aspectMask = PE_IMAGE_ASPECT_COLOR;
            imageViewCreateInfo.baseMipLevel = 0;
            imageViewCreateInfo.levelCount = 1;
            imageViewCreateInfo.baseArrayLayer = 0;
            imageViewCreateInfo.layerCount = 1;

            auto imageView = ImageView::Create(m_owner->m_images[i], imageViewCreateInfo, "Swapchain_image_view" + std::to_string(i));
            m_owner->m_images[i]->SetRTV(imageView);

            Debug::SetObjectName(pe::GetVulkanImage(m_owner->m_images[i]), "Swapchain_image" + std::to_string(i));
            Debug::SetObjectName(pe::GetVulkanImageView(m_owner->m_images[i]->GetRTV()), "Swapchain_image_view" + std::to_string(i));
        }

        Debug::SetObjectName(m_swapchain, m_owner->GetName());
    }

    VulkanSwapchainImpl::~VulkanSwapchainImpl()
    {
        if (m_swapchain)
            VulkanRhi::Device().destroySwapchainKHR(m_swapchain);
    }

    uint32_t VulkanSwapchainImpl::AquireNextImage(Semaphore *semaphore)
    {
        auto result = VulkanRhi::Device().acquireNextImageKHR(m_swapchain, UINT64_MAX, semaphore->ApiHandle(), nullptr);
        PE_ERROR_IF(result.result != vk::Result::eSuccess && result.result != vk::Result::eSuboptimalKHR,
                    "Failed to acquire swapchain image");
        return result.value;
    }
} // namespace pe
