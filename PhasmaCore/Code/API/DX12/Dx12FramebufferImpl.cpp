#include "API/DX12/Dx12FramebufferImpl.h"

#if defined(PE_WIN32)

namespace pe
{
    Dx12FramebufferImpl::Dx12FramebufferImpl(Framebuffer *owner,
                                             uint32_t /*width*/,
                                             uint32_t /*height*/,
                                             const std::vector<ImageView *> & /*views*/,
                                             RenderPass * /*renderPass*/,
                                             const std::string & /*name*/)
        : m_owner{owner}
    {
        // No D3D12 equivalent of vk::Framebuffer to create here. Render-target
        // descriptors are bound per-record inside Dx12CommandBufferImpl from the
        // Attachment array directly.
    }
} // namespace pe

#endif // PE_WIN32
