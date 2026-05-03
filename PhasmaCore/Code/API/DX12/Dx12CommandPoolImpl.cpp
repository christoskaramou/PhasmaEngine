#include "API/DX12/Dx12CommandPoolImpl.h"

#if defined(PE_WIN32)

#include "API/Queue.h"

namespace pe
{
    Dx12CommandPoolImpl::Dx12CommandPoolImpl(CommandPool *owner, Queue *, vk::CommandPoolCreateFlags, const std::string &)
        : m_owner{owner}
    {
    }

    void Dx12CommandPoolImpl::Reset()
    {
        // Dx12CommandBufferImpl still owns its allocator in this conservative bridge.
        // Pool-owned allocator reuse can replace that once the raw bypass is retired.
    }
} // namespace pe

#endif // PE_WIN32
