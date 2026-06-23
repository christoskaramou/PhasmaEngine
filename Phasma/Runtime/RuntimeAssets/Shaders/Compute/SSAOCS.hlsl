#include "../Common/Common.hlsl"

// Native SSAO. Faithful port of the canonical hemisphere-kernel SSAO
// (LearnOpenGL "SSAO", Hammon/Chapman lineage): a FIXED view-space hemisphere
// kernel, oriented to the surface normal and rotated per-pixel by a hash, sampled
// against the depth buffer with a smoothstep range-check to suppress haloing.
// The kernel is generated deterministically per sample index here instead of being
// uploaded from the CPU, and a procedural rotation replaces the 4x4 noise texture.

struct SSAOUniforms
{
    float4x4 projection;
    float4x4 invProjection;
    float4x4 normalsToView;
    float4 framebuffer; // xy = size, zw = 1/size
    float4 params;      // x = radius, y = bias, z = intensity, w = power
};

static const uint SSAO_SAMPLE_COUNT = 32;

#ifndef SSAO_BLUR_PASS
[[vk::binding(0, 0)]] Texture2D<float> Depth : register(t0, space0);
[[vk::binding(1, 0)]] Texture2D<float4> Normal : register(t1, space0);
[[vk::binding(2, 0)]] RWTexture2D<float> RawAO : register(u2, space0);
[[vk::binding(3, 0)]] cbuffer SSAOConstants : register(b3, space0)
{
    SSAOUniforms cb;
};
#else
[[vk::binding(0, 0)]] RWTexture2D<float> BlurInput : register(u0, space0);
[[vk::binding(1, 0)]] Texture2D<float> BlurDepth : register(t1, space0);
[[vk::binding(2, 0)]] RWTexture2D<float> OutputAO : register(u2, space0);
[[vk::binding(3, 0)]] cbuffer SSAOBlurConstants : register(b3, space0)
{
    SSAOUniforms cb;
};
#endif

float2 PixelToUv(uint2 pixel, SSAOUniforms constants)
{
    return (float2(pixel) + 0.5f) * constants.framebuffer.zw;
}

float3 ViewPosFromDepth(float2 uv, float depth, SSAOUniforms constants)
{
    return GetPosFromUV(uv, depth, constants.invProjection);
}

// View-space distance from camera, sign-agnostic (works whether the view axis is +z or -z).
float ViewDistanceFromDepth(float2 uv, float depth, SSAOUniforms constants)
{
    return abs(ViewPosFromDepth(uv, depth, constants).z);
}

#ifndef SSAO_BLUR_PASS
// Deterministic per-index hash -> [0,1)^3. Same kernel for every pixel (a FIXED kernel),
// which is what lets the bilateral blur resolve the per-pixel rotation into smooth AO.
float3 HashKernel(uint i)
{
    float n = float(i) + 1.0f;
    float3 p = frac(float3(n * 0.1031f, n * 0.1030f, n * 0.0973f));
    p += dot(p, p.yxz + 33.33f);
    return frac((p.xxy + p.yzz) * p.zyx);
}

// Per-pixel hash -> [0,1)^2, used to rotate the kernel about the surface normal.
float2 HashPixel(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.xx + p3.yz) * p3.zy);
}

// Reference kernel sample i: a hemisphere direction (z>=0 = along the normal) whose
// length is biased toward the surface so AO responds to nearby geometry only.
float3 KernelSample(uint i)
{
    float3 r = HashKernel(i);
    float3 dir = normalize(float3(r.xy * 2.0f - 1.0f, r.z + 0.05f));
    float scale = float(i) / float(SSAO_SAMPLE_COUNT);
    scale = lerp(0.1f, 1.0f, scale * scale); // accelerating interpolation -> cluster near surface
    return dir * scale * (0.4f + 0.6f * r.z);
}

float ComputeAO(uint2 pixel)
{
    // Reverse-Z: depth == 0 is the far plane (sky / no geometry) -> fully lit.
    float depth = Depth.Load(int3(pixel, 0));
    if (depth <= 0.0f)
        return 1.0f;

    float2 uv = PixelToUv(pixel, cb);
    float3 viewPos = ViewPosFromDepth(uv, depth, cb);
    float3 storedNormal = Normal.Load(int3(pixel, 0)).xyz * 2.0f - 1.0f;
    float3 N = normalize(mul(float4(storedNormal, 0.0f), cb.normalsToView).xyz);

    float radius = cb.params.x;
    float bias = cb.params.y;
    float intensity = cb.params.z;
    float power = cb.params.w;

    float centerDist = max(abs(viewPos.z), FLT_EPSILON);

    // Per-pixel tangent basis: rotate the fixed kernel about the normal (Gram-Schmidt).
    float ang = HashPixel(float2(pixel)).x * (2.0f * PI);
    float3 randomVec = float3(cos(ang), sin(ang), 0.0f);
    float3 tangent = normalize(randomVec - N * dot(randomVec, N) + float3(0.0f, 0.0f, FLT_EPSILON));
    float3 bitangent = cross(N, tangent);
    float3x3 TBN = float3x3(tangent, bitangent, N);

    float occlusion = 0.0f;

    [unroll]
    for (uint i = 0; i < SSAO_SAMPLE_COUNT; ++i)
    {
        // View-space sample position in the hemisphere above the surface.
        float3 sampleView = viewPos + mul(KernelSample(i), TBN) * radius;

        // Project to screen UV.
        float4 clip = mul(float4(sampleView, 1.0f), cb.projection);
        if (clip.w <= FLT_EPSILON)
            continue;
        float2 sampleUv = NdcToUv(clip.xy / clip.w);
        if (any(sampleUv < 0.0f) || any(sampleUv > 1.0f))
            continue;

        int2 samplePixel = int2(sampleUv * cb.framebuffer.xy);
        float sampleDepth = Depth.Load(int3(samplePixel, 0));
        if (sampleDepth <= 0.0f)
            continue;

        // Distance (from camera) of the real surface seen at this screen location.
        float sceneDist = abs(ViewPosFromDepth(sampleUv, sampleDepth, cb).z);
        float sampleDist = abs(sampleView.z);

        // Occluded when the real surface sits in front of the sample (closer to camera).
        float occluded = (sceneDist <= sampleDist - bias) ? 1.0f : 0.0f;

        // Reject occluders whose depth is far from the shaded point -> kills silhouette halos.
        float rangeCheck = smoothstep(0.0f, 1.0f, radius / max(abs(centerDist - sceneDist), FLT_EPSILON));
        occlusion += occluded * rangeCheck;
    }

    float ao = 1.0f - (occlusion / float(SSAO_SAMPLE_COUNT)) * intensity;
    return pow(saturate(ao), max(power, 0.001f));
}

[numthreads(8, 8, 1)]
void mainCS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= uint(cb.framebuffer.x) || id.y >= uint(cb.framebuffer.y))
        return;

    RawAO[id.xy] = ComputeAO(id.xy);
}

#else

uint2 ClampBlurPixel(int2 p)
{
    return uint2(clamp(p, int2(0, 0), int2(int(cb.framebuffer.x) - 1, int(cb.framebuffer.y) - 1)));
}

float BlurSpatialWeight(int2 offset)
{
    float d2 = dot(float2(offset), float2(offset));
    return exp(-d2 * 0.35f);
}

float BlurDepthWeight(float centerDistance, float sampleDistance)
{
    float sigma = max(centerDistance * 0.015f, 0.025f);
    float diff = abs(centerDistance - sampleDistance);
    return exp(-(diff * diff) / max(2.0f * sigma * sigma, 1e-5f));
}

float BilateralBlur(uint2 pixel)
{
    float centerDepth = BlurDepth.Load(int3(pixel, 0));
    if (centerDepth <= 0.0f)
        return 1.0f;

    float2 centerUv = PixelToUv(pixel, cb);
    float centerDistance = ViewDistanceFromDepth(centerUv, centerDepth, cb);
    float weightedAO = 0.0f;
    float weightSum = 0.0f;

    [unroll]
    for (int y = -2; y <= 2; ++y)
    {
        [unroll]
        for (int x = -2; x <= 2; ++x)
        {
            int2 offset = int2(x, y);
            uint2 samplePixel = ClampBlurPixel(int2(pixel) + offset);
            float sampleDepth = BlurDepth.Load(int3(samplePixel, 0));
            if (sampleDepth <= 0.0f)
                continue;

            float2 sampleUv = PixelToUv(samplePixel, cb);
            float sampleDistance = ViewDistanceFromDepth(sampleUv, sampleDepth, cb);
            float weight = BlurSpatialWeight(offset) * BlurDepthWeight(centerDistance, sampleDistance);
            weightedAO += BlurInput[samplePixel] * weight;
            weightSum += weight;
        }
    }

    return weightSum > 0.0f ? saturate(weightedAO / weightSum) : BlurInput[pixel];
}

[numthreads(8, 8, 1)]
void mainCS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= uint(cb.framebuffer.x) || id.y >= uint(cb.framebuffer.y))
        return;

    OutputAO[id.xy] = BilateralBlur(id.xy);
}
#endif
