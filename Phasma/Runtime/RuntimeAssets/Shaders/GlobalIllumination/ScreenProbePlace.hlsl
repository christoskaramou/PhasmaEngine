#include "../Common/Common.hlsl"
#include "ScreenProbeCommon.hlsl"
[[vk::binding(3, 0)]] Texture2D<float> sp_placeDepth;
[[vk::binding(4, 0)]] Texture2D<float4> sp_placeNormal;
[[vk::binding(5, 0)]] Texture2D<float4> sp_positionHistory;
[[vk::binding(6, 0)]] Texture2D<float4> sp_normalHistory;
[[vk::binding(7, 0)]] Texture2D<float4> sp_binRadianceHistory;
[[vk::binding(15, 0)]] Texture2D<float2> sp_placeVelocity;
[[vk::image_format("rgba32f")]][[vk::binding(10, 0)]] RWTexture2D<float4> sp_positionsOut;
[[vk::image_format("rgba16f")]][[vk::binding(11, 0)]] RWTexture2D<float4> sp_normalsOut;
[[vk::image_format("rgba16f")]][[vk::binding(12, 0)]] RWTexture2D<float4> sp_directionsOut;
[[vk::image_format("rgba16f")]][[vk::binding(13, 0)]] RWTexture2D<float4> sp_binPriorOut; // a = 1: from history
[[vk::image_format("r32ui")]][[vk::binding(16, 0)]] RWTexture2D<uint> sp_adaptiveMapOut;
[[vk::binding(17, 0)]] RWStructuredBuffer<uint> sp_adaptiveCountOut;

groupshared uint s_choice;
groupshared float s_cosine[64];
groupshared float s_light[64];
groupshared float s_cdf[64];

void InclusiveScan(uint t, inout float value, uint which)
{
    for (uint offset = 1; offset < 64; offset <<= 1)
    {
        float add = 0.0;
        if (t >= offset)
            add = which == 0 ? s_cosine[t - offset] : (which == 1 ? s_light[t - offset] : s_cdf[t - offset]);
        GroupMemoryBarrierWithGroupSync();
        value += add;
        if (which == 0)
            s_cosine[t] = value;
        else if (which == 1)
            s_light[t] = value;
        else
            s_cdf[t] = value;
        GroupMemoryBarrierWithGroupSync();
    }
}

// One workgroup per 16x16 tile. Thread t is a placement candidate, then direction bin t (8x8 world-space octahedral
// bins), then ray t.
[numthreads(64, 1, 1)]
void main(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID)
{
    const uint2 tile = group.xy;
    const uint t = thread.x;
    const uint2 bin = uint2(t % 8, t / 8);
    const uint2 rayPixel = tile * 8 + bin;

    // Clear what ScreenProbeAdaptive.hlsl fills next: this tile's 2x2 sub-tiles, one adaptive slot, the slot counter.
    if (t < 4)
        sp_adaptiveMapOut[tile * 2 + uint2(t & 1, t >> 1)] = 0;
    if (t == 4 && tile.y < sp_adaptiveRows)
        sp_positionsOut[uint2(tile.x, sp_tilesY + tile.y)] = 0.0.xxxx;
    if (t == 5 && all(tile == 0))
        sp_adaptiveCountOut[0] = 0;

    // Placement: 64 candidates on a 2-pixel lattice; the lattice phase and the preferred candidate change every frame,
    // so over time every pixel of the tile hosts the probe. A sky candidate gives way to the nearest one with geometry.
    const uint jitter = ProbeHash(tile.x * 73856093u ^ tile.y * 19349663u ^ sp_frame * 83492791u);
    const uint2 phase = uint2(jitter & 1u, (jitter >> 1) & 1u);
    const int2 preferred = int2((jitter >> 2) & 7u, (jitter >> 5) & 7u);
    const uint2 pixel = tile * 16 + bin * 2 + phase;
    const bool valid = all(pixel < uint2(sp_width, sp_height)) && sp_placeDepth.Load(int3(pixel, 0)) > 0.0;
    const int2 offset = int2(bin) - preferred;
    if (t == 0)
        s_choice = 0xFFFFFFFFu;
    GroupMemoryBarrierWithGroupSync();
    if (valid)
        InterlockedMin(s_choice, (uint(dot(offset, offset)) << 6) | t);
    GroupMemoryBarrierWithGroupSync();
    const uint choice = s_choice;
    if (choice == 0xFFFFFFFFu)
    {
        if (t == 0)
        {
            sp_positionsOut[tile] = 0.0.xxxx;
            sp_normalsOut[tile] = 0.0.xxxx;
        }
        sp_directionsOut[rayPixel] = 0.0.xxxx;
        sp_binPriorOut[rayPixel] = 0.0.xxxx;
        return;
    }
    const uint chosen = choice & 63u;
    const uint2 probePixel = tile * 16 + uint2(chosen % 8, chosen / 8) * 2 + phase;
    const float depth = sp_placeDepth.Load(int3(probePixel, 0));
    const float3 position = GetPosFromUV((float2(probePixel) + 0.5) / float2(sp_width, sp_height), depth, sp_invViewProj);
    const float3 normal = normalize(sp_placeNormal.Load(int3(probePixel, 0)).xyz * 2.0 - 1.0);
    const float viewDistance = length(position - sp_camPos.xyz);
    if (t == 0)
    {
        sp_positionsOut[tile] = float4(position, viewDistance);
        sp_normalsOut[tile] = float4(normal, 0.0);
    }

    // Last frame's probe on the same surface (reprojected tile, normals within 25 degrees, on this probe's plane)
    // supplies the radiance the rays are aimed by.
    bool history = false;
    uint2 oldTile = 0;
    if (sp_reset == 0)
    {
        bool valid;
        float objectMotion;
        const float2 coord = ScreenProbePreviousCoord(position, probePixel, sp_placeVelocity.Load(int3(probePixel, 0)), valid, objectMotion) + 0.5;
        if (valid && all(coord >= 0.0) && all(coord < float2(sp_width, sp_height)))
        {
            oldTile = min(uint2(coord) / 16, uint2(sp_tilesX - 1, sp_tilesY - 1));
            float4 oldPosition = sp_positionHistory.Load(int3(oldTile, 0));
            float3 oldNormal = sp_normalHistory.Load(int3(oldTile, 0)).xyz;
            history = oldPosition.w > 0.0 && dot(oldNormal, normal) > 0.9 &&
                      abs(dot(normal, oldPosition.xyz - position)) < 0.05 * viewDistance;
        }
    }

    // Bin probability = 0.25 x its share of the cosine-weighted hemisphere + 0.75 x its share of cosine x last
    // frame's radiance, so every direction above the surface keeps a non-zero pdf (as ProbeSample.hlsl does for the
    // world probes). Without history the rays are cosine-distributed.
    const float texelArea = 4.0 / 64.0; // in [-1,1]^2
    const float3 p = ProbeOctPoint((float2(bin) + 0.5) / 8.0);
    float cosine = max(dot(normalize(p), normal), 0.0) * texelArea / pow(length(p), 3.0);
    // Also the bin's radiance for ScreenProbeIntegrate when no ray lands in it this frame.
    const float3 prior = history ? ProbeFiniteRadiance(sp_binRadianceHistory.Load(int3(oldTile * 8 + bin, 0)).rgb) : 0.0.xxx;
    sp_binPriorOut[rayPixel] = float4(prior, history ? 1.0 : 0.0);
    float light = dot(prior, float3(0.2126, 0.7152, 0.0722)) * cosine;
    const float ownCosine = cosine;
    const float ownLight = light;
    s_cosine[t] = cosine;
    s_light[t] = light;
    GroupMemoryBarrierWithGroupSync();
    InclusiveScan(t, cosine, 0);
    InclusiveScan(t, light, 1);
    const float totalCosine = s_cosine[63];
    const float totalLight = s_light[63];
    float weight = totalLight > 0.0 ? 0.25 * ownCosine / totalCosine + 0.75 * ownLight / totalLight : ownCosine / totalCosine;
    s_cdf[t] = weight;
    GroupMemoryBarrierWithGroupSync();
    InclusiveScan(t, weight, 2);
    const float total = s_cdf[63];

    // Stratified: ray t takes the t-th of 64 equal slices of the distribution, all shifted by one random offset.
    const uint probe = tile.x + tile.y * sp_tilesX;
    const uint seed = probe * 64u + t + sp_frame * 131071u;
    const float target = (float(t) + ProbeRandom(probe * 9781u + sp_frame * 6271u)) / 64.0 * total;
    uint low = 0;
    uint high = 63;
    while (low < high)
    {
        uint middle = (low + high) / 2;
        if (s_cdf[middle] > target)
            high = middle;
        else
            low = middle + 1;
    }
    const float probability = (s_cdf[low] - (low > 0 ? s_cdf[low - 1] : 0.0)) / total;
    const float2 uv = (float2(low % 8, low / 8) + float2(ProbeRandom(seed * 2u), ProbeRandom(seed * 2u + 1u))) / 8.0;
    const float3 q = ProbeOctPoint(uv);
    // pdf over the sphere = bin probability / bin area in [-1,1]^2 x |q|^3.
    sp_directionsOut[rayPixel] = float4(normalize(q), probability / texelArea * pow(length(q), 3.0));
}
