#pragma once

#include "API/RHITypes.h"
#include "nlohmann/json.hpp"

namespace pe
{
    class Image;

    namespace imagecapture
    {
        size_t FormatBytesPerPixel(::PeFormat format);
        std::vector<uint8_t> ResizeRGBA(const uint8_t *src, int srcW, int srcH,
                                        int &outW, int &outH, int maxDim = 1024);

        // Main thread only; the caller must wait for renderer work before reading a live image.
        nlohmann::json CaptureGpuImage(Image *image, const nlohmann::json &args);
    } // namespace imagecapture
} // namespace pe
