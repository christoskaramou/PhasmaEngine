#include "../Common/Common.hlsl"

// Native SSAO. Faithful port of the canonical hemisphere-kernel SSAO
// (LearnOpenGL "SSAO", Hammon/Chapman lineage): a FIXED view-space hemisphere
// kernel, oriented to the surface normal and rotated per-pixel by a hash, sampled
// against the depth buffer with a smoothstep range-check to suppress haloing.
// The kernel is generated deterministically per sample index here instead of being
// uploaded from the CPU, and a procedural rotation replaces the 4x4 noise texture.
//
// Perf: the heavy occlusion pass runs at HALF resolution (writes ssaoRaw at half the
// framebuffer size -> a quarter of the invocations), then a depth-aware joint bilateral
// pass upsamples it back to full resolution into ssao. Depth/normal are still sampled
// at full resolution so occlusion stays accurate; the upsample's depth weighting keeps
// silhouettes sharp (no bleed across edges) while denoising the per-pixel rotation.
// Within a sample, the occluder only needs its view-space Z (distance from camera), so
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

#ifndef SSAO_BLUR_PASS
[[vk::binding(0, 0)]] Texture2D<float> Depth : register(t0, space0);
[[vk::binding(1, 0)]] Texture2D<float4> Normal : register(t1, space0);
[[vk::binding(2, 0)]] RWTexture2D<float> RawAO : register(u2, space0);
[[vk::binding(3, 0)]] cbuffer SSAOConstants : register(b3, space0)
{
    SSAOUniforms cb;
};
#else
[[vk::binding(0, 0)]] RWTexture2D<float> BlurInput : register(u0, space0); // half-res ssaoRaw
[[vk::binding(1, 0)]] Texture2D<float> BlurDepth : register(t1, space0);   // full-res depth
[[vk::binding(2, 0)]] RWTexture2D<float> OutputAO : register(u2, space0);  // full-res ssao
[[vk::binding(3, 0)]] cbuffer SSAOBlurConstants : register(b3, space0)
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

#ifndef SSAO_BLUR_PASS
// Deterministic per-index hash -> [0,1)^3. Same kernel for every pixel (a FIXED kernel),
// which is what lets the bilateral upsample resolve the per-pixel rotation into smooth AO.
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
        if (any(sampleUv < 0.0f) || any(sampleUv > 1.0f))
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

#else

float BlurDepthWeight(float centerDistance, float sampleDistance)
{
    float sigma = max(centerDistance * 0.015f, 0.025f);
    float diff = abs(centerDistance - sampleDistance);
    return exp(-(diff * diff) / max(2.0f * sigma * sigma, 1e-5f));
}

// Joint bilateral upsample: each full-res output pixel gathers a 5x5 footprint of the
// half-res ssaoRaw and weights each tap by how close its surface is (in view-space Z) to
// the full-res pixel's surface. Depth-mismatched half-res taps (across a silhouette) get
// near-zero weight, so the upsample stays edge-sharp instead of bleeding AO over edges.
// The 5x5 half-res footprint (~10x10 full-res) keeps enough same-surface taps near a
// contact edge -- where depth weighting rejects the cross-edge half -- to denoise the
// fixed-kernel rotation cleanly, replacing the old full-res blur in one pass.
#define SSAO_UPSAMPLE_RADIUS 2
float BilateralUpsample(uint2 fullPixel)
{
    float centerDepth = BlurDepth.Load(int3(fullPixel, 0));
    if (centerDepth <= 0.0f)
        return 1.0f;

    float2 centerUv = PixelToUv(fullPixel, cb.fullFramebuffer.zw);
    float centerDistance = ViewZAbs(centerUv, centerDepth, cb);

    int2 halfDimsMax = int2(int(cb.framebuffer.x) - 1, int(cb.framebuffer.y) - 1);
    int2 centerHalf = int2(centerUv * cb.framebuffer.xy);

    float weightedAO = 0.0f;
    float weightSum = 0.0f;

    [unroll]
    for (int y = -SSAO_UPSAMPLE_RADIUS; y <= SSAO_UPSAMPLE_RADIUS; ++y)
    {
        [unroll]
        for (int x = -SSAO_UPSAMPLE_RADIUS; x <= SSAO_UPSAMPLE_RADIUS; ++x)
        {
            int2 q = clamp(centerHalf + int2(x, y), int2(0, 0), halfDimsMax);
            float2 sampleUv = PixelToUv(uint2(q), cb.framebuffer.zw);
            int2 sampleFull = int2(sampleUv * cb.fullFramebuffer.xy);
            float sampleDepth = BlurDepth.Load(int3(sampleFull, 0));
            if (sampleDepth <= 0.0f)
                continue;

            float sampleDistance = ViewZAbs(sampleUv, sampleDepth, cb);
            // Spatial Gaussian wide enough to actually use the outer ring of the 5x5
            // (the depth term carries the edge-awareness so cross-edge taps still vanish).
            float spatial = exp(-float(x * x + y * y) * 0.25f);
            float weight = spatial * BlurDepthWeight(centerDistance, sampleDistance);
            weightedAO += BlurInput[uint2(q)] * weight;
            weightSum += weight;
        }
    }

    return weightSum > 0.0f ? saturate(weightedAO / weightSum) : BlurInput[uint2(centerHalf)];
}

[numthreads(8, 8, 1)]
void mainCS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= uint(cb.fullFramebuffer.x) || id.y >= uint(cb.fullFramebuffer.y))
        return;

    OutputAO[id.xy] = BilateralUpsample(id.xy);
}
#endif
