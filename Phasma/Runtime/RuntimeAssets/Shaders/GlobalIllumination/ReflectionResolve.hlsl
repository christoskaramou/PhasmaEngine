#include "../Common/Common.hlsl"
#include "ScreenProbeCommon.hlsl"
#include "ReflectionCommon.hlsl"
[[vk::binding(3, 0)]] Texture2D<float4> lr_rays; // per pixel: rgb radiance, a hit distance (< 0: none)
[[vk::binding(4, 0)]] Texture2D<float> lr_depth;
[[vk::binding(5, 0)]] Texture2D<float4> lr_normal;
[[vk::binding(6, 0)]] Texture2D<float4> lr_material; // srm target: y roughness
[[vk::binding(7, 0)]] Texture2D<float4> lr_probeSH;
[[vk::binding(10, 0)]] Texture2D<float4> lr_probePositions;
[[vk::binding(11, 0)]] Texture2D<float4> lr_probeNormals;
[[vk::binding(12, 0)]] Texture2D<uint> lr_probeGeometry; // ScreenProbeResolve's output this frame
[[vk::binding(13, 0)]] Texture2D<float4> lr_history;
[[vk::binding(16, 0)]] Texture2D<float2> lr_velocity;
[[vk::binding(17, 0)]] Texture2D<uint> lr_probeAdaptive;
[[vk::image_format("rgba16f")]][[vk::binding(15, 0)]] RWTexture2D<float4> lr_specularOut;

// Mean cosine of a GGX reflection lobe (von Mises-Fisher fit, sharpness 2 / alpha^2): how much of the SH's band 1
// survives in radiance prefiltered by that lobe.
float ReflectionLobeCosine(float roughness)
{
    const float alpha = roughness * roughness;
    const float kappa = 2.0 / max(alpha * alpha, 1e-4);
    return kappa > 20.0 ? 1.0 - 1.0 / kappa : 1.0 / tanh(kappa) - 1.0 / kappa;
}

// A thread per pixel: the reflected radiance prefiltered by this pixel's GGX lobe (LightingPS multiplies it by the
// split-sum F0 x A + B), a = frames averaged (0: no data, LightingPS keeps the cubemap).
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const uint2 pixel = id.xy;
    if (any(pixel >= uint2(sp_width, sp_height)))
        return;
    const float depth = lr_depth.Load(int3(pixel, 0));
    if (depth == 0.0)
    {
        lr_specularOut[pixel] = 0.0.xxxx;
        return;
    }
    const float2 size = float2(sp_width, sp_height);
    const float3 position = GetPosFromUV((float2(pixel) + 0.5) / size, depth, sp_invViewProj);
    const float3 N = normalize(lr_normal.Load(int3(pixel, 0)).xyz * 2.0 - 1.0);
    const float3 V = normalize(sp_camPos.xyz - position);
    const float viewDistance = length(sp_camPos.xyz - position);
    const float roughness = lr_material.Load(int3(pixel, 0)).y;
    const float3 R = reflect(-V, N);

    // Rough: the screen probes' radiance around the reflection direction, as Lumen reuses the screen probe gather.
    // The lobe is at most cosine-wide: SH from 64 rays cannot hold a sharper one, and a sharper band 1 only adds noise
    // (measured 2026-10-10 against an all-traced reference).
    ScreenProbeSH probeSH;
    const float probeWeight = ScreenProbeGatherSH(lr_probePositions, lr_probeNormals, lr_probeSH, lr_probeAdaptive, uint2(sp_tilesX, sp_tilesY),
                                                  pixel, position, N, viewDistance, probeSH);
    const float band1 = min(ReflectionLobeCosine(max(roughness, REFLECTION_MAX_ROUGHNESS - 0.1)), 2.0 / 3.0);
    const float3 rough = ScreenProbeSHRadiance(probeSH, R, band1);

    // Traced: this pixel reuses the rays of nearby pixels on the same surface (Lumen's screen-space reconstruction),
    // each weighted by this pixel's GGX lobe for the ray's direction over the pdf it was drawn with, which gives the
    // lobe-weighted mean radiance. The weights use a lobe at least 0.12 rough, so mirror pixels still take their
    // neighbours' rays. The rays' range bounds near-mirror history below.
    float3 traced = 0.0.xxx;
    float tracedWeight = 0.0;
    float3 low = 65504.0.xxx;
    float3 high = 0.0.xxx;
    if (roughness < REFLECTION_MAX_ROUGHNESS)
    {
        const float alpha = max(roughness, 0.12) * max(roughness, 0.12);
        // Pixels. Wider reuse biases: 8 px (Lumen's kernel) put a 0.3-rough floor 9% over a no-reuse reference, 4 px 2%.
        const float radius = 1.0 + 3.0 * saturate(roughness / REFLECTION_MAX_ROUGHNESS);
        const float angle = ProbeRandom(ProbeHash(pixel.x * 1973u ^ pixel.y * 9277u ^ sp_frame * 26699u)) * 2.0 * PI;
        for (uint k = 0; k < 9; ++k)
        {
            float2 offset = 0.0.xx;
            if (k > 0)
            {
                const float a = angle + float(k) * (2.0 * PI / 8.0);
                offset = float2(cos(a), sin(a)) * radius * (k % 2 == 0 ? 1.0 : 0.5);
            }
            const int2 neighbour = int2(pixel) + int2(round(offset));
            if (any(neighbour < 0) || any(neighbour >= int2(size)))
                continue;
            const float4 ray = lr_rays.Load(int3(neighbour, 0));
            if (ray.a < 0.0)
                continue;
            const uint2 source = uint2(neighbour);
            const float sourceDepth = lr_depth.Load(int3(source, 0));
            const float3 sourcePosition = GetPosFromUV((float2(source) + 0.5) / size, sourceDepth, sp_invViewProj);
            const float3 sourceN = normalize(lr_normal.Load(int3(source, 0)).xyz * 2.0 - 1.0);
            if (sourceDepth == 0.0 || dot(N, sourceN) < 0.9 || abs(dot(N, sourcePosition - position)) > 0.02 * viewDistance)
                continue;
            const float4 sample = ReflectionSample(sourceN, normalize(sp_camPos.xyz - sourcePosition),
                                                   lr_material.Load(int3(source, 0)).y, ReflectionRandom(source, sp_frame));
            const float NoL = dot(N, sample.xyz);
            if (NoL <= 0.0 || sample.w <= 0.0)
                continue;
            const float3 H = normalize(V + sample.xyz);
            const float weight = ReflectionD(saturate(dot(N, H)), alpha) * ReflectionG1(NoL, alpha) / sample.w;
            const float3 radiance = ProbeFiniteRadiance(ray.rgb);
            traced += radiance * weight;
            tracedWeight += weight;
            low = min(low, radiance);
            high = max(high, radiance);
        }
    }
    const bool hasTraced = tracedWeight > 0.0;
    if (!hasTraced && probeWeight < 0.05)
    {
        lr_specularOut[pixel] = 0.0.xxxx;
        return;
    }
    const float fade = probeWeight >= 0.05 ? saturate((roughness - (REFLECTION_MAX_ROUGHNESS - 0.1)) / 0.1) : 0.0;
    const float3 current = hasTraced ? lerp(traced / tracedWeight, rough, fade) : rough;

    // Reprojection by velocity where the screen probe resolve kept this surface's history (it tested the same taps), up
    // to 24 frames for traced reflections (Lumen keeps 12 but runs TSR after; 12 left a glossy floor flickering at
    // +0.37 points over GI off) and 32 for the screen probe path (as the diffuse resolve). Near-mirror history is
    // clamped to this frame's ray range, which limits the smear the surface reprojection gives mirror reflections;
    // glossier pixels are not, since a lamp caught by only some frames' rays would make the clamp flicker.
    float3 normalUnused;
    float distanceUnused;
    uint probeFrames;
    ScreenProbeUnpackGeometry(lr_probeGeometry.Load(int3(pixel, 0)), normalUnused, distanceUnused, probeFrames);
    float4 history = 0.0.xxxx;
    float historyWeight = 0.0;
    if (sp_reset == 0 && probeFrames >= 2)
    {
        bool valid;
        float objectMotion;
        const float2 coord = ScreenProbePreviousCoord(position, pixel, lr_velocity.Load(int3(pixel, 0)), valid, objectMotion);
        const int2 origin = int2(floor(coord));
        const float2 blend = coord - float2(origin);
        [unroll] for (uint tap = 0; tap < 4; ++tap)
        {
            const int2 offset = int2(tap & 1, tap >> 1);
            const int2 texel = origin + offset;
            if (!valid || any(texel < 0) || any(texel >= int2(size)))
                continue;
            const float4 old = lr_history.Load(int3(texel, 0));
            if (old.a <= 0.0)
                continue;
            const float2 bilinear = lerp(1.0 - blend, blend, float2(offset));
            history += old * (bilinear.x * bilinear.y);
            historyWeight += bilinear.x * bilinear.y;
        }
    }
    float count = 1.0;
    float3 result = current;
    if (historyWeight > 0.01)
    {
        history /= historyWeight;
        float3 previous = history.rgb;
        if (hasTraced && roughness < 0.15)
            previous = clamp(previous, low, high);
        count = min(history.a + 1.0, lerp(24.0, 32.0, hasTraced ? fade : 1.0));
        result = lerp(previous, current, 1.0 / count);
    }
    lr_specularOut[pixel] = float4(result, count);
}
