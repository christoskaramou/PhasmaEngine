#pragma once

#include "API/RenderPass_Internal.h"

namespace pe
{
    struct Attachment;

    // DX12 has no native render-pass object in the legacy command-list path.
    // BeginPass / EndPass on the DX12 command buffer record OMSetRenderTargets
    // and the load/store clears directly off the Attachment array, so this impl
    // exists only to satisfy the pImpl factory dispatch.
    struct Dx12RenderPassImpl final : public RenderPass::Impl
    {
        Dx12RenderPassImpl(RenderPass *owner, uint32_t count, Attachment *attachments, const std::string &name);
        ~Dx12RenderPassImpl() override;

        static Dx12RenderPassImpl *From(RenderPass *rp) { return static_cast<Dx12RenderPassImpl *>(rp->m_impl); }
        static const Dx12RenderPassImpl *From(const RenderPass *rp) { return static_cast<const Dx12RenderPassImpl *>(rp->m_impl); }
        static Dx12RenderPassImpl *TryFrom(RenderPass *rp) { return rp && rp->m_impl ? From(rp) : nullptr; }
        static const Dx12RenderPassImpl *TryFrom(const RenderPass *rp) { return rp && rp->m_impl ? From(rp) : nullptr; }

        RenderPass *m_owner{};
    };
} // namespace pe
