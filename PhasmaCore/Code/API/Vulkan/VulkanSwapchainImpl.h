#pragma once

#include "API/Swapchain_Internal.h"

namespace pe
{
    class Semaphore;

    struct VulkanSwapchainImpl final : public Swapchain::Impl
    {
        VulkanSwapchainImpl(Swapchain *owner, const SwapchainDesc &desc);
        ~VulkanSwapchainImpl() override;

        uint32_t AquireNextImage(Semaphore *semaphore) override;

        static VulkanSwapchainImpl *From(Swapchain *sc) { return static_cast<VulkanSwapchainImpl *>(sc->m_impl); }
        static const VulkanSwapchainImpl *From(const Swapchain *sc) { return static_cast<const VulkanSwapchainImpl *>(sc->m_impl); }
        static VulkanSwapchainImpl *TryFrom(Swapchain *sc) { return sc && sc->m_impl ? From(sc) : nullptr; }
        static const VulkanSwapchainImpl *TryFrom(const Swapchain *sc) { return sc && sc->m_impl ? From(sc) : nullptr; }

        Swapchain *m_owner;
        vk::SwapchainKHR m_swapchain;
        vk::Format m_vkFormat{vk::Format::eUndefined};
    };

    inline vk::SwapchainKHR GetVulkanSwapchain(Swapchain *sc)
    {
        VulkanSwapchainImpl *impl = VulkanSwapchainImpl::TryFrom(sc);
        return impl ? impl->m_swapchain : vk::SwapchainKHR{};
    }

    inline vk::SwapchainKHR GetVulkanSwapchain(const Swapchain *sc)
    {
        const VulkanSwapchainImpl *impl = VulkanSwapchainImpl::TryFrom(sc);
        return impl ? impl->m_swapchain : vk::SwapchainKHR{};
    }
} // namespace pe
