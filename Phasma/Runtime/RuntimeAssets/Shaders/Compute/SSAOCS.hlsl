#include "../Common/Common.hlsl"

// Native SSAO. Faithful port of the canonical hemisphere-kernel SSAO
// (LearnOpenGL "SSAO", Hammon/Chapman lineage): a FIXED view-space hemisphere
// kernel, oriented to the surface normal and rotated per-pixel by a hash, sampled
// against the depth buffer with a smoothstep range-check to suppress haloing.
// The kernel is generated deterministically per sample index here instead of being
// uploaded from the CPU, and a procedural rotation replaces the 4x4 noise texture.
//
// Perf: three compute passes, structured like FidelityFX CACAO's medium path so the only
// full-resolution work is a cheap apply.
//   1. Occlusion (this file, no define)   -> ssaoRaw   at HALF res (the heavy 16-tap pass)
//   2. SSAO_BLUR_PASS (bilateral denoise) -> ssaoBlur  at HALF res (smooths kernel rotation)
//   3. SSAO_UPSAMPLE_PASS (joint bilateral upsample) -> ssao at FULL res (4-tap apply)
// Depth/normal are sampled at full resolution throughout so occlusion stays accurate; the
// depth weighting in passes 2 and 3 keeps silhouettes sharp (no bleed across edges).
// Within a sample the occluder only needs its view-space Z (distance from camera), so
// ViewZAbs() recovers it with two dot products instead of a full inverse-projection
// matrix-vector reconstruct. Sample count is a uniform so the editor can trade quality
// for speed live.

struct SSAOUniforms
{
    float4x4 projection;
    float4x4 invProjection;
    float4x4 normalsToView;
    float4 framebuffer;     // AO working (half) res: xy = size, zw = 1/size
    float4 fullFramebuffer; // full res (depth/normal/output): xy = size, zw = 1/size
    float4 params;          // x = radius, y = bias, z = intensity, w = power
    float4 misc;            // x = sample count
};

#if !defined(SSAO_BLUR_PASS) && !defined(SSAO_UPSAMPLE_PASS)
// Pass 1: occlusion. Depth/Normal at full res, write half-res raw AO.
[[vk::binding(0, 0)]] Texture2D<float> Depth : register(t0, space0);
[[vk::binding(1, 0)]] Texture2D<float4> Normal : register(t1, space0);
[[vk::binding(2, 0)]] RWTexture2D<float> RawAO : register(u2, space0);
[[vk::binding(3, 0)]] cbuffer SSAOConstants : register(b3, space0)
{
    SSAOUniforms cb;
};
#else
// Passes 2/3 share a layout: half-res AO in (u0), full-res depth (t1), AO out (u2).
[[vk::binding(0, 0)]] RWTexture2D<float> FilterInput : register(u0, space0);
[[vk::binding(1, 0)]] Texture2D<float> FilterDepth : register(t1, space0);
[[vk::binding(2, 0)]] RWTexture2D<float> FilterOutput : register(u2, space0);
[[vk::binding(3, 0)]] cbuffer SSAOFilterConstants : register(b3, space0)
{
    SSAOUniforms cb;
};
#endif

float2 PixelToUv(uint2 pixel, float2 invSize)
{
    return (float2(pixel) + 0.5f) * invSize;
}

float3 ViewPosFromDepth(float2 uv, float depth, SSAOUniforms constants)
{
    return GetPosFromUV(uv, depth, constants.invProjection);
}

// View-space distance from camera (|z|), sign-agnostic. Recovers only the z/w of the
// inverse-projection transform (two dot products) rather than the full xyz reconstruct.
float ViewZAbs(float2 uv, float depth, SSAOUniforms constants)
{
    float4 ndc = float4(UvToNdc(uv), depth, 1.0f);
    float4 colZ = float4(constants.invProjection._m02, constants.invProjection._m12,
                         constants.invProjection._m22, constants.invProjection._m32);
    float4 colW = float4(constants.invProjection._m03, constants.invProjection._m13,
                         constants.invProjection._m23, constants.invProjection._m33);
    float z = dot(ndc, colZ);
    float w = dot(ndc, colW);
    if (abs(w) < FLT_EPSILON)
        w = FLT_EPSILON;
    return abs(z / w);
}

float BlurDepthWeight(float centerDistance, float sampleDistance)
{
    float sigma = max(centerDistance * 0.015f, 0.025f);
    float diff = abs(centerDistance - sampleDistance);
    return exp(-(diff * diff) / max(2.0f * sigma * sigma, 1e-5f));
}

#if !defined(SSAO_BLUR_PASS) && !defined(SSAO_UPSAMPLE_PASS)
// Deterministic per-index hash -> [0,1)^3. Same kernel for every pixel (a FIXED kernel),
// which is what lets the bilateral denoise resolve the per-pixel rotation into smooth AO.
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
float3 KernelSample(uint i, uint sampleCount)
{
    float3 r = HashKernel(i);
    float3 dir = normalize(float3(r.xy * 2.0f - 1.0f, r.z + 0.05f));
    float scale = float(i) / float(sampleCount);
    scale = lerp(0.1f, 1.0f, scale * scale); // accelerating interpolation -> cluster near surface
    return dir * scale * (0.4f + 0.6f * r.z);
}

// halfPixel is a coordinate in the half-res ssaoRaw target; depth/normal are read at full res.
float ComputeAO(uint2 halfPixel)
{
    float2 uv = PixelToUv(halfPixel, cb.framebuffer.zw);
    int2 fullPixel = int2(uv * cb.fullFramebuffer.xy);

    // Reverse-Z: depth == 0 is the far plane (sky / no geometry) -> fully lit.
    float depth = Depth.Load(int3(fullPixel, 0));
    if (depth <= 0.0f)
        return 1.0f;

    float3 viewPos = ViewPosFromDepth(uv, depth, cb);
    float3 storedNormal = Normal.Load(int3(fullPixel, 0)).xyz * 2.0f - 1.0f;
    float3 N = normalize(mul(float4(storedNormal, 0.0f), cb.normalsToView).xyz);

    float radius = cb.params.x;
    float bias = cb.params.y;
    float intensity = cb.params.z;
    float power = cb.params.w;
    uint sampleCount = max(1u, uint(cb.misc.x));

    float centerDist = max(abs(viewPos.z), FLT_EPSILON);

    // Per-pixel tangent basis: rotate the fixed kernel about the normal (Gram-Schmidt).
    float ang = HashPixel(float2(fullPixel)).x * (2.0f * PI);
    float3 randomVec = float3(cos(ang), sin(ang), 0.0f);
    float3 tangent = normalize(randomVec - N * dot(randomVec, N) + float3(0.0f, 0.0f, FLT_EPSILON));
    float3 bitangent = cross(N, tangent);
    float3x3 TBN = float3x3(tangent, bitangent, N);

    float occlusion = 0.0f;

    [loop]
    for (uint i = 0; i < sampleCount; ++i)
    {
        // View-space sample position in the hemisphere above the surface.
        float3 sampleView = viewPos + mul(KernelSample(i, sampleCount), TBN) * radius;

        // Project to screen UV.
        float4 clip = mul(float4(sampleView, 1.0f), cb.projection);
        if (clip.w <= FLT_EPSILON)
            continue;
        float2 sampleUv = NdcToUv(clip.xy / clip.w);
        // Reject >= 1 as well as < 0: uv == 1 would map to fullFramebuffer.xy (one past the edge).
        if (any(sampleUv < 0.0f) || any(sampleUv >= 1.0f))
            continue;

        int2 samplePixel = int2(sampleUv * cb.fullFramebuffer.xy);
        float sampleDepth = Depth.Load(int3(samplePixel, 0));
        if (sampleDepth <= 0.0f)
            continue;

        // Distance (from camera) of the real surface seen at this screen location vs. our sample.
        float sceneDist = ViewZAbs(sampleUv, sampleDepth, cb);
        float sampleDist = abs(sampleView.z);

        // Occluded when the real surface sits in front of the sample (closer to camera).
        float occluded = (sceneDist <= sampleDist - bias) ? 1.0f : 0.0f;

        // Reject occluders whose depth is far from the shaded point -> kills silhouette halos.
        float rangeCheck = smoothstep(0.0f, 1.0f, radius / max(abs(centerDist - sceneDist), FLT_EPSILON));
        occlusion += occluded * rangeCheck;
    }

    float ao = 1.0f - (occlusion / float(sampleCount)) * intensity;
    return pow(saturate(ao), max(power, 0.001f));
}

[numthreads(8, 8, 1)]
void mainCS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= uint(cb.framebuffer.x) || id.y >= uint(cb.framebuffer.y))
        return;

    RawAO[id.xy] = ComputeAO(id.xy);
}

#elif defined(SSAO_BLUR_PASS)

// Pass 2: depth-aware denoise at HALF res. Smooths the fixed-kernel rotation while
// preserving depth edges, so the full-res apply (pass 3) can be a cheap few taps. The 5x5
// half-res footprint runs at a quarter of the pixels -- this is the work CACAO did sparsely
// at its downsampled SSAO size rather than at full output resolution.
float BilateralDenoiseHalf(uint2 halfPixel)
{
    float2 uv = PixelToUv(halfPixel, cb.framebuffer.zw);
    int2 fullPixel = int2(uv * cb.fullFramebuffer.xy);
    float centerDepth = FilterDepth.Load(int3(fullPixel, 0));
    if (centerDepth <= 0.0f)
        return 1.0f;

    float centerDistance = ViewZAbs(uv, centerDepth, cb);
    int2 halfDimsMax = int2(int(cb.framebuffer.x) - 1, int(cb.framebuffer.y) - 1);

    float weightedAO = 0.0f;
    float weightSum = 0.0f;

    [unroll]
    for (int y = -2; y <= 2; ++y)
    {
        [unroll]
        for (int x = -2; x <= 2; ++x)
        {
            int2 q = clamp(int2(halfPixel) + int2(x, y), int2(0, 0), halfDimsMax);
            float2 sampleUv = PixelToUv(uint2(q), cb.framebuffer.zw);
            int2 sampleFull = int2(sampleUv * cb.fullFramebuffer.xy);
            float sampleDepth = FilterDepth.Load(int3(sampleFull, 0));
            if (sampleDepth <= 0.0f)
                continue;

            float sampleDistance = ViewZAbs(sampleUv, sampleDepth, cb);
            float spatial = exp(-float(x * x + y * y) * 0.25f);
            float weight = spatial * BlurDepthWeight(centerDistance, sampleDistance);
            weightedAO += FilterInput[uint2(q)] * weight;
            weightSum += weight;
        }
    }

    return weightSum > 0.0f ? saturate(weightedAO / weightSum) : FilterInput[halfPixel];
}

[numthreads(8, 8, 1)]
void mainCS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= uint(cb.framebuffer.x) || id.y >= uint(cb.framebuffer.y))
        return;

    FilterOutput[id.xy] = BilateralDenoiseHalf(id.xy);
}

#else // SSAO_UPSAMPLE_PASS

// Pass 3: joint bilateral upsample of the denoised half-res AO to full res. Only a 2x2
// bilinear footprint is needed because the half-res buffer is already smooth; the depth
// weight against full-res depth rejects the cross-edge corners so silhouettes stay sharp.
float JointBilateralUpsample(uint2 fullPixel)
{
    float centerDepth = FilterDepth.Load(int3(fullPixel, 0));
    if (centerDepth <= 0.0f)
        return 1.0f;

    float2 uv = PixelToUv(fullPixel, cb.fullFramebuffer.zw);
    float centerDistance = ViewZAbs(uv, centerDepth, cb);

    int2 halfDimsMax = int2(int(cb.framebuffer.x) - 1, int(cb.framebuffer.y) - 1);
    float2 f = uv * cb.framebuffer.xy - 0.5f;
    int2 base = int2(floor(f));
    float2 frac2 = f - float2(base);

    float weightedAO = 0.0f;
    float weightSum = 0.0f;

    [unroll]
    for (int y = 0; y <= 1; ++y)
    {
        [unroll]
        for (int x = 0; x <= 1; ++x)
        {
            int2 q = clamp(base + int2(x, y), int2(0, 0), halfDimsMax);
            float bilinear = (x == 0 ? 1.0f - frac2.x : frac2.x) * (y == 0 ? 1.0f - frac2.y : frac2.y);
            float2 sampleUv = PixelToUv(uint2(q), cb.framebuffer.zw);
            int2 sampleFull = int2(sampleUv * cb.fullFramebuffer.xy);
            float sampleDepth = FilterDepth.Load(int3(sampleFull, 0));
            if (sampleDepth <= 0.0f)
                continue;

            float sampleDistance = ViewZAbs(sampleUv, sampleDepth, cb);
            float weight = bilinear * BlurDepthWeight(centerDistance, sampleDistance);
            weightedAO += FilterInput[uint2(q)] * weight;
            weightSum += weight;
        }
    }

    // All four corners rejected (a thin sliver whose half-res footprint is all other-surface):
    // fall back to the nearest half-res texel rather than leaking a wrong-surface average.
    int2 nearest = clamp(int2(uv * cb.framebuffer.xy), int2(0, 0), halfDimsMax);
    return weightSum > 0.0f ? saturate(weightedAO / weightSum) : FilterInput[uint2(nearest)];
}

[numthreads(8, 8, 1)]
void mainCS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= uint(cb.fullFramebuffer.x) || id.y >= uint(cb.fullFramebuffer.y))
        return;

    FilterOutput[id.xy] = JointBilateralUpsample(id.xy);
}
#endif
