#include "../Common/Common.hlsl"
#include "ScreenProbeCommon.hlsl"
[[vk::binding(3, 0)]] Texture2D<float4> sp_integrateRays;       // rgb radiance, a hit distance
[[vk::binding(4, 0)]] Texture2D<float4> sp_integrateDirections; // xyz direction, w pdf over the sphere
[[vk::binding(5, 0)]] Texture2D<float4> sp_integratePositions;
[[vk::binding(10, 0)]] Texture2D<float4> sp_integrateNormals;
[[vk::binding(11, 0)]] Texture2D<float4> sp_binPrior; // last frame's bin radiance (a = 1 when there is one)
[[vk::image_format("rgba16f")]][[vk::binding(7, 0)]] RWTexture2D<float4> sp_binRadianceOut; // a: mean hit distance

groupshared float3 s_radiance[64];
groupshared float s_distance[64];
groupshared uint s_bin[64];
groupshared float4 s_fill[64];

// One workgroup per probe; thread t is ray t, then direction bin t (8x8 world-space octahedral bins). Each bin's
// radiance is the mean of this frame's rays that landed in it; a bin no ray reached keeps last frame's value (the
// one the rays were aimed by), or without one the mean of the bins that have rays. The mean hit distance (0 when no
// ray landed) lets ScreenProbeFilter tell what a neighbour may share. The SH comes from the filtered bins there:
// projecting the rays themselves with 1/pdf weights would blow up at grazing angles, where the cosine pdf vanishes.
[numthreads(64, 1, 1)]
void main(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID)
{
    const uint2 tile = group.xy;
    const uint t = thread.x;
    const uint2 bin = uint2(t % 8, t / 8);
    if (sp_integratePositions.Load(int3(tile, 0)).w <= 0.0)
    {
        sp_binRadianceOut[tile * 8 + bin] = 0.0.xxxx;
        return;
    }
    const float4 ray = sp_integrateRays.Load(int3(tile * 8 + bin, 0));
    const float4 directionPdf = sp_integrateDirections.Load(int3(tile * 8 + bin, 0));
    s_radiance[t] = ProbeFiniteRadiance(ray.rgb);
    s_distance[t] = ray.a;
    const uint2 rayBin = min(uint2(ProbeOctEncode(directionPdf.xyz) * 8.0), 7u);
    s_bin[t] = directionPdf.w > 0.0 ? rayBin.x + rayBin.y * 8 : 64u;
    GroupMemoryBarrierWithGroupSync();

    float3 sum = 0.0.xxx;
    float distance = 0.0;
    uint count = 0;
    for (uint r = 0; r < 64; ++r)
    {
        if (s_bin[r] == t)
        {
            sum += s_radiance[r];
            distance += s_distance[r];
            ++count;
        }
    }
    const float3 normal = sp_integrateNormals.Load(int3(tile, 0)).xyz;
    const float3 p = ProbeOctPoint((float2(bin) + 0.5) / 8.0);
    const float3 direction = normalize(p);
    const bool above = dot(direction, normal) > 0.0;
    const float4 prior = sp_binPrior.Load(int3(tile * 8 + bin, 0));
    float3 radiance = count > 0 ? sum / float(count) : prior.rgb;
    const bool known = count > 0 || prior.a > 0.0;
    s_fill[t] = above && count > 0 ? float4(radiance, 1.0) : 0.0.xxxx;
    GroupMemoryBarrierWithGroupSync();
    for (uint stride = 32; stride > 0; stride >>= 1)
    {
        if (t < stride)
            s_fill[t] += s_fill[t + stride];
        GroupMemoryBarrierWithGroupSync();
    }
    if (!known)
        radiance = s_fill[0].a > 0.0 ? s_fill[0].rgb / s_fill[0].a : 0.0.xxx;
    sp_binRadianceOut[tile * 8 + bin] = float4(radiance, count > 0 ? distance / float(count) : 0.0);
}
