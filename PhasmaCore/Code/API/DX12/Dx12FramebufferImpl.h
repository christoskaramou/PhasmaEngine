#pragma once

#include "API/Framebuffer_Internal.h"

#if defined(PE_WIN32)

namespace pe
{
    // DX12 has no first-class VkFramebuffer equivalent; render targets are bound
    // per-record via OMSetRenderTargets with raw RTV/DSV handles. This impl is a
    // conservative no-op holder so the neutral Framebuffer owner can be
    // constructed on the DX12 path without leaking Vulkan-only resource creation.
    struct Dx12FramebufferImpl final : public Framebuffer::Impl
    {
        Dx12FramebufferImpl(Framebuffer *owner,
                            uint32_t width,
                            uint32_t height,
                            const std::vector<ImageView *> &views,
                            RenderPass *renderPass,
                            const std::string &name);
        ~Dx12FramebufferImpl() override = default;

        static Dx12FramebufferImpl *From(Framebuffer *fb) { return static_cast<Dx12FramebufferImpl *>(fb->m_impl); }
        static const Dx12FramebufferImpl *From(const Framebuffer *fb) { return static_cast<const Dx12FramebufferImpl *>(fb->m_impl); }
        static Dx12FramebufferImpl *TryFrom(Framebuffer *fb) { return fb && fb->m_impl ? From(fb) : nullptr; }
        static const Dx12FramebufferImpl *TryFrom(const Framebuffer *fb) { return fb && fb->m_impl ? From(fb) : nullptr; }

        Framebuffer *m_owner{};
    };
} // namespace pe

#endif // PE_WIN32
