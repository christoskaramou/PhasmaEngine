#pragma once

#include "API/RHITypes.h"

namespace pe
{
    class Buffer;

    namespace ParticleBufferBackend
    {
        PeMemoryUsage ParticleBufferMemoryUsage();
        void ZeroParticleBuffer(Buffer *buffer);
    } // namespace ParticleBufferBackend
} // namespace pe
