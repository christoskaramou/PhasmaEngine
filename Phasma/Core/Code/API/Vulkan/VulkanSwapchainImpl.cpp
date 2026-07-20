#include "API/Vulkan/VulkanSwapchainImpl.h"
#include "API/Image_Internal.h"
#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Semaphore.h"
#include "API/Surface.h"
#include "API/Vulkan/VulkanImageImpl.h"
#include "API/Vulkan/VulkanImageViewImpl.h"
#include "API/Vulkan/VulkanRHITypeUtils.h"
#include "API/Vulkan/VulkanSemaphoreImpl.h"
#include "API/Vulkan/VulkanSurfaceImpl.h"

namespace pe
{
    VulkanSwapchainImpl::VulkanSwapchainImpl(Swapchain *owner, const SwapchainDesc &desc)
        : m_owner{owner}
    {
        Surface *surface = desc.surface;
        PE_ERROR_IF(!surface, "VulkanSwapchainImpl requires a non-null Surface");

        auto capabilities = VulkanRhi::Gpu().getSurfaceCapabilitiesKHR(pe::GetVulkanSurface(surface));

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

        // Surface transform selection matters on Android. A device whose panel is mounted in a
        // different orientation than the app reports a rotated currentTransform (e.g. ROTATE_90 for a
        // landscape-locked app on a portrait panel). The engine renders un-rotated content into an
        // offscreen displayRT and blits it 1:1 to the swapchain image, so if we request
        // preTransform=currentTransform the presentation engine expects us to pre-rotate that image. We
        // do not do that yet, so prefer IDENTITY and let the compositor present our normal frames. Keep
        // imageExtent at currentExtent: on Android minImageExtent/maxImageExtent can still allow a range,
        // but the swapchain image size must match the surface size SDL reports.
        vk::SurfaceTransformFlagBitsKHR chosenTransform = capabilities.currentTransform;
        if (capabilities.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity)
            chosenTransform = vk::SurfaceTransformFlagBitsKHR::eIdentity;

        // One-time-per-(re)create diagnostics: the correct pre-rotation handling is device-specific and
        // depends on exactly what the driver reports, so log it. Read on device via
        // `adb shell run-as dev.phasma.player cat files/PhasmaEngine.log`.
        PE_INFO("[Swapchain] currentTransform=0x%x chosen=0x%x supported=0x%x currentExtent=%ux%u "
                "final=%ux%u min=%ux%u max=%ux%u",
                static_cast<uint32_t>(static_cast<VkSurfaceTransformFlagBitsKHR>(capabilities.currentTransform)),
                static_cast<uint32_t>(static_cast<VkSurfaceTransformFlagBitsKHR>(chosenTransform)),
                static_cast<uint32_t>(static_cast<VkSurfaceTransformFlagsKHR>(capabilities.supportedTransforms)),
                capabilities.currentExtent.width, capabilities.currentExtent.height,
                chosenExtent.width, chosenExtent.height,
                capabilities.minImageExtent.width, capabilities.minImageExtent.height,
                capabilities.maxImageExtent.width, capabilities.maxImageExtent.height);

        surface->SetActualExtent({0, 0, chosenExtent.width, chosenExtent.height});
        m_owner->m_width = chosenExtent.width;
        m_owner->m_height = chosenExtent.height;

        vk::SwapchainCreateInfoKHR swapchainCreateInfo{};
        swapchainCreateInfo.surface = pe::GetVulkanSurface(surface);
        // bbCount=2 matches DX12 FLIP_DISCARD and avoids NVIDIA IMMEDIATE
        // burst-throttle stalls in vkAcquireNextImageKHR. Mesa Dozen (Vulkan
        // over D3D12 on WSLg) has been fragile with the two-image path, so keep
        // one image of slack above the driver minimum there. Android FIFO pacing
        // benefits from triple-buffering so acquire is less likely to stall when
        // a frame overshoots vsync.
#if defined(PE_ANDROID)
        uint32_t desiredImageCount = 3u;
#else
        uint32_t desiredImageCount = RHII.UsesDozenVulkan() ? capabilities.minImageCount + 1u : 2u;
#endif
        if (desiredImageCount < capabilities.minImageCount)
            desiredImageCount = capabilities.minImageCount;
        if (capabilities.maxImageCount > 0 && desiredImageCount > capabilities.maxImageCount)
            desiredImageCount = capabilities.maxImageCount;
        swapchainCreateInfo.minImageCount = desiredImageCount;
        const vk::Format surfaceFormat = ToVkFormat(surface->GetFormat());
        const vk::ColorSpaceKHR surfaceColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
        swapchainCreateInfo.imageFormat = surfaceFormat;
        swapchainCreateInfo.imageColorSpace = surfaceColorSpace;
        swapchainCreateInfo.imageExtent = chosenExtent;
        swapchainCreateInfo.imageArrayLayers = 1;
        vk::ImageUsageFlags swapchainUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
        if (capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferSrc)
            swapchainUsage |= vk::ImageUsageFlagBits::eTransferSrc;
        swapchainCreateInfo.imageUsage = swapchainUsage;
        swapchainCreateInfo.preTransform = chosenTransform;
        swapchainCreateInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
        swapchainCreateInfo.presentMode = ToVkPresentMode(surface->GetPresentMode());
        swapchainCreateInfo.clipped = VK_TRUE;

        m_swapchain = VulkanRhi::Device().createSwapchainKHR(swapchainCreateInfo);
        m_vkFormat = surfaceFormat;

        auto imagesVK = VulkanRhi::Device().getSwapchainImagesKHR(m_swapchain);

        m_owner->m_images.resize(imagesVK.size());
        for (unsigned i = 0; i < m_owner->m_images.size(); i++)
        {
            Image *img = new Image();
            img->m_impl = CreateSwapchainImageImpl(img, imagesVK[i]);
            VulkanImageImpl::From(img)->m_vkFormat = surfaceFormat;
            img->m_width = chosenExtent.width;
            img->m_height = chosenExtent.height;
            img->m_format = surface->GetFormat();
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
            imageViewCreateInfo.format = surface->GetFormat();
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
        try
        {
            auto result = VulkanRhi::Device().acquireNextImageKHR(m_swapchain, UINT64_MAX, pe::GetVulkanSemaphore(semaphore), nullptr);
            PE_ERROR_IF(result.result != vk::Result::eSuccess && result.result != vk::Result::eSuboptimalKHR,
                        "Failed to acquire swapchain image");
            return result.value;
        }
        catch (const vk::OutOfDateKHRError &)
        {
            throw SwapchainOutOfDateError{};
        }
    }
} // namespace pe
