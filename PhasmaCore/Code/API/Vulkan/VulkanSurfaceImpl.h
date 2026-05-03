#pragma once

#include "API/Surface_Internal.h"

namespace pe
{
    struct VulkanSurfaceImpl final : public Surface::Impl
    {
        VulkanSurfaceImpl(Surface *owner, SDL_Window *window);
        ~VulkanSurfaceImpl() override;

        static VulkanSurfaceImpl *From(Surface *s) { return static_cast<VulkanSurfaceImpl *>(s->m_impl); }
        static const VulkanSurfaceImpl *From(const Surface *s) { return static_cast<const VulkanSurfaceImpl *>(s->m_impl); }
        static VulkanSurfaceImpl *TryFrom(Surface *s) { return s && s->m_impl ? From(s) : nullptr; }
        static const VulkanSurfaceImpl *TryFrom(const Surface *s) { return s && s->m_impl ? From(s) : nullptr; }

        std::vector<PePresentMode> GetSupportedPresentModes() const override;

        Surface *m_owner{};
        vk::SurfaceKHR m_apiHandle{};
    };

    inline vk::SurfaceKHR GetVulkanSurface(Surface *s)
    {
        VulkanSurfaceImpl *impl = VulkanSurfaceImpl::TryFrom(s);
        return impl ? impl->m_apiHandle : vk::SurfaceKHR{};
    }

    inline vk::SurfaceKHR GetVulkanSurface(const Surface *s)
    {
        const VulkanSurfaceImpl *impl = VulkanSurfaceImpl::TryFrom(s);
        return impl ? impl->m_apiHandle : vk::SurfaceKHR{};
    }
} // namespace pe
