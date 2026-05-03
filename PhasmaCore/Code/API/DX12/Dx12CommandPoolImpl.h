#pragma once

#include "API/CommandPool_Internal.h"

#if defined(PE_WIN32)

namespace pe
{
    struct Dx12CommandPoolImpl final : public CommandPool::Impl
    {
        Dx12CommandPoolImpl(CommandPool *owner, Queue *queue, vk::CommandPoolCreateFlags flags, const std::string &name);
        ~Dx12CommandPoolImpl() override = default;

        static Dx12CommandPoolImpl *From(CommandPool *cp) { return static_cast<Dx12CommandPoolImpl *>(cp->m_impl); }
        static const Dx12CommandPoolImpl *From(const CommandPool *cp) { return static_cast<const Dx12CommandPoolImpl *>(cp->m_impl); }

        void Reset() override;

        CommandPool *m_owner{};
    };
} // namespace pe

#endif // PE_WIN32
