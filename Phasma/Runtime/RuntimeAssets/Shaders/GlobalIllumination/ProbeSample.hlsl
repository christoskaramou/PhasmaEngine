#include "../Common/Common.hlsl"
#include "ProbeVolume.hlsl"
[[vk::image_format("rgba32f")]][[vk::binding(4, 0)]] RWTexture2D<float4> rayDirections;
[[vk::binding(5, 0)]] Texture2D<float4> radianceHistory;

groupshared float s_flux[256];
groupshared float s_area[256];
groupshared float s_cdf[256];

// Unnormalised octahedron point of a [0,1]^2 map position: |p|^3 converts area in [-1,1]^2 to solid angle
// (d omega = dA / |p|^3), so the 16x16 map's texels are not equal-area.
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

void InclusiveScan(uint t, inout float value, uint which)
{
    for (uint offset = 1; offset < 256; offset <<= 1)
    {
        float add = 0.0;
        if (t >= offset)
            add = which == 0 ? s_flux[t - offset] : (which == 1 ? s_area[t - offset] : s_cdf[t - offset]);
        GroupMemoryBarrierWithGroupSync();
        value += add;
        if (which == 0)
            s_flux[t] = value;
        else if (which == 1)
            s_area[t] = value;
        else
            s_cdf[t] = value;
        GroupMemoryBarrierWithGroupSync();
    }
}

// One workgroup per probe, one thread per ray (and per radiance texel). Rays are importance-sampled from last
// frame's radiance cache, as Lumen's probes do: a texel is chosen with probability 0.75 x its share of the probe's
// incoming flux + 0.25 x its share of the sphere, so every direction keeps a non-zero pdf. Rays are stratified over
// that distribution and jittered inside their texel; the pdf over the sphere goes to ProbeUpdate, which weights
// every ray by 1/pdf so the irradiance estimate stays unbiased. Without history the distribution is uniform.
[numthreads(256, 1, 1)]
void main(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID)
{
    const uint probe = group.x;
    const uint t = thread.x;
    if (probe >= gi_gridSize * gi_gridSize * gi_gridSize)
        return;

    const uint2 texel = uint2(t % PROBE_RADIANCE_SIZE, t / PROBE_RADIANCE_SIZE);
    const float texelArea = 4.0 / float(PROBE_RADIANCE_SIZE * PROBE_RADIANCE_SIZE); // in [-1,1]^2
    float area = texelArea / pow(length(ProbeOctPoint((float2(texel) + 0.5) / PROBE_RADIANCE_SIZE)), 3.0);
    float flux = 0.0;
    uint oldProbe;
    if (ProbeHistoryIndex(probe, oldProbe))
    {
        float4 history = radianceHistory.Load(int3(ProbeRadianceTile(oldProbe) + texel, 0));
        flux = history.a > 0.0 ? dot(history.rgb, float3(0.2126, 0.7152, 0.0722)) * area : 0.0;
    }
    const float ownFlux = flux;
    const float ownArea = area;
    s_flux[t] = flux;
    s_area[t] = area;
    GroupMemoryBarrierWithGroupSync();
    InclusiveScan(t, flux, 0);
    InclusiveScan(t, area, 1);
    const float totalFlux = s_flux[255];
    const float totalArea = s_area[255];
    float weight = totalFlux > 0.0 ? 0.75 * ownFlux / totalFlux + 0.25 * ownArea / totalArea : ownArea / totalArea;
    s_cdf[t] = weight;
    GroupMemoryBarrierWithGroupSync();
    InclusiveScan(t, weight, 2);
    const float total = s_cdf[255];

    if (t >= gi_rayCount)
        return;
    // Stratified: ray t takes the t-th of gi_rayCount equal slices of the distribution, all shifted by one offset.
    const uint seed = probe * 256u + t + gi_frame * 131071u;
    const float target = (float(t) + ProbeRandom(probe * 9781u + gi_frame * 6271u)) / float(gi_rayCount) * total;
    uint low = 0;
    uint high = 255;
    while (low < high)
    {
        uint middle = (low + high) / 2;
        if (s_cdf[middle] > target)
            high = middle;
        else
            low = middle + 1;
    }
    const float probability = (s_cdf[low] - (low > 0 ? s_cdf[low - 1] : 0.0)) / total;
    const uint2 chosen = uint2(low % PROBE_RADIANCE_SIZE, low / PROBE_RADIANCE_SIZE);
    const float2 uv = (float2(chosen) + float2(ProbeRandom(seed * 2u), ProbeRandom(seed * 2u + 1u))) / PROBE_RADIANCE_SIZE;
    const float3 p = ProbeOctPoint(uv);
    // pdf over the sphere = texel probability / texel area in [-1,1]^2 x |p|^3.
    const float pdf = probability / texelArea * pow(length(p), 3.0);
    rayDirections[uint2(t, probe)] = float4(normalize(p), pdf);
}
