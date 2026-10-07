#ifndef PROBE_VOLUME_H
#define PROBE_VOLUME_H
// Literal bindings let the DX12 source reflector recover register spaces.
#ifdef GI_LIGHTING
[[vk::binding(0, 3)]] cbuffer ProbeLightingVolume
{
    float4 gi_originSpacing;
    uint gi_gridSize;
    uint gi_rayCount;
    uint gi_frame;
    uint gi_reset;
    float gi_maxDistance;
    float gi_intensity;
    float gi_normalBias;
    float gi_hysteresis;
    float4 gi_historyOriginSpacing;
    float4 gi_spacing; // xyz per-axis spacing; w = 1 when the volume follows the camera
};
[[vk::binding(1, 3)]] Texture2D<float4> gi_lightingIrradiance;
[[vk::binding(2, 3)]] Texture2D<float4> gi_lightingDistance;
[[vk::binding(3, 3)]] SamplerState gi_lightingSampler;
#define gi_irradiance gi_lightingIrradiance
#define gi_distance gi_lightingDistance
#define gi_sampler gi_lightingSampler
#elif defined(GI_PROBES) || defined(GI_SHADING)
[[vk::binding(0, 2)]] cbuffer ProbeTraceVolume
{
    float4 gi_originSpacing;
    uint gi_gridSize;
    uint gi_rayCount;
    uint gi_frame;
    uint gi_reset;
    float gi_maxDistance;
    float gi_intensity;
    float gi_normalBias;
    float gi_hysteresis;
    float4 gi_historyOriginSpacing;
    float4 gi_spacing; // xyz per-axis spacing; w = 1 when the volume follows the camera
};
[[vk::binding(1, 2)]] Texture2D<float4> gi_traceIrradiance;
[[vk::binding(2, 2)]] Texture2D<float4> gi_traceDistance;
[[vk::binding(3, 2)]] SamplerState gi_traceSampler;
#define gi_sampler gi_traceSampler
#define gi_irradiance gi_traceIrradiance
#define gi_distance gi_traceDistance
#else
[[vk::binding(8, 0)]] cbuffer ProbeVolume
{
    float4 gi_originSpacing;
    uint gi_gridSize;
    uint gi_rayCount;
    uint gi_frame;
    uint gi_reset;
    float gi_maxDistance;
    float gi_intensity;
    float gi_normalBias;
    float gi_hysteresis;
    float4 gi_historyOriginSpacing;
    float4 gi_spacing; // xyz per-axis spacing; w = 1 when the volume follows the camera
};
[[vk::binding(1, 0)]] Texture2D<float4> gi_irradiance;
[[vk::binding(2, 0)]] Texture2D<float4> gi_distance;
[[vk::binding(9, 0)]] SamplerState gi_sampler;
#endif

float3 ProbePosition(uint probe)
{
    uint3 cell = uint3(probe % gi_gridSize, (probe / gi_gridSize) % gi_gridSize,
                      probe / (gi_gridSize * gi_gridSize));
    return gi_originSpacing.xyz + float3(cell) * gi_spacing.xyz;
}

float3 ProbeRayDirection(uint ray)
{
    float z = 1.0 - 2.0 * (float(ray) + 0.5) / float(gi_rayCount);
    float angle = float(ray) * 2.399963229728653 + float(gi_frame % 1024) * 0.61803398875;
    float radius = sqrt(max(0.0, 1.0 - z * z));
    float3 direction = float3(radius * cos(angle), radius * sin(angle), z);
    float rotation = float(gi_frame % 1024) * 1.32471795724;
    return float3(direction.x, direction.y * cos(rotation) - direction.z * sin(rotation),
                  direction.y * sin(rotation) + direction.z * cos(rotation));
}

float2 ProbeOctEncode(float3 direction)
{
    direction /= dot(abs(direction), 1.0.xxx);
    float2 uv = direction.xy;
    if (direction.z < 0.0)
        uv = (1.0 - abs(uv.yx)) * float2(uv.x >= 0.0 ? 1.0 : -1.0, uv.y >= 0.0 ? 1.0 : -1.0);
    return uv * 0.5 + 0.5;
}

float3 ProbeOctDecode(float2 uv)
{
    float2 xy = uv * 2.0 - 1.0;
    float3 direction = float3(xy, 1.0 - abs(xy.x) - abs(xy.y));
    if (direction.z < 0.0)
        direction.xy = (1.0 - abs(direction.yx)) * float2(direction.x >= 0.0 ? 1.0 : -1.0,
                                                         direction.y >= 0.0 ? 1.0 : -1.0);
    return normalize(direction);
}

float4 ProbeSample(Texture2D<float4> atlas, uint probe, float3 direction)
{
    float2 tile = float2(probe % gi_gridSize, probe / gi_gridSize) * 8.0;
    float2 dimensions = float2(gi_gridSize * 8, gi_gridSize * gi_gridSize * 8);
    float2 uv = (tile + ProbeOctEncode(direction) * 6.0 + 1.0) / dimensions;
    return atlas.SampleLevel(gi_sampler, uv, 0);
}

float4 ProbeLighting(float3 position, float3 normal)
{
    if (gi_gridSize < 2)
        return 0.0.xxxx;
    float4 originSpacing = gi_originSpacing;
#ifdef GI_PROBES
    // Ray feedback reads last frame's atlas; lighting reads the atlas updated this frame.
    if ((gi_reset & 1u) != 0)
        return 0.0.xxxx;
    originSpacing = gi_historyOriginSpacing;
#endif
    float3 biased = position + normal * gi_normalBias;
    float3 grid = (biased - originSpacing.xyz) / gi_spacing.xyz;
    if (any(grid < 0.0) || any(grid > float(gi_gridSize - 1)))
        return 0.0.xxxx;
    uint3 base = min(uint3(grid), gi_gridSize - 2);
    float3 fraction = saturate(grid - float3(base));
    float3 edge = min(grid, float(gi_gridSize - 1) - grid);
    // Only a camera-following volume has edges in view; a volume fitted to the scene covers every surface.
    float fade = gi_spacing.w > 0.0 ? smoothstep(0.0, 1.0, min(edge.x, min(edge.y, edge.z))) : 1.0;
    float3 irradiance = 0.0.xxx;
    float totalWeight = 0.0;
    [unroll] for (uint corner = 0; corner < 8; ++corner)
    {
        uint3 offset = uint3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
        uint3 cell = base + offset;
        uint probe = cell.x + gi_gridSize * (cell.y + gi_gridSize * cell.z);
        float3 probePosition = originSpacing.xyz + float3(cell) * gi_spacing.xyz;
        float3 toSurface = biased - probePosition;
        float distance = length(toSurface);
        float3 direction = distance > 0.0001 ? toSurface / distance : normal;
        float2 moments = ProbeSample(gi_distance, probe, direction).xy;
        float variance = max(moments.y - moments.x * moments.x, 0.0001);
        float delta = max(distance - moments.x, 0.0);
        float visibility = variance / (variance + delta * delta);
        visibility = visibility * visibility * visibility;
        float3 trilinear = lerp(1.0 - fraction, fraction, float3(offset));
        float weight = trilinear.x * trilinear.y * trilinear.z;
        float facing = saturate(dot(normal, -direction) * 0.5 + 0.5);
        weight *= max(0.05, facing * facing) * visibility;
        float4 sample = ProbeSample(gi_irradiance, probe, normal);
        weight *= sample.a;
        irradiance += sample.rgb * weight;
        totalWeight += weight;
    }
    return totalWeight > 0.00001 ? float4(irradiance / totalWeight, fade) : 0.0.xxxx;
}
#endif
