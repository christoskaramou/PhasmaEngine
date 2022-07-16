#pragma once

namespace pe
{
    struct GlobalSettings
    {
        inline static bool RightHanded = false;
        inline static bool ReverseZ = true;
    };

    constexpr uint32_t SHADOWMAP_CASCADES = 4;
    constexpr uint32_t SHADOWMAP_SIZE = 2048;
    constexpr uint32_t SWAPCHAIN_IMAGES = 2;
}
