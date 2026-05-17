#pragma once

#include "API/RHITypes.h"

#include <cstdint>
#include <string>

namespace pe
{
    struct ScreenshotWriteDesc
    {
        std::string path;
        const uint8_t *pixels = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        size_t rowPitch = 0;
        ::PeFormat format = PE_FORMAT_UNDEFINED;
    };

    [[nodiscard]] bool WriteScreenshotPng(const ScreenshotWriteDesc &desc, std::string *resolvedPath = nullptr);
} // namespace pe
