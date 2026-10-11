#include "ProbeVolume.hlsl"
[[vk::binding(3, 0)]] Texture2D<float4> probeRays;
[[vk::binding(6, 0)]] Texture2D<float4> rayDirections;
[[vk::binding(7, 0)]] Texture2D<float4> radianceHistory;
[[vk::image_format("rgba32f")]][[vk::binding(10, 0)]] RWTexture2D<float4> probeRadiance;
[[vk::binding(4, 0)]] Texture2D<float4> farRays; // rgb far-field radiance, a < 0: none (RayTrace.hlsl)
[[vk::binding(5, 0)]] Texture2D<float4> farRadianceHistory;
[[vk::image_format("rgba32f")]][[vk::binding(11, 0)]] RWTexture2D<float4> probeFarRadiance;

groupshared uint s_texel[256];
groupshared float3 s_radiance[256];
groupshared float4 s_far[256];

// Averages this frame's rays that landed in the texel into its history (window up to 32 frames); a texel no ray
// reached keeps its history.
float4 BlendRadiance(float3 sum, uint count, float4 history)
{
    if (count == 0)
        return history;
    const float samples = min(history.a + 1.0, 32.0);
    return float4(lerp(sum / float(count), history.rgb, 1.0 - 1.0 / samples), samples);
}

// One workgroup per probe, one thread per radiance texel: the probe's directional radiance (the cache ProbeSample
// importance-samples from), averaged over this frame's rays that landed in each texel and blended into history.
[numthreads(16, 16, 1)]
void main(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID)
{
    const uint probe = group.x + group.y * gi_gridSize;
    const uint index = thread.x + thread.y * 16;
    const bool valid = probe < ProbeCount();
    if (valid && index < gi_rayCount)
    {
        float4 sample = probeRays.Load(int3(index, probe, 0));
        uint2 texel = min(uint2(ProbeOctEncode(rayDirections.Load(int3(index, probe, 0)).xyz) * PROBE_RADIANCE_SIZE),
                          PROBE_RADIANCE_SIZE - 1);
        s_texel[index] = texel.x + texel.y * PROBE_RADIANCE_SIZE;
        s_radiance[index] = sample.a < 0.0 ? 0.0.xxx : ProbeFiniteRadiance(sample.rgb); // inside geometry sees nothing
        float4 far = farRays.Load(int3(index, probe, 0));
        s_far[index] = float4(ProbeFiniteRadiance(far.rgb), far.a);
    }
    GroupMemoryBarrierWithGroupSync();
    if (!valid)
        return;

    float3 sum = 0.0.xxx;
    float3 farSum = 0.0.xxx;
    uint count = 0;
    uint farCount = 0;
    for (uint ray = 0; ray < gi_rayCount; ++ray)
    {
        if (s_texel[ray] == index)
        {
            sum += s_radiance[ray];
            ++count;
            if (s_far[ray].a >= 0.0)
            {
                farSum += s_far[ray].rgb;
                ++farCount;
            }
        }
    }
    const uint2 pixel = ProbeRadianceTile(probe) + thread.xy;
    float4 history = 0.0.xxxx;
    float4 farHistory = 0.0.xxxx;
    uint oldProbe;
    if (ProbeHistoryIndex(probe, oldProbe))
    {
        history = radianceHistory.Load(int3(ProbeRadianceTile(oldProbe) + thread.xy, 0));
        farHistory = farRadianceHistory.Load(int3(ProbeRadianceTile(oldProbe) + thread.xy, 0));
    }
    // A short window: the cache steers rays, so it should follow lighting changes quickly.
    probeRadiance[pixel] = BlendRadiance(sum, count, history);
    probeFarRadiance[pixel] = BlendRadiance(farSum, farCount, farHistory);
}
