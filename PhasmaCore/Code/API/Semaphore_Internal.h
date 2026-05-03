#pragma once

#include "API/Semaphore.h"

namespace pe
{
    struct Semaphore::Impl : public NoCopy
    {
        virtual ~Impl() = default;

        virtual void Wait(uint64_t value) = 0;
        virtual bool WaitTimeout(uint64_t value, uint64_t timeoutNS) = 0;
        virtual void Signal(uint64_t value) = 0;
        virtual uint64_t GetValue() = 0;
    };

    Semaphore::Impl *CreateSemaphoreImpl(Semaphore *owner, bool timeline, const std::string &name);
} // namespace pe
