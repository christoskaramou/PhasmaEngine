#include "../Common/Common.hlsl"
#include "ScreenProbeCommon.hlsl"
[[vk::binding(3, 0)]] Texture2D<float> sp_resolveDepth;
[[vk::binding(4, 0)]] Texture2D<float4> sp_resolveNormal;
[[vk::binding(5, 0)]] Texture2D<float4> sp_resolveSH;
[[vk::binding(6, 0)]] Texture2D<float4> sp_resolvePositions;
[[vk::binding(7, 0)]] Texture2D<float4> sp_resolveNormals;
[[vk::binding(10, 0)]] Texture2D<float4> sp_irradianceHistory;
[[vk::binding(11, 0)]] Texture2D<uint> sp_geometryHistory;
[[vk::binding(15, 0)]] Texture2D<float2> sp_resolveVelocity;
[[vk::binding(16, 0)]] Texture2D<uint> sp_resolveAdaptive;
[[vk::image_format("rgba16f")]][[vk::binding(12, 0)]] RWTexture2D<float4> sp_irradianceOut;
[[vk::image_format("r32ui")]][[vk::binding(13, 0)]] RWTexture2D<uint> sp_geometryOut;

// Lumen's short-range AO (screen space): contact occlusion the screen probes, 16 px apart, do not resolve. Each frame
// SP_AO_RAYS cosine-distributed rays per pixel march SP_AO_STEPS steps through the depth buffer up to 2% of the screen
// (Lumen's default, unverified); an occluder counts only within a slab as thick as that range, so a thin object in
// front does not shadow what lies far behind it. Returns the share of rays that got out; bentNormal is their mean
// direction (the normal when all or none got out). Applied before the temporal average, which smooths both. Against a
// path-traced reference it darkens creases between lit surfaces too much (wiki: rendering.md); kept because the
// target is Lumen's look.
#define SP_AO_RAYS 4
#define SP_AO_STEPS 6
float ScreenProbeShortRangeAO(uint2 pixel, float3 position, float3 normal, float pixelSize, out float3 bentNormal)
{
    const float2 size = float2(sp_width, sp_height);
    const float range = pixelSize * 0.02 * max(size.x, size.y);
    // Orthonormal basis around the normal (Duff et al. 2017).
    const float side = normal.z >= 0.0 ? 1.0 : -1.0;
    const float a = -1.0 / (side + normal.z);
    const float b = normal.x * normal.y * a;
    const float3 tangent = float3(1.0 + side * normal.x * normal.x * a, side * b, -side * normal.x);
    const float3 bitangent = float3(b, side + normal.y * normal.y * a, -normal.y);
    const float3 origin = position + normal * pixelSize;
    const uint seed = ProbeHash(pixel.x * 73856093u ^ pixel.y * 19349663u ^ sp_frame * 83492791u);
    float visible = 0.0;
    float3 bent = 0.0.xxx;
    [unroll] for (uint r = 0; r < SP_AO_RAYS; ++r)
    {
        const float2 u = float2((float(r) + ProbeRandom(seed + r * 2u)) / SP_AO_RAYS, ProbeRandom(seed + r * 2u + 1u));
        const float radius = sqrt(u.x);
        const float phi = 6.28318531 * u.y;
        const float3 direction = tangent * (radius * cos(phi)) + bitangent * (radius * sin(phi)) + normal * sqrt(1.0 - u.x);
        const float jitter = ProbeRandom(seed + 97u + r);
        bool occluded = false;
        for (uint s = 0; s < SP_AO_STEPS && !occluded; ++s)
        {
            const float3 sample = origin + direction * (range * (float(s) + jitter) / SP_AO_STEPS);
            const float4 clip = mul(float4(sample, 1.0), sp_viewProj);
            if (clip.w <= 0.0)
                break;
            const float2 uv = NdcToUv(clip.xy / clip.w);
            if (any(uv <= 0.0) || any(uv >= 1.0))
                break;
            const float sceneDepth = sp_resolveDepth.Load(int3(uv * size, 0));
            if (sceneDepth == 0.0)
                continue;
            const float sceneDistance = length(GetPosFromUV(uv, sceneDepth, sp_invViewProj) - sp_camPos.xyz);
            const float sampleDistance = length(sample - sp_camPos.xyz);
            occluded = sceneDistance < sampleDistance - pixelSize && sampleDistance - sceneDistance < range;
        }
        if (!occluded)
        {
            visible += 1.0;
            bent += direction;
        }
    }
    bentNormal = visible >= float(SP_AO_RAYS) || visible == 0.0 ? normal : normalize(bent);
    return visible / float(SP_AO_RAYS);
}

// A thread per pixel: irradiance (rgb) and screen probe coverage (a; LightingPS falls back to world probes for the
// rest), averaged over time.
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const uint2 pixel = id.xy;
    if (any(pixel >= uint2(sp_width, sp_height)))
        return;
    const float depth = sp_resolveDepth.Load(int3(pixel, 0));
    if (depth == 0.0)
    {
        sp_irradianceOut[pixel] = 0.0.xxxx;
        sp_geometryOut[pixel] = 0u;
        return;
    }
    const float2 size = float2(sp_width, sp_height);
    const float3 position = GetPosFromUV((float2(pixel) + 0.5) / size, depth, sp_invViewProj);
    const float3 normal = normalize(sp_resolveNormal.Load(int3(pixel, 0)).xyz * 2.0 - 1.0);
    const float viewDistance = length(position - sp_camPos.xyz);
    const float pixelSize = length(GetPosFromUV((float2(pixel) + float2(1.5, 0.5)) / size, depth, sp_invViewProj) - position);

    // The nearby screen probes' SH (ScreenProbeGatherSH), evaluated for the short-range bent normal and scaled by the
    // short-range AO, as Lumen applies them.
    ScreenProbeSH sh;
    const float total = ScreenProbeGatherSH(sp_resolvePositions, sp_resolveNormals, sp_resolveSH, sp_resolveAdaptive, uint2(sp_tilesX, sp_tilesY), pixel,
                                            position, normal, viewDistance, sh);
    float3 bentNormal;
    const float ao = ScreenProbeShortRangeAO(pixel, position, normal, pixelSize, bentNormal);
    const float3 irradiance = ScreenProbeSHIrradiance(sh, bentNormal) * ao;
    const float4 current = float4(total > 0.0001 ? irradiance : 0.0.xxx, saturate(total * 4.0));

    // Reprojection by velocity: bilinear history taps whose stored normal and distance match this surface are kept,
    // and the average grows to 32 frames. Taps that do not match (disocclusion) are dropped.
    float4 history = 0.0.xxxx;
    float frames = 0.0;
    float historyWeight = 0.0;
    if (sp_reset == 0)
    {
        bool valid;
        float objectMotion;
        const float2 coord = ScreenProbePreviousCoord(position, pixel, sp_resolveVelocity.Load(int3(pixel, 0)), valid, objectMotion);
        const int2 origin = int2(floor(coord));
        const float2 blend = coord - float2(origin);
        const float expected = length(position - sp_prevCamPos.xyz);
        // A moving surface's own motion also changes its distance: allow as much as it moved across the screen, in
        // world units at this depth (the size of a pixel here times the pixels it moved beyond the camera motion).
        const float movedBy = objectMotion * pixelSize;
        [unroll] for (uint tap = 0; tap < 4; ++tap)
        {
            const int2 offset = int2(tap & 1, tap >> 1);
            const int2 texel = origin + offset;
            if (!valid || any(texel < 0) || any(texel >= int2(size)))
                continue;
            const uint packed = sp_geometryHistory.Load(int3(texel, 0));
            if (packed == 0u)
                continue;
            float3 oldNormal;
            float oldDistance;
            uint oldFrames;
            ScreenProbeUnpackGeometry(packed, oldNormal, oldDistance, oldFrames);
            if (abs(oldDistance - expected) > 0.03 * expected + movedBy || dot(oldNormal, normal) < 0.9)
                continue;
            const float2 bilinear = lerp(1.0 - blend, blend, float2(offset));
            const float weight = bilinear.x * bilinear.y;
            history += sp_irradianceHistory.Load(int3(texel, 0)) * weight;
            frames += float(oldFrames) * weight;
            historyWeight += weight;
        }
    }
    float count = 1.0;
    float4 result = current;
    if (historyWeight > 0.01)
    {
        count = min(frames / historyWeight + 1.0, 32.0);
        result = lerp(history / historyWeight, current, 1.0 / count);
    }
    sp_irradianceOut[pixel] = result;
    sp_geometryOut[pixel] = ScreenProbePackGeometry(normal, viewDistance, uint(count + 0.5));
}
