#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <cstdint>
#include <vector>

namespace pmcp
{
    namespace
    {
        void StbWriteToVector(void *context, void *data, int size)
        {
            auto *out = static_cast<std::vector<uint8_t> *>(context);
            const auto *bytes = static_cast<const uint8_t *>(data);
            out->insert(out->end(), bytes, bytes + size);
        }
    } // namespace

    std::vector<uint8_t> EncodeRGBA_PNG(const uint8_t *rgba, int w, int h)
    {
        std::vector<uint8_t> png;
        if (!rgba || w <= 0 || h <= 0)
            return png;
        stbi_write_png_to_func(StbWriteToVector, &png, w, h, 4, rgba, w * 4);
        return png;
    }
} // namespace pmcp
