#pragma once

#include "API/Queue.h"

namespace pe
{
    struct CommandPool::Impl : public NoCopy
    {
        virtual ~Impl() = default;

        virtual void Reset() = 0;
    };

    CommandPool::Impl *CreateCommandPoolImpl(CommandPool *owner, Queue *queue, PeCommandPoolCreateFlags flags, const std::string &name);
} // namespace pe
