#pragma once

#include "API/RHITypes.h"

#if defined(PE_WIN32)

namespace pe
{
    class CommandBuffer;
    class Image;

    void Dx12BlitImage(CommandBuffer *cmd, Image *src, Image *dst, const ImageBlit &region, PeFilter filter);
} // namespace pe

#endif // PE_WIN32
