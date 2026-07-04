#pragma once
#include "gtc/packing.hpp" // glm::packHalf1x16 / unpackHalf1x16

// Surface-height map storage: a signed IEEE half-float scalar in [-1, 1] where 0 = ground,
// +1 = the world's Height Range max and -1 = its min (see MapGen::MapHeight). Half floats give
// a smooth height scaler at half the size of float32; the in-editor Map Painter is the only
// writer, MapGen and the painter read it back. Legacy 8-bit PNGs are detected at the call site
// (their gray 0..255 is remapped to [-1, 1]).
//
// Layout: magic "PH16" + int32 w + int32 h (little-endian) + w*h half floats.
namespace pe::voxel
{
    inline constexpr char kHeightMapMagic[4] = {'P', 'H', '1', '6'};
    inline constexpr size_t kHeightMapHeaderBytes = 4 + 4 + 4;

    inline bool IsHeightMapF16(const void *data, size_t size)
    {
        return size >= 4 && memcmp(data, kHeightMapMagic, 4) == 0;
    }

    // Decode an in-memory PH16 blob into floats. False on a bad header or a size that can't hold w*h.
    inline bool DecodeHeightMapF16(const uint8_t *data, size_t size, int &w, int &h, std::vector<float> &out)
    {
        if (!IsHeightMapF16(data, size) || size < kHeightMapHeaderBytes)
            return false;
        memcpy(&w, data + 4, 4);
        memcpy(&h, data + 8, 4);
        if (w <= 0 || h <= 0)
            return false;
        const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
        if (size < kHeightMapHeaderBytes + n * 2)
            return false;
        out.resize(n);
        const uint8_t *p = data + kHeightMapHeaderBytes;
        for (size_t i = 0; i < n; ++i)
        {
            uint16_t half;
            memcpy(&half, p + i * 2, 2);
            out[i] = glm::unpackHalf1x16(half);
        }
        return true;
    }

    inline std::vector<uint8_t> EncodeHeightMapF16(const float *px, int w, int h)
    {
        const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
        std::vector<uint8_t> out(kHeightMapHeaderBytes + n * 2);
        memcpy(out.data(), kHeightMapMagic, 4);
        memcpy(out.data() + 4, &w, 4);
        memcpy(out.data() + 8, &h, 4);
        uint8_t *p = out.data() + kHeightMapHeaderBytes;
        for (size_t i = 0; i < n; ++i)
        {
            const uint16_t half = glm::packHalf1x16(px[i]);
            memcpy(p + i * 2, &half, 2);
        }
        return out;
    }
} // namespace pe::voxel
