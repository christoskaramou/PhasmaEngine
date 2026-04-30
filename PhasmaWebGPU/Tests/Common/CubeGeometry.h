#pragma once

#include <cstdint>

namespace pwgpu::test::cube
{
    inline constexpr uint32_t kVertexStride = 10 * sizeof(float);
    inline constexpr uint32_t kPositionOffset = 0;
    inline constexpr uint32_t kUvOffset = 8 * sizeof(float);
    inline constexpr uint32_t kVertexCount = 36;

    // clang-format off
    inline constexpr float kVertices[] = {
         1.f, -1.f,  1.f,  1.f,    1.f,  0.f,  1.f,  1.f,   0.f,  1.f,
        -1.f, -1.f,  1.f,  1.f,    0.f,  0.f,  1.f,  1.f,   1.f,  1.f,
        -1.f, -1.f, -1.f,  1.f,    0.f,  0.f,  0.f,  1.f,   1.f,  0.f,
         1.f, -1.f, -1.f,  1.f,    1.f,  0.f,  0.f,  1.f,   0.f,  0.f,
         1.f, -1.f,  1.f,  1.f,    1.f,  0.f,  1.f,  1.f,   0.f,  1.f,
        -1.f, -1.f, -1.f,  1.f,    0.f,  0.f,  0.f,  1.f,   1.f,  0.f,

         1.f,  1.f,  1.f,  1.f,    1.f,  1.f,  1.f,  1.f,   0.f,  1.f,
         1.f, -1.f,  1.f,  1.f,    1.f,  0.f,  1.f,  1.f,   1.f,  1.f,
         1.f, -1.f, -1.f,  1.f,    1.f,  0.f,  0.f,  1.f,   1.f,  0.f,
         1.f,  1.f, -1.f,  1.f,    1.f,  1.f,  0.f,  1.f,   0.f,  0.f,
         1.f,  1.f,  1.f,  1.f,    1.f,  1.f,  1.f,  1.f,   0.f,  1.f,
         1.f, -1.f, -1.f,  1.f,    1.f,  0.f,  0.f,  1.f,   1.f,  0.f,

        -1.f,  1.f,  1.f,  1.f,    0.f,  1.f,  1.f,  1.f,   0.f,  1.f,
         1.f,  1.f,  1.f,  1.f,    1.f,  1.f,  1.f,  1.f,   1.f,  1.f,
         1.f,  1.f, -1.f,  1.f,    1.f,  1.f,  0.f,  1.f,   1.f,  0.f,
        -1.f,  1.f, -1.f,  1.f,    0.f,  1.f,  0.f,  1.f,   0.f,  0.f,
        -1.f,  1.f,  1.f,  1.f,    0.f,  1.f,  1.f,  1.f,   0.f,  1.f,
         1.f,  1.f, -1.f,  1.f,    1.f,  1.f,  0.f,  1.f,   1.f,  0.f,

        -1.f, -1.f,  1.f,  1.f,    0.f,  0.f,  1.f,  1.f,   0.f,  1.f,
        -1.f,  1.f,  1.f,  1.f,    0.f,  1.f,  1.f,  1.f,   1.f,  1.f,
        -1.f,  1.f, -1.f,  1.f,    0.f,  1.f,  0.f,  1.f,   1.f,  0.f,
        -1.f, -1.f, -1.f,  1.f,    0.f,  0.f,  0.f,  1.f,   0.f,  0.f,
        -1.f, -1.f,  1.f,  1.f,    0.f,  0.f,  1.f,  1.f,   0.f,  1.f,
        -1.f,  1.f, -1.f,  1.f,    0.f,  1.f,  0.f,  1.f,   1.f,  0.f,

         1.f,  1.f,  1.f,  1.f,    1.f,  1.f,  1.f,  1.f,   0.f,  1.f,
        -1.f,  1.f,  1.f,  1.f,    0.f,  1.f,  1.f,  1.f,   1.f,  1.f,
        -1.f, -1.f,  1.f,  1.f,    0.f,  0.f,  1.f,  1.f,   1.f,  0.f,
        -1.f, -1.f,  1.f,  1.f,    0.f,  0.f,  1.f,  1.f,   1.f,  0.f,
         1.f, -1.f,  1.f,  1.f,    1.f,  0.f,  1.f,  1.f,   0.f,  0.f,
         1.f,  1.f,  1.f,  1.f,    1.f,  1.f,  1.f,  1.f,   0.f,  1.f,

         1.f, -1.f, -1.f,  1.f,    1.f,  0.f,  0.f,  1.f,   0.f,  1.f,
        -1.f, -1.f, -1.f,  1.f,    0.f,  0.f,  0.f,  1.f,   1.f,  1.f,
        -1.f,  1.f, -1.f,  1.f,    0.f,  1.f,  0.f,  1.f,   1.f,  0.f,
         1.f,  1.f, -1.f,  1.f,    1.f,  1.f,  0.f,  1.f,   0.f,  0.f,
         1.f, -1.f, -1.f,  1.f,    1.f,  0.f,  0.f,  1.f,   0.f,  1.f,
        -1.f,  1.f, -1.f,  1.f,    0.f,  1.f,  0.f,  1.f,   1.f,  0.f,
    };
    // clang-format on
} // namespace pwgpu::test::cube
