#include "../Common/Common.hlsl"

struct PushConstants
{
    uint mipLevel;
    uint faceSize;
    uint sampleCount;
    float roughness;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

[[vk::binding(0)]] RWTexture2DArray<float4> outCubeMips[12] : register(u0);
[[vk::binding(1)]] Texture2D<float4> inEquirectangular : register(t1);
[[vk::binding(2)]] SamplerState samplerLinear : register(s2);

// Firefly suppression. An HDR sun is a sub-texel delta orders of magnitude
// brighter than the sky. With a fixed low-discrepancy sample set (identical per
// texel), that delta lights up isolated texels on a regular lattice, which then
// magnify onto rough surfaces as a grid of bright squares. Capping per-sample
// radiance bounds the peak so the lattice has no high-contrast spike to expose,
// while leaving the sun clearly bright. Tune up for a brighter sun reflection,
// down if any residual squares remain.
#define MAX_PREFILTER_RADIANCE 16.0

// Source-mip bias. The Karis solid-angle estimate under-blurs a delta-bright
// sun, so samples still read a near-point and the per-texel sun-hit count
// varies, magnifying into swimming blotches on rough surfaces. Biasing each
// sample toward a blurrier (pre-averaged) source mip converges the integral to
// a smooth blur with the sample budget we have. Energy-preserving and scaled by
// roughness, so low-roughness reflections stay sharp. Raise if blotches remain,
// lower if rough reflections look over-blurred.
#define SOURCE_MIP_BIAS 8.0

float2 SampleSphericalMap(float3 v)
{
    float2 uv = float2(atan2(v.z, v.x), asin(v.y));
    uv *= float2(0.1591, 0.3183);
    uv += 0.5;
    return float2(uv.x, 1.0 - uv.y);
}

float3 CubemapDirection(uint face, float2 uv)
{
    switch (face)
    {
    case 0:
        return normalize(float3(1.0, -uv.y, -uv.x));
    case 1:
        return normalize(float3(-1.0, -uv.y, uv.x));
    case 2:
        return normalize(float3(uv.x, 1.0, uv.y));
    case 3:
        return normalize(float3(uv.x, -1.0, -uv.y));
    case 4:
        return normalize(float3(uv.x, -uv.y, 1.0));
    default:
        return normalize(float3(-uv.x, -uv.y, -1.0));
    }
}

float RadicalInverseVdc(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float2 Hammersley(uint i, uint sampleCount)
{
    return float2(float(i) / float(sampleCount), RadicalInverseVdc(i));
}

float GGXDistribution(float NoH, float roughness)
{
    float a = max(roughness * roughness, 0.001);
    float a2 = a * a;
    float d = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / max(PI * d * d, FLT_EPSILON);
}

float3 ImportanceSampleGGX(float2 xi, float3 n, float roughness)
{
    float a = max(roughness * roughness, 0.001);
    float a2 = a * a;

    float phi = 2.0 * PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / max(1.0 + (a2 - 1.0) * xi.y, FLT_EPSILON));
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));

    float3 h;
    h.x = cos(phi) * sinTheta;
    h.y = sin(phi) * sinTheta;
    h.z = cosTheta;

    float3 up = abs(n.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, n));
    float3 bitangent = cross(n, tangent);

    return normalize(tangent * h.x + bitangent * h.y + n * h.z);
}

float3 PrefilterEnvironment(float3 r)
{
    uint sourceWidth;
    uint sourceHeight;
    uint sourceLevels;
    inEquirectangular.GetDimensions(0, sourceWidth, sourceHeight, sourceLevels);

    float3 n = r;
    float3 v = r;
    float3 prefilteredColor = 0.0;
    float totalWeight = 0.0;
    float roughness = max(pc.roughness, 0.001);
    uint sampleCount = max(pc.sampleCount, 1u);
    float sourceTexelSolidAngle = 4.0 * PI / max(float(sourceWidth * sourceHeight), 1.0);

    [loop]
    for (uint i = 0; i < sampleCount; ++i)
    {
        float2 xi = Hammersley(i, sampleCount);
        float3 h = ImportanceSampleGGX(xi, n, roughness);
        float3 l = normalize(2.0 * dot(v, h) * h - v);

        float NoL = saturate(dot(n, l));
        if (NoL > 0.0)
        {
            float NoH = saturate(dot(n, h));
            float VoH = saturate(dot(v, h));
            float pdf = GGXDistribution(NoH, roughness) * NoH / max(4.0 * VoH, FLT_EPSILON);
            float sampleSolidAngle = 1.0 / (float(sampleCount) * max(pdf, FLT_EPSILON));
            float sourceMip = roughness <= 0.001 ? 0.0 : max(0.0, 0.5 * log2(sampleSolidAngle / sourceTexelSolidAngle));
            sourceMip += roughness * SOURCE_MIP_BIAS;

            float3 radiance = inEquirectangular.SampleLevel(samplerLinear, SampleSphericalMap(l), sourceMip).rgb;
            radiance = min(radiance, MAX_PREFILTER_RADIANCE);
            prefilteredColor += radiance * NoL;
            totalWeight += NoL;
        }
    }

    if (totalWeight <= 0.0)
        return inEquirectangular.SampleLevel(samplerLinear, SampleSphericalMap(r), 0.0).rgb;

    return prefilteredColor / totalWeight;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= pc.faceSize || id.y >= pc.faceSize || id.z >= 6 || pc.mipLevel >= 12)
        return;

    float2 uv = (float2(id.xy) + 0.5) / float2(pc.faceSize, pc.faceSize);
    uv = uv * 2.0 - 1.0;

    float3 dir = CubemapDirection(id.z, uv);
    outCubeMips[pc.mipLevel][id] = float4(PrefilterEnvironment(dir), 1.0);
}
