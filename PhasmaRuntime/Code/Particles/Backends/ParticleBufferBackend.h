#pragma once

#include "API/RHITypes.h"

namespace pe
{
    class Buffer;

    namespace ParticleBufferBackend
    {
        PeMemoryUsage ParticleBufferMemoryUsage();
        void ZeroParticleBuffer(Buffer *buffer);
        void ZeroParticleBufferRange(Buffer *buffer, size_t byteOffset, size_t byteSize);
    } // namespace ParticleBufferBackend
} // namespace pe
