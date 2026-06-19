#pragma once

#include "API/Vulkan/VulkanHeaders.h"

#include "API/Framebuffer_Internal.h"

namespace pe
{
    struct VulkanFramebufferImpl final : public Framebuffer::Impl
    {
        VulkanFramebufferImpl(Framebuffer *owner,
                              uint32_t width,
                              uint32_t height,
                              const std::vector<ImageView *> &views,
                              RenderPass *renderPass,
                              const std::string &name);
        ~VulkanFramebufferImpl() override;

        static VulkanFramebufferImpl *From(Framebuffer *fb) { return static_cast<VulkanFramebufferImpl *>(fb->m_impl); }
        static const VulkanFramebufferImpl *From(const Framebuffer *fb) { return static_cast<const VulkanFramebufferImpl *>(fb->m_impl); }
        static VulkanFramebufferImpl *TryFrom(Framebuffer *fb) { return fb && fb->m_impl ? From(fb) : nullptr; }
        static const VulkanFramebufferImpl *TryFrom(const Framebuffer *fb) { return fb && fb->m_impl ? From(fb) : nullptr; }

        Framebuffer *m_owner{};
        vk::Framebuffer m_apiHandle{};
    };

    inline vk::Framebuffer GetVulkanFramebuffer(Framebuffer *fb)
    {
        VulkanFramebufferImpl *impl = VulkanFramebufferImpl::TryFrom(fb);
        return impl ? impl->m_apiHandle : vk::Framebuffer{};
    }

    inline vk::Framebuffer GetVulkanFramebuffer(const Framebuffer *fb)
    {
        const VulkanFramebufferImpl *impl = VulkanFramebufferImpl::TryFrom(fb);
        return impl ? impl->m_apiHandle : vk::Framebuffer{};
    }
} // namespace pe
