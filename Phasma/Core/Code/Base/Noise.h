#pragma once
#include "gtc/noise.hpp" // glm::perlin

// General-purpose procedural noise helpers — engine-wide, not tied to any subsystem.
// Voxel terrain, mesh heightfields, procedural textures, scattering, clouds, etc. all share these.
namespace pe
{
    // Fractal Brownian motion over Perlin noise. Sums `octaves` layers, each at `lacunarity`x the
    // frequency and `gain`x the amplitude of the previous one, then normalizes by the amplitude sum
    // so the output stays in ~[-1, 1] regardless of octave count. glm::perlin is seamless, so
    // adjacent samples are continuous (tiled terrain / textures line up with no extra bookkeeping).
    inline float Fbm2D(const vec2 &p, int octaves = 4, float baseFreq = 1.0f, float lacunarity = 2.0f,
                       float gain = 0.5f)
    {
        float sum = 0.0f, amp = 1.0f, freq = baseFreq, norm = 0.0f;
        for (int o = 0; o < octaves; ++o)
        {
            sum += amp * glm::perlin(p * freq);
            norm += amp;
            amp *= gain;
            freq *= lacunarity;
        }
        return norm > 0.0f ? sum / norm : 0.0f; // ~[-1, 1]
    }

    // 3D fractal Brownian motion (volumetric noise) — caves, ore pockets, 3D density fields,
    // clouds. Same octave/normalization scheme as Fbm2D.
    inline float Fbm3D(const vec3 &p, int octaves = 4, float baseFreq = 1.0f, float lacunarity = 2.0f,
                       float gain = 0.5f)
    {
        float sum = 0.0f, amp = 1.0f, freq = baseFreq, norm = 0.0f;
        for (int o = 0; o < octaves; ++o)
        {
            sum += amp * glm::perlin(p * freq);
            norm += amp;
            amp *= gain;
            freq *= lacunarity;
        }
        return norm > 0.0f ? sum / norm : 0.0f; // ~[-1, 1]
    }
} // namespace pe
