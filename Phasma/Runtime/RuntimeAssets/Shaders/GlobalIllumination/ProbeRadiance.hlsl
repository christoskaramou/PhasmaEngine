#include "ProbeVolume.hlsl"
[[vk::binding(3, 0)]] Texture2D<float4> probeRays;
[[vk::binding(6, 0)]] Texture2D<float4> rayDirections;
[[vk::binding(7, 0)]] Texture2D<float4> radianceHistory;
[[vk::image_format("rgba32f")]][[vk::binding(10, 0)]] RWTexture2D<float4> probeRadiance;

groupshared uint s_texel[256];
groupshared float3 s_radiance[256];

// One workgroup per probe, one thread per radiance texel: the probe's directional radiance (the cache ProbeSample
// importance-samples from), averaged over this frame's rays that landed in each texel and blended into history.
[numthreads(16, 16, 1)]
void main(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID)
{
    const uint probe = group.x + group.y * gi_gridSize;
    const uint index = thread.x + thread.y * 16;
    const bool valid = probe < gi_gridSize * gi_gridSize * gi_gridSize;
    if (valid && index < gi_rayCount)
    {
        float4 sample = probeRays.Load(int3(index, probe, 0));
        uint2 texel = min(uint2(ProbeOctEncode(rayDirections.Load(int3(index, probe, 0)).xyz) * PROBE_RADIANCE_SIZE),
                          PROBE_RADIANCE_SIZE - 1);
        s_texel[index] = texel.x + texel.y * PROBE_RADIANCE_SIZE;
        s_radiance[index] = sample.a < 0.0 ? 0.0.xxx : ProbeFiniteRadiance(sample.rgb); // inside geometry sees nothing
    }
    GroupMemoryBarrierWithGroupSync();
    if (!valid)
        return;

    float3 sum = 0.0.xxx;
    uint count = 0;
    for (uint ray = 0; ray < gi_rayCount; ++ray)
    {
        if (s_texel[ray] == index)
        {
            sum += s_radiance[ray];
            ++count;
        }
    }
    const uint2 pixel = ProbeRadianceTile(probe) + thread.xy;
    float4 history = 0.0.xxxx;
    uint oldProbe;
    if (ProbeHistoryIndex(probe, oldProbe))
        history = radianceHistory.Load(int3(ProbeRadianceTile(oldProbe) + thread.xy, 0));
    if (count == 0)
    {
        probeRadiance[pixel] = history; // no ray landed here this frame
        return;
    }
    // A short window: the cache only steers rays, so it should follow lighting changes quickly.
    const float samples = min(history.a + 1.0, 32.0);
    probeRadiance[pixel] = float4(lerp(sum / float(count), history.rgb, 1.0 - 1.0 / samples), samples);
}
