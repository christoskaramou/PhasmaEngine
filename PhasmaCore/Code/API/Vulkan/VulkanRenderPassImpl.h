#pragma once

#include "API/RenderPass_Internal.h"

namespace pe
{
    struct VulkanRenderPassImpl final : public RenderPass::Impl
    {
        VulkanRenderPassImpl(RenderPass *owner, uint32_t count, Attachment *attachments, const std::string &name);
        ~VulkanRenderPassImpl() override;

        static VulkanRenderPassImpl *From(RenderPass *rp) { return static_cast<VulkanRenderPassImpl *>(rp->m_impl); }
        static const VulkanRenderPassImpl *From(const RenderPass *rp) { return static_cast<const VulkanRenderPassImpl *>(rp->m_impl); }
        static VulkanRenderPassImpl *TryFrom(RenderPass *rp) { return rp && rp->m_impl ? From(rp) : nullptr; }
        static const VulkanRenderPassImpl *TryFrom(const RenderPass *rp) { return rp && rp->m_impl ? From(rp) : nullptr; }

        RenderPass *m_owner{};
        vk::RenderPass m_apiHandle{};
    };

    inline vk::RenderPass GetVulkanRenderPass(RenderPass *rp)
    {
        VulkanRenderPassImpl *impl = VulkanRenderPassImpl::TryFrom(rp);
        return impl ? impl->m_apiHandle : vk::RenderPass{};
    }

    inline vk::RenderPass GetVulkanRenderPass(const RenderPass *rp)
    {
        const VulkanRenderPassImpl *impl = VulkanRenderPassImpl::TryFrom(rp);
        return impl ? impl->m_apiHandle : vk::RenderPass{};
    }
} // namespace pe
