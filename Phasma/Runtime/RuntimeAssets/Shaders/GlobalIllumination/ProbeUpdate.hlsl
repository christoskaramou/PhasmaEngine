#include "ProbeVolume.hlsl"
[[vk::binding(3, 0)]] Texture2D<float4> probeRays;
[[vk::binding(6, 0)]] Texture2D<float4> rayDirections; // xyz direction, w pdf over the sphere
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
    float backfaces = 0.0;
    float rayWeight = 0.0;
    for (uint ray = 0; ray < gi_rayCount; ++ray)
    {
        float4 sample = probeRays.Load(int3(ray, probe, 0));
        float4 directionPdf = rayDirections.Load(int3(ray, probe, 0));
        // Every sum is weighted by 1/pdf, so importance-sampled rays still estimate the same integrals.
        float inversePdf = 1.0 / max(directionPdf.w, 0.000001);
        backfaces += sample.a < 0.0 ? inversePdf : 0.0;
        rayWeight += inversePdf;
        float cosine = max(0.0, dot(direction, directionPdf.xyz));
        float weight = pow(cosine, 32.0) * inversePdf;
        float distance = max(sample.a, 0.0);
        moments += float2(distance, distance * distance) * weight;
        distanceWeight += weight;
        if (sample.a >= 0.0)
        {
            radiance += ProbeFiniteRadiance(sample.rgb) * cosine * inversePdf;
            irradianceWeight += cosine * inversePdf;
        }
    }
    // The backface fraction straddles the threshold near walls; alpha shares the history blend so a probe fades in
    // or out instead of toggling every frame.
    float active = backfaces * 4.0 < rayWeight ? 1.0 : 0.0;
    float4 irradiance = float4(3.14159265359 * radiance / max(irradianceWeight, 0.00001), active);
    float4 distance = float4(moments / max(distanceWeight, 0.00001), 0.0, 0.0);
    uint2 pixel = dispatch.xy;
    // Per texel the history keeps the mean (irradiance), the sample count (distance.z) and the variance of the
    // per-frame change statistic (distance.w; negative when the last frame was outside the noise). The statistic is
    // the largest per-channel difference to the mean, so colour-only changes count. Two frames in a row outside the
    // noise (4 sigma + 2%), or a visibility change (active flag flipped, mean hit distance beyond its spread), are a
    // real change: the texel's average is held at the response window gi_hysteresis asks for (1 / (1 - h), 4 to 49
    // frames) while the change continues; a single outlier, which a bright emitter's heavy-tailed noise makes, is
    // not. Frames outside the noise are left out of its estimate, so a change does not raise its own threshold.
    // Everything else is noise and keeps averaging, up to 1000 frames whatever gi_hysteresis is; a known lighting or
    // geometry change (reset bit 1) caps that at 200 frames.
    float samples = 1.0;
    float variance = 0.0;
    bool pending = false;
    uint oldProbe;
    if (ProbeHistoryIndex(probe, oldProbe))
    {
        uint2 oldPixel = uint2(oldProbe % gi_gridSize, oldProbe / gi_gridSize) * 8 + dispatch.xy % 8;
        float4 oldIrradiance = gi_irradiance.Load(int3(oldPixel, 0));
        float4 oldDistance = gi_distance.Load(int3(oldPixel, 0));
        float3 delta = abs(irradiance.rgb - oldIrradiance.rgb);
        float difference = max(delta.r, max(delta.g, delta.b));
        float scale = max(oldIrradiance.r, max(oldIrradiance.g, oldIrradiance.b));
        float spread = sqrt(max(oldDistance.y - oldDistance.x * oldDistance.x, 0.0));
        bool visibilityChanged = abs(irradiance.a - oldIrradiance.a) > 0.5 ||
                                 abs(distance.x - oldDistance.x) > 4.0 * spread + 0.05 * oldDistance.x + 0.01;
        float previous = oldDistance.z;
        float oldVariance = abs(oldDistance.w);
        bool outside = previous >= 4.0 && (difference > 4.0 * sqrt(oldVariance) + 0.02 * scale + 0.0001 || visibilityChanged);
        if (outside && oldDistance.w < 0.0)
            previous = min(previous, clamp(1.0 / max(1.0 - gi_hysteresis, 0.001), 4.0, 49.0));
        if ((gi_reset & 2u) != 0)
            previous = min(previous, 200.0);
        pending = outside;
        samples = min(previous + 1.0, 1000.0);
        float history = 1.0 - 1.0 / samples;
        irradiance = lerp(irradiance, oldIrradiance, history);
        distance.xy = lerp(distance.xy, oldDistance.xy, history);
        variance = outside ? oldVariance : lerp(difference * difference, oldVariance, 1.0 - 1.0 / min(samples, 64.0));
    }
    distance.z = samples;
    distance.w = pending ? -max(variance, 1e-20) : variance;
    probeIrradiance[pixel] = irradiance;
    probeDistance[pixel] = distance;
}
