#include "../Common/Common.hlsl"
#include "ScreenProbeCommon.hlsl"
[[vk::binding(3, 0)]] Texture2D<float4> sp_filterBins; // ScreenProbeIntegrate: rgb bin radiance, a mean hit distance (0: no ray)
[[vk::binding(4, 0)]] Texture2D<float4> sp_filterPositions;
[[vk::binding(5, 0)]] Texture2D<float4> sp_filterNormals;
[[vk::image_format("rgba32f")]][[vk::binding(6, 0)]] RWTexture2D<float4> sp_filteredSHOut;

#define SP_FILTER_HIT_ANGLE_COS 0.98480775 // cos 10 degrees

groupshared float3 s_sh[SCREEN_PROBE_SH_TEXELS][64];

// Lumen's spatial probe filter: a group per probe, thread t is direction bin t (8x8 world-space octahedral bins). A
// bin's radiance is averaged with the same bin of the 3x3 neighbouring probes on the same surface (tent x facing^8 x
// within 2% of the view distance of this probe's plane), but only where the neighbour's hit along that bin, seen from
// this probe, lies within 10 degrees of the bin (Lumen's angle check, unverified default): distant light is shared, an
// occluder close to one probe is not. The filtered bins are projected onto L2 SH over the hemisphere above the surface
// (the lower half is dark). Adaptive probes have no grid neighbours and keep their own bins.
[numthreads(64, 1, 1)]
void main(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID)
{
    const int2 tile = int2(group.xy);
    const uint t = thread.x;
    const int2 bin = int2(t % 8, t / 8);
    const float4 centre = sp_filterPositions.Load(int3(tile, 0));
    if (centre.w <= 0.0)
    {
        if (t < SCREEN_PROBE_SH_TEXELS)
            sp_filteredSHOut[uint2(tile.x * SCREEN_PROBE_SH_TEXELS + t, tile.y)] = 0.0.xxxx;
        return;
    }
    const float3 normal = sp_filterNormals.Load(int3(tile, 0)).xyz;
    const float3 p = ProbeOctPoint((float2(bin) + 0.5) / 8.0);
    const float3 direction = normalize(p);
    const bool adaptive = tile.y >= int(sp_tilesY);
    float3 radiance = 0.0.xxx;
    float total = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const bool self = x == 0 && y == 0;
            const int2 neighbour = tile + int2(x, y);
            if (!self && (adaptive || any(neighbour < 0) || any(neighbour >= int2(sp_tilesX, sp_tilesY))))
                continue;
            const float4 position = sp_filterPositions.Load(int3(neighbour, 0));
            if (position.w <= 0.0)
                continue;
            const float facing = saturate(dot(normal, sp_filterNormals.Load(int3(neighbour, 0)).xyz));
            const float plane = abs(dot(normal, position.xyz - centre.xyz)) / (0.02 * centre.w);
            const float weight = (x == 0 ? 1.0 : 0.5) * (y == 0 ? 1.0 : 0.5) * pow(facing, 8.0) * saturate(1.0 - plane);
            if (weight <= 0.0)
                continue;
            const float4 sample = sp_filterBins.Load(int3(neighbour * 8 + bin, 0));
            if (!self && (sample.a <= 0.0 || dot(normalize(position.xyz + direction * sample.a - centre.xyz), direction) < SP_FILTER_HIT_ANGLE_COS))
                continue;
            radiance += sample.rgb * weight;
            total += weight;
        }
    }
    radiance = total > 0.0 ? radiance / total : 0.0.xxx;

    float basis[SCREEN_PROBE_SH_TEXELS];
    ScreenProbeSHBasis(direction, basis);
    const float solidAngle = dot(direction, normal) > 0.0 ? (4.0 / 64.0) / pow(length(p), 3.0) : 0.0;
    [unroll] for (uint i = 0; i < SCREEN_PROBE_SH_TEXELS; ++i)
        s_sh[i][t] = radiance * (basis[i] * solidAngle);
    GroupMemoryBarrierWithGroupSync();
    for (uint step = 32; step > 0; step >>= 1)
    {
        if (t < step)
        {
            [unroll] for (uint j = 0; j < SCREEN_PROBE_SH_TEXELS; ++j)
                s_sh[j][t] += s_sh[j][t + step];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (t < SCREEN_PROBE_SH_TEXELS)
        sp_filteredSHOut[uint2(tile.x * SCREEN_PROBE_SH_TEXELS + t, tile.y)] = float4(s_sh[t][0], 0.0);
}
