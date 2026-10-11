#ifndef PROBE_VOLUME_H
#define PROBE_VOLUME_H
#define GI_MAX_CASCADES 4 // GlobalIlluminationPass::kCascades
#if defined(SCREEN_PROBES) || defined(REFLECTIONS)
#define GI_AFTER_UPDATE 1 // traced after GlobalIlluminationPass: its atlases already hold this frame
#endif
// Literal bindings let the DX12 source reflector recover register spaces.
#ifdef GI_LIGHTING
[[vk::binding(0, 3)]] cbuffer ProbeLightingVolume
{
    float4 gi_originSpacing[4];        // per cascade: xyz origin, w its smallest spacing
    float4 gi_historyOriginSpacing[4]; // last frame's
    float4 gi_spacing[4];              // per cascade: xyz spacing; w = 1 when the volume follows the camera
    uint gi_gridSize;
    uint gi_rayCount;
    uint gi_frame;
    uint gi_reset;
    uint gi_cascadeCount;
    float gi_intensity;
    float gi_hysteresis;
    uint gi_padding;
};
[[vk::binding(1, 3)]] Texture2D<float4> gi_lightingIrradiance;
[[vk::binding(2, 3)]] Texture2D<float4> gi_lightingDistance;
[[vk::binding(3, 3)]] SamplerState gi_lightingSampler;
[[vk::binding(4, 3)]] Texture2D<float4> gi_screenIrradiance; // ScreenProbePass: rgb irradiance, a = coverage
[[vk::binding(5, 3)]] Texture2D<float4> gi_reflections; // ReflectionPass: rgb prefiltered radiance, a > 0: has data
#define gi_irradiance gi_lightingIrradiance
#define gi_distance gi_lightingDistance
#define gi_sampler gi_lightingSampler
#elif defined(GI_PROBES) || defined(GI_SHADING)
[[vk::binding(0, 2)]] cbuffer ProbeTraceVolume
{
    float4 gi_originSpacing[4];        // per cascade: xyz origin, w its smallest spacing
    float4 gi_historyOriginSpacing[4]; // last frame's
    float4 gi_spacing[4];              // per cascade: xyz spacing; w = 1 when the volume follows the camera
    uint gi_gridSize;
    uint gi_rayCount;
    uint gi_frame;
    uint gi_reset;
    uint gi_cascadeCount;
    float gi_intensity;
    float gi_hysteresis;
    uint gi_padding;
};
[[vk::binding(1, 2)]] Texture2D<float4> gi_traceIrradiance;
[[vk::binding(2, 2)]] Texture2D<float4> gi_traceDistance;
[[vk::binding(3, 2)]] SamplerState gi_traceSampler;
#ifdef GI_PROBES
[[vk::binding(4, 2)]] Texture2D<float4> gi_rayDirections; // xyz direction, w pdf (ProbeSample.hlsl)
#endif
#if defined(GI_PROBES) && !defined(GI_AFTER_UPDATE)
[[vk::image_format("rgba16f")]][[vk::binding(5, 2)]] RWTexture2D<float4> gi_farRays; // rgb far-field radiance, a < 0: none
#endif
#define gi_sampler gi_traceSampler
#define gi_irradiance gi_traceIrradiance
#define gi_distance gi_traceDistance
#else
[[vk::binding(8, 0)]] cbuffer ProbeVolume
{
    float4 gi_originSpacing[4];        // per cascade: xyz origin, w its smallest spacing
    float4 gi_historyOriginSpacing[4]; // last frame's
    float4 gi_spacing[4];              // per cascade: xyz spacing; w = 1 when the volume follows the camera
    uint gi_gridSize;
    uint gi_rayCount;
    uint gi_frame;
    uint gi_reset;
    uint gi_cascadeCount;
    float gi_intensity;
    float gi_hysteresis;
    uint gi_padding;
};
[[vk::binding(1, 0)]] Texture2D<float4> gi_irradiance;
[[vk::binding(2, 0)]] Texture2D<float4> gi_distance;
[[vk::binding(9, 0)]] SamplerState gi_sampler;
#endif

// Probes are numbered across cascades: cascade x gridSize^3 + the probe's cell in that cascade. The atlases stack the
// cascades downward in the same order.
uint ProbeCascadeProbes()
{
    return gi_gridSize * gi_gridSize * gi_gridSize;
}

uint ProbeCount()
{
    return ProbeCascadeProbes() * gi_cascadeCount;
}

uint ProbeCascade(uint probe)
{
    return probe / ProbeCascadeProbes();
}

uint3 ProbeCell(uint probe)
{
    uint local = probe % ProbeCascadeProbes();
    return uint3(local % gi_gridSize, (local / gi_gridSize) % gi_gridSize, local / (gi_gridSize * gi_gridSize));
}

uint ProbeIndex(uint cascade, uint3 cell)
{
    return cascade * ProbeCascadeProbes() + cell.x + gi_gridSize * (cell.y + gi_gridSize * cell.z);
}

float ProbeLargestSpacing(uint cascade)
{
    float3 spacing = gi_spacing[cascade].xyz;
    return max(spacing.x, max(spacing.y, spacing.z));
}

float3 ProbePosition(uint probe)
{
    uint cascade = ProbeCascade(probe);
    return gi_originSpacing[cascade].xyz + float3(ProbeCell(probe)) * gi_spacing[cascade].xyz;
}

float ProbeMaxDistance(uint cascade)
{
    return 16.0 * ProbeLargestSpacing(cascade);
}

float ProbeNormalBias(uint cascade)
{
    return 0.1 * gi_originSpacing[cascade].w;
}

float3 ProbeOrigin(uint cascade, bool history)
{
    return history ? gi_historyOriginSpacing[cascade].xyz : gi_originSpacing[cascade].xyz;
}

// The finest cascade whose probes surround `position` (none: GI_MAX_CASCADES) and how far, in cells, the point is from
// its boundary. Cascades are nested around the camera, so each coarser one contains the finer ones.
uint ProbeCascadeAt(float3 position, bool history, out float edge)
{
    edge = 0.0;
    for (uint cascade = 0; cascade < gi_cascadeCount; ++cascade)
    {
        float3 grid = (position - ProbeOrigin(cascade, history)) / gi_spacing[cascade].xyz;
        if (all(grid >= 0.0) && all(grid <= float(gi_gridSize - 1)))
        {
            float3 distance = min(grid, float(gi_gridSize - 1) - grid);
            edge = min(distance.x, min(distance.y, distance.z));
            return cascade;
        }
    }
    return GI_MAX_CASCADES;
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

#define PROBE_RADIANCE_SIZE 16

// Where a cascade's far-field rays start (the radiance cache screen probes hand over to): past the probe's
// interpolation footprint, so the cache never shows what lies between the probe and a nearby surface.
float ProbeFarFieldStart(uint cascade)
{
    return 1.75 * ProbeLargestSpacing(cascade);
}

// Hit radiance as the probe averages use it: a 16-bit ray target can hold Inf (or NaN) from very bright hits.
float3 ProbeFiniteRadiance(float3 radiance)
{
    return all(isfinite(radiance)) ? clamp(radiance, 0.0, 10000.0) : 0.0.xxx;
}

// A probe's 16x16 tile in the radiance atlas (8 probes per row, like the 8x8 irradiance tiles).
uint2 ProbeRadianceTile(uint probe)
{
    return uint2(probe % gi_gridSize, probe / gi_gridSize) * PROBE_RADIANCE_SIZE;
}

// Where a probe's history lives after its cascade scrolled with the camera; false when there is none to use.
bool ProbeHistoryIndex(uint probe, out uint oldProbe)
{
    oldProbe = 0;
    if ((gi_reset & 1u) != 0)
        return false;
    uint cascade = ProbeCascade(probe);
    int3 oldCell = int3(ProbeCell(probe)) +
                   int3(round((gi_originSpacing[cascade].xyz - gi_historyOriginSpacing[cascade].xyz) / gi_spacing[cascade].xyz));
    if (any(oldCell < 0) || any(oldCell >= int(gi_gridSize)))
        return false;
    oldProbe = ProbeIndex(cascade, uint3(oldCell));
    return true;
}

float2 ProbeOctEncode(float3 direction)
{
    direction /= dot(abs(direction), 1.0.xxx);
    float2 uv = direction.xy;
    if (direction.z < 0.0)
        uv = (1.0 - abs(uv.yx)) * float2(uv.x >= 0.0 ? 1.0 : -1.0, uv.y >= 0.0 ? 1.0 : -1.0);
    return uv * 0.5 + 0.5;
}

// Unnormalised octahedron point of a [0,1]^2 map position: |p|^3 converts area in [-1,1]^2 to solid angle
// (d omega = dA / |p|^3), so octahedral map texels are not equal-area.
float3 ProbeOctPoint(float2 uv)
{
    float2 xy = uv * 2.0 - 1.0;
    float3 p = float3(xy, 1.0 - abs(xy.x) - abs(xy.y));
    if (p.z < 0.0)
        p.xy = (1.0 - abs(p.yx)) * float2(p.x >= 0.0 ? 1.0 : -1.0, p.y >= 0.0 ? 1.0 : -1.0);
    return p;
}

uint ProbeHash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float ProbeRandom(uint seed)
{
    return float(ProbeHash(seed) >> 8) / 16777216.0;
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
    float2 dimensions = float2(gi_gridSize * 8, gi_gridSize * gi_gridSize * 8 * GI_MAX_CASCADES);
    float2 uv = (tile + ProbeOctEncode(direction) * 6.0 + 1.0) / dimensions;
    return atlas.SampleLevel(gi_sampler, uv, 0);
}

// Irradiance from one cascade's 8 probes around the normal-biased point (trilinear x facing x visibility); a = 1 when
// any probe counted.
float4 ProbeCascadeIrradiance(uint cascade, float3 position, float3 normal, bool history)
{
    float3 origin = ProbeOrigin(cascade, history);
    float3 spacing = gi_spacing[cascade].xyz;
    float3 biased = position + normal * ProbeNormalBias(cascade);
    float3 grid = clamp((biased - origin) / spacing, 0.0, float(gi_gridSize - 1));
    uint3 base = min(uint3(grid), gi_gridSize - 2);
    float3 fraction = saturate(grid - float3(base));
    float3 irradiance = 0.0.xxx;
    float totalWeight = 0.0;
    [unroll] for (uint corner = 0; corner < 8; ++corner)
    {
        uint3 offset = uint3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
        uint3 cell = base + offset;
        uint probe = ProbeIndex(cascade, cell);
        float3 toSurface = biased - (origin + float3(cell) * spacing);
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
    return totalWeight > 0.00001 ? float4(irradiance / totalWeight, 1.0) : 0.0.xxxx;
}

// rgb irradiance; a = how much it replaces environment lighting (0 outside every cascade).
float4 ProbeLighting(float3 position, float3 normal)
{
    if (gi_gridSize < 2 || gi_cascadeCount == 0)
        return 0.0.xxxx;
    bool history = false;
#if defined(GI_PROBES) && !defined(GI_AFTER_UPDATE)
    // Ray feedback reads last frame's atlas; lighting (and passes traced after GI) reads the atlas updated this frame.
    if ((gi_reset & 1u) != 0)
        return 0.0.xxxx;
    history = true;
#endif
    float edge;
    uint cascade = ProbeCascadeAt(position, history, edge);
    if (cascade >= gi_cascadeCount)
        return 0.0.xxxx;
    float4 lit = ProbeCascadeIrradiance(cascade, position, normal, history);
    // Over a cascade's outer cell the next, coarser cascade takes over; the last one fades into environment lighting
    // when the volume follows the camera (a volume fitted to the scene covers every surface).
    float inside = smoothstep(0.0, 1.0, edge);
    if (inside >= 1.0)
        return lit;
    if (cascade + 1 < gi_cascadeCount)
    {
        float4 next = ProbeCascadeIrradiance(cascade + 1, position, normal, history);
        if (lit.a <= 0.0)
            return next;
        return next.a > 0.0 ? float4(lerp(next.rgb, lit.rgb, inside), 1.0) : lit;
    }
    return float4(lit.rgb, gi_spacing[cascade].w > 0.0 ? inside * lit.a : lit.a);
}
#endif
