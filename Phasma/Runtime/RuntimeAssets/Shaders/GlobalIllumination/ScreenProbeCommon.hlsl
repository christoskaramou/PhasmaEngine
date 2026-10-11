#ifndef SCREEN_PROBE_COMMON_H
#define SCREEN_PROBE_COMMON_H
// Screen probes (ScreenProbePass): a probe per 16x16 pixel tile, 8x8 rays each, plus adaptive probes on the 8x8
// sub-tile grid where those cannot interpolate a pixel (Lumen's adaptive placement). Probe images are tilesX x
// (tilesY + adaptiveRows): uniform probes first, then adaptive slot k at (k % tilesX, tilesY + k / tilesX). Ray images
// are 8x that, SH images tilesX*9 wide (one texel per L2 coefficient, rgb).
#include "ProbeVolume.hlsl"

#ifdef SCREEN_PROBES
[[vk::binding(5, 2)]] Texture2D<float4> sp_worldRadiance; // GI_RadianceHistory: 16x16 octahedral radiance per world probe
[[vk::binding(6, 2)]] Texture2D<float4> sp_probePositions; // xyz world position, w view distance (0 = no probe)
[[vk::binding(7, 2)]] Texture2D<float4> sp_probeNormals;
#else
// Not binding 0: DX12 maps it to b0, the push-constant register. No comment after the declaration: the DX12
// register rewrite appends to the line.
[[vk::binding(14, 0)]] cbuffer ScreenProbeUBO
{
    float4x4 sp_invViewProj;
    float4x4 sp_prevViewProj; // last frame's, without TAA jitter
    float4x4 sp_viewProj;     // this frame's, without TAA jitter
    float4 sp_camPos;
    float4 sp_prevCamPos;
    uint sp_tilesX;
    uint sp_tilesY;
    uint sp_width;
    uint sp_height;
    uint sp_frame;
    uint sp_reset;
    uint sp_hasVelocity; // the velocity target is bound (else camera motion only)
    uint sp_adaptiveRows; // probe rows after the tilesY uniform rows that hold adaptive probes
};
#endif

// Second-order (L2) spherical harmonics, 9 coefficients, as Lumen keeps them (unverified). L1 weighs light arriving
// along the surface at a quarter of its value where the cosine gives none: measured 2026-10-11 against a per-pixel
// traced reference, the Cornell floor read 13.5% dark and the ceiling around the lamp up to 40% bright.
#define SCREEN_PROBE_SH_TEXELS 9

struct ScreenProbeSH
{
    float3 c[SCREEN_PROBE_SH_TEXELS];
};

void ScreenProbeSHBasis(float3 d, out float b[SCREEN_PROBE_SH_TEXELS])
{
    b[0] = 0.282095;
    b[1] = 0.488603 * d.y;
    b[2] = 0.488603 * d.z;
    b[3] = 0.488603 * d.x;
    b[4] = 1.092548 * d.x * d.y;
    b[5] = 1.092548 * d.y * d.z;
    b[6] = 0.315392 * (3.0 * d.z * d.z - 1.0);
    b[7] = 1.092548 * d.x * d.z;
    b[8] = 0.546274 * (d.x * d.x - d.y * d.y);
}

// The SH along `direction`, band 1 scaled by `band1` and band 2 by `band2`.
float3 ScreenProbeSHEvaluate(ScreenProbeSH sh, float3 direction, float band1, float band2)
{
    float b[SCREEN_PROBE_SH_TEXELS];
    ScreenProbeSHBasis(direction, b);
    float3 value = sh.c[0] * b[0];
    [unroll] for (uint i = 1; i < 4; ++i)
        value += sh.c[i] * (b[i] * band1);
    [unroll] for (uint j = 4; j < SCREEN_PROBE_SH_TEXELS; ++j)
        value += sh.c[j] * (b[j] * band2);
    return max(value, 0.0);
}

// Irradiance for a normal: the cosine lobe convolves band 0 by pi, band 1 by 2 pi / 3, band 2 by pi / 4.
float3 ScreenProbeSHIrradiance(ScreenProbeSH sh, float3 normal)
{
    return PI * ScreenProbeSHEvaluate(sh, normal, 2.0 / 3.0, 0.25);
}

#ifndef SCREEN_PROBES
// Where a surface point sat last frame, in this frame's pixel coordinates (integers are pixel centres): this pixel
// moved by its velocity (G-buffer, previous minus current NDC without TAA jitter, as Unreal's), so moving objects
// keep their history; without the velocity target, by the camera motion alone. Both are unjittered, so a still
// camera reads its own history pixel exactly; jittered matrices would resample it between pixels every frame and blur
// it. objectMotion: how far the velocity moves the pixel beyond the camera motion, in pixels.
float2 ScreenProbePreviousCoord(float3 position, uint2 pixel, float2 velocity, out bool valid, out float objectMotion)
{
    const float4 previous = mul(float4(position, 1.0), sp_prevViewProj);
    const float4 current = mul(float4(position, 1.0), sp_viewProj);
    valid = previous.w > 0.0 && current.w > 0.0;
    const float2 size = float2(sp_width, sp_height);
    const float2 camera = (previous.xy / previous.w - current.xy / current.w) * 0.5 * size;
    const float2 motion = sp_hasVelocity != 0 ? velocity * 0.5 * size : camera;
    objectMotion = length(motion - camera);
    return float2(pixel) + motion;
}
#endif

// Radiance along `direction` prefiltered by a lobe of mean cosine `band1` (1: the SH's own radiance). Band 2 follows
// the same von Mises-Fisher fit (about band1^3), capped at the cosine lobe's 1/4.
float3 ScreenProbeSHRadiance(ScreenProbeSH sh, float3 direction, float band1)
{
    return ScreenProbeSHEvaluate(sh, direction, band1, min(band1 * band1 * band1, 0.25));
}

// How well a probe stands for a pixel: how close it lies to the pixel's plane (falling off over 1% of the view
// distance) x how far its normal agrees.
float ScreenProbeWeight(float4 probe, float3 probeNormal, float3 position, float3 normal, float viewDistance)
{
    if (probe.w <= 0.0)
        return 0.0;
    const float facing = saturate(dot(normal, probeNormal));
    const float plane = abs(dot(normal, probe.xyz - position)) / (0.01 * viewDistance);
    return exp(-plane) * facing * facing;
}

// Adds one probe's SH to a pixel's sums, weighted by `weight` x ScreenProbeWeight.
void ScreenProbeAccumulate(Texture2D<float4> positions, Texture2D<float4> normals, Texture2D<float4> sh, int2 tile, float weight,
                           float3 position, float3 normal, float viewDistance,
                           inout ScreenProbeSH sum, inout float total)
{
    weight *= ScreenProbeWeight(positions.Load(int3(tile, 0)), normals.Load(int3(tile, 0)).xyz, position, normal, viewDistance);
    if (weight <= 0.0)
        return;
    [unroll] for (uint i = 0; i < SCREEN_PROBE_SH_TEXELS; ++i)
        sum.c[i] += sh.Load(int3(tile.x * SCREEN_PROBE_SH_TEXELS + i, tile.y, 0)).rgb * weight;
    total += weight;
}

// The probe an 8x8 sub-tile holds (adaptiveMap: slot + 1, 0 = none) as a tile coordinate of the probe images.
bool ScreenProbeAdaptiveTile(Texture2D<uint> adaptiveMap, int2 subTile, uint2 tiles, out int2 tile)
{
    const uint slot = adaptiveMap.Load(int3(subTile, 0));
    tile = int2((slot - 1) % tiles.x, tiles.y + (slot - 1) / tiles.x);
    return slot > 0;
}

// The SH around a pixel, blended as Lumen upsamples screen probes: the four nearest uniform probes (bilinear on the
// 16 px grid) and the adaptive probes of the four nearest 8x8 sub-tiles (bilinear on that grid), each weighted by
// plane distance and normal (ScreenProbeAccumulate). Returns the total weight (0: no probe fits).
float ScreenProbeGatherSH(Texture2D<float4> positions, Texture2D<float4> normals, Texture2D<float4> sh, Texture2D<uint> adaptiveMap,
                          uint2 tiles, uint2 pixel, float3 position, float3 normal, float viewDistance,
                          out ScreenProbeSH result)
{
    [unroll] for (uint i = 0; i < SCREEN_PROBE_SH_TEXELS; ++i)
        result.c[i] = 0.0.xxx;
    float total = 0.0;
    const float2 grid = (float2(pixel) + 0.5) / 16.0 - 0.5;
    const int2 base = int2(floor(grid));
    const float2 fraction = grid - float2(base);
    const float2 subGrid = (float2(pixel) + 0.5) / 8.0 - 0.5;
    const int2 subBase = int2(floor(subGrid));
    const float2 subFraction = subGrid - float2(subBase);
    [unroll] for (uint corner = 0; corner < 4; ++corner)
    {
        const int2 offset = int2(corner & 1, corner >> 1);
        const float2 bilinear = lerp(1.0 - fraction, fraction, float2(offset));
        ScreenProbeAccumulate(positions, normals, sh, clamp(base + offset, int2(0, 0), int2(tiles) - 1), bilinear.x * bilinear.y,
                              position, normal, viewDistance, result, total);
        int2 tile;
        if (ScreenProbeAdaptiveTile(adaptiveMap, clamp(subBase + offset, int2(0, 0), int2(tiles * 2) - 1), tiles, tile))
        {
            const float2 subBilinear = lerp(1.0 - subFraction, subFraction, float2(offset));
            ScreenProbeAccumulate(positions, normals, sh, tile, subBilinear.x * subBilinear.y, position, normal, viewDistance,
                                  result, total);
        }
    }
    if (total > 0.0001)
    {
        [unroll] for (uint j = 0; j < SCREEN_PROBE_SH_TEXELS; ++j)
            result.c[j] /= total;
    }
    return total;
}

// Resolve history per pixel: view distance (half float) | octahedral normal (2 x 5 bits) | frames averaged (6 bits).
uint ScreenProbePackGeometry(float3 normal, float distance, uint frames)
{
    uint2 oct = uint2(saturate(ProbeOctEncode(normal)) * 31.0 + 0.5);
    return f32tof16(distance) | (oct.x << 16) | (oct.y << 21) | (min(frames, 63u) << 26);
}

void ScreenProbeUnpackGeometry(uint packed, out float3 normal, out float distance, out uint frames)
{
    distance = f16tof32(packed & 0xFFFFu);
    normal = ProbeOctDecode(float2((packed >> 16) & 31u, (packed >> 21) & 31u) / 31.0);
    frames = packed >> 26;
}

#ifdef SCREEN_PROBES
// Radiance arriving along `direction` at a screen probe from the world probes' far-field radiance cache, for a ray
// that ran out of length (Lumen's hand-off). The cache holds what each world probe sees beyond its interpolation
// footprint (RayTrace.hlsl), so a world probe on the far side of a thin wall next to the screen probe sees past the
// wall rather than the light behind it. Interpolated as ProbeLighting interpolates irradiance (trilinear x facing x
// visibility around the normal-biased point, inactive probes skipped) and bilinear between the cache's octahedral
// texels, in the finest cascade around the screen probe and blended into the next over its outer cell. Outside every
// cascade (or before the cache has data) the sky value the miss shader returned is used; the last cascade of a
// camera-following volume fades into it at its edges.
// One cascade's far-field radiance along `direction`; a = 1 when any probe counted.
float4 WorldRadianceCascade(uint cascade, float3 position, float3 normal, float3 direction)
{
    float3 origin = gi_originSpacing[cascade].xyz;
    float3 spacing = gi_spacing[cascade].xyz;
    float3 biased = position + normal * ProbeNormalBias(cascade);
    float3 grid = clamp((biased - origin) / spacing, 0.0, float(gi_gridSize - 1));
    uint3 base = min(uint3(grid), gi_gridSize - 2);
    float3 fraction = saturate(grid - float3(base));
    float2 texel = ProbeOctEncode(direction) * PROBE_RADIANCE_SIZE - 0.5;
    int2 corner0 = int2(floor(texel));
    float2 blend = texel - float2(corner0);
    float3 radiance = 0.0.xxx;
    float total = 0.0;
    [unroll] for (uint corner = 0; corner < 8; ++corner)
    {
        uint3 offset = uint3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
        uint3 cell = base + offset;
        uint probe = ProbeIndex(cascade, cell);
        float3 toSurface = biased - (origin + float3(cell) * spacing);
        float distance = length(toSurface);
        float3 toward = distance > 0.0001 ? toSurface / distance : -normal;
        float facing = saturate(dot(normal, -toward) * 0.5 + 0.5);
        float front = max(0.05, facing * facing);
        float2 moments = ProbeSample(gi_distance, probe, toward).xy;
        float variance = max(moments.y - moments.x * moments.x, 0.0001);
        float delta = max(distance - moments.x, 0.0);
        float visibility = variance / (variance + delta * delta);
        float3 trilinear = lerp(1.0 - fraction, fraction, float3(offset));
        float weight = trilinear.x * trilinear.y * trilinear.z * front * visibility * visibility * visibility *
                       ProbeSample(gi_irradiance, probe, direction).a;
        float3 sample = 0.0.xxx;
        float sampleWeight = 0.0;
        [unroll] for (uint tap = 0; tap < 4; ++tap)
        {
            int2 tapOffset = int2(tap & 1, tap >> 1);
            float2 bilinear = lerp(1.0 - blend, blend, float2(tapOffset));
            float4 cached = sp_worldRadiance.Load(int3(int2(ProbeRadianceTile(probe)) + clamp(corner0 + tapOffset, 0, PROBE_RADIANCE_SIZE - 1), 0));
            if (cached.a > 0.0)
            {
                sample += ProbeFiniteRadiance(cached.rgb) * bilinear.x * bilinear.y;
                sampleWeight += bilinear.x * bilinear.y;
            }
        }
        if (sampleWeight <= 0.0 || weight <= 0.0)
            continue;
        radiance += sample / sampleWeight * weight;
        total += weight;
    }
    return total > 0.00001 ? float4(radiance / total, 1.0) : 0.0.xxxx;
}

float3 WorldRadianceCache(float3 position, float3 normal, float3 direction, float3 fallback)
{
    if (gi_gridSize < 2 || gi_cascadeCount == 0)
        return fallback;
    float edge;
    uint cascade = ProbeCascadeAt(position, false, edge);
    if (cascade >= gi_cascadeCount)
        return fallback;
    float4 cached = WorldRadianceCascade(cascade, position, normal, direction);
    float inside = smoothstep(0.0, 1.0, edge);
    if (inside < 1.0)
    {
        if (cascade + 1 < gi_cascadeCount)
        {
            float4 next = WorldRadianceCascade(cascade + 1, position, normal, direction);
            cached = cached.a <= 0.0 ? next : (next.a > 0.0 ? float4(lerp(next.rgb, cached.rgb, inside), 1.0) : cached);
        }
        else if (gi_spacing[cascade].w > 0.0)
        {
            return cached.a > 0.0 ? lerp(fallback, cached.rgb, inside) : fallback;
        }
    }
    return cached.a > 0.0 ? cached.rgb : fallback;
}
#endif
#endif
