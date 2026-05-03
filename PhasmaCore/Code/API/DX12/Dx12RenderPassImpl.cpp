#include "API/DX12/Dx12RenderPassImpl.h"

#if defined(PE_WIN32)

namespace pe
{
    Dx12RenderPassImpl::Dx12RenderPassImpl(RenderPass *owner, uint32_t /*count*/, Attachment * /*attachments*/, const std::string & /*name*/)
        : m_owner{owner}
    {
        // No DX12 object backs a render pass in the legacy path.
    }

    Dx12RenderPassImpl::~Dx12RenderPassImpl() = default;
} // namespace pe

#endif // PE_WIN32
