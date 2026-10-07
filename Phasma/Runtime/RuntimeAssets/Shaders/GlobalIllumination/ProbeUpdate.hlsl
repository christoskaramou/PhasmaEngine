#include "ProbeVolume.hlsl"
[[vk::binding(3, 0)]] Texture2D<float4> probeRays;
[[vk::image_format("rgba32f")]][[vk::binding(4, 0)]] RWTexture2D<float4> probeIrradiance;
[[vk::image_format("rgba32f")]][[vk::binding(5, 0)]] RWTexture2D<float4> probeDistance;

[numthreads(8, 8, 1)]
void main(uint3 dispatch : SV_DispatchThreadID)
{
    uint2 tile = dispatch.xy / 8;
    uint probe = tile.x + tile.y * gi_gridSize;
    if (probe >= gi_gridSize * gi_gridSize * gi_gridSize)
        return;
    int2 texel = int2(dispatch.xy % 8);
    // Mirror octahedral seams into the border; each thread writes its own texel.
    if (texel.x == 0 || texel.x == 7)
        texel = int2(texel.x == 0 ? 1 : 6, 7 - texel.y);
    if (texel.y == 0 || texel.y == 7)
        texel = int2(7 - texel.x, texel.y == 0 ? 1 : 6);
    float3 direction = ProbeOctDecode((float2(texel) - 0.5) / 6.0);
    float3 radiance = 0.0.xxx;
    float irradianceWeight = 0.0;
    float2 moments = 0.0.xx;
    float distanceWeight = 0.0;
    uint backfaces = 0;
    for (uint ray = 0; ray < gi_rayCount; ++ray)
    {
        float4 sample = probeRays.Load(int3(ray, probe, 0));
        backfaces += sample.a < 0.0 ? 1 : 0;
        float cosine = max(0.0, dot(direction, ProbeRayDirection(ray)));
        float weight = pow(cosine, 32.0);
        float distance = max(sample.a, 0.0);
        moments += float2(distance, distance * distance) * weight;
        distanceWeight += weight;
        if (sample.a >= 0.0)
        {
            radiance += max(sample.rgb, 0.0) * cosine;
            irradianceWeight += cosine;
        }
    }
    // The backface count of 64 rotating rays straddles the threshold near walls; alpha shares the history blend
    // so a probe fades in or out instead of toggling every frame.
    float active = backfaces * 4 < gi_rayCount ? 1.0 : 0.0;
    float4 irradiance = float4(3.14159265359 * radiance / max(irradianceWeight, 0.00001), active);
    float4 distance = float4(moments / max(distanceWeight, 0.00001), 0.0, 0.0);
    int3 cell = int3(probe % gi_gridSize, (probe / gi_gridSize) % gi_gridSize,
                     probe / (gi_gridSize * gi_gridSize));
    int3 shift = int3(round((gi_originSpacing.xyz - gi_historyOriginSpacing.xyz) / gi_spacing.xyz));
    int3 oldCell = cell + shift;
    uint2 pixel = dispatch.xy;
    // Progressive average: each texel weighs this frame by 1/n, n counting frames since the probe started or the
    // lighting last changed (kept in distance.z), up to the gi_hysteresis ceiling. Static lighting converges and
    // stops moving; a restart (bit 1) drops n to 49 so changes move at the old 0.98 rate instead of one noisy frame.
    float samples = 1.0;
    if ((gi_reset & 1u) == 0 && all(oldCell >= 0) && all(oldCell < int(gi_gridSize)))
    {
        uint oldProbe = oldCell.x + gi_gridSize * (oldCell.y + gi_gridSize * oldCell.z);
        uint2 oldPixel = uint2(oldProbe % gi_gridSize, oldProbe / gi_gridSize) * 8 + dispatch.xy % 8;
        float4 oldDistance = gi_distance.Load(int3(oldPixel, 0));
        float previous = (gi_reset & 2u) != 0 ? min(oldDistance.z, 49.0) : oldDistance.z;
        samples = previous + 1.0;
        float history = min(1.0 - 1.0 / samples, gi_hysteresis);
        irradiance = lerp(irradiance, gi_irradiance.Load(int3(oldPixel, 0)), history);
        distance.xy = lerp(distance.xy, oldDistance.xy, history);
    }
    distance.z = min(samples, 65536.0);
    probeIrradiance[pixel] = irradiance;
    probeDistance[pixel] = distance;
}
