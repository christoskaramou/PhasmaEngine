// Hi-Z depth pyramid build (Phase 2a of the occlusion-culling plan).
//
// Reduces a source level into the next coarser pyramid level by taking the MINIMUM
// over the covered source footprint. The engine uses reverse-Z (near = 1, far = 0),
// so the per-tile minimum is the farthest of the nearest surfaces — the conservative
// occluder depth an occlusion test compares against. Min-over-footprint (rather than a
// fixed 2x2) stays correct for non-power-of-two sizes: a destination texel mins every
// source texel that maps into it, never dropping one. For the 1:1 mip-0 case the
// footprint is a single texel (a straight copy of the depth value).
//
// Compiled twice:
//   PYRAMID_FROM_DEPTH defined -> source is the scene depth buffer (sampled Texture2D),
//                                 producing pyramid mip 0.
//   undefined                  -> source is the previous pyramid level read as a STORAGE
//                                 image (RWTexture2D), producing mip i from mip i-1.
//
// Reading the previous level as a storage image keeps the WHOLE pyramid in one
// UNORDERED_ACCESS / GENERAL state for the entire build; consecutive dispatches are
// ordered with a plain UAV/memory barrier (no per-subresource layout transition). The
// DX12 backend tracks a single whole-resource state and cannot express per-mip layout
// transitions, so the sampled-SRV-per-mip approach is intentionally avoided.

#ifdef PYRAMID_FROM_DEPTH
[[vk::binding(0, 0)]] Texture2D<float> Src;
float LoadSrc(uint2 p) { return Src.Load(int3(int(p.x), int(p.y), 0)); }
#else
[[vk::binding(0, 0)]] RWTexture2D<float> Src;
float LoadSrc(uint2 p) { return Src[p]; }
#endif

[[vk::binding(1, 0)]] RWTexture2D<float> Dst;

struct PushConstants
{
    uint dstWidth;
    uint dstHeight;
    uint srcWidth;
    uint srcHeight;
};
[[vk::push_constant]] PushConstants pc;

[numthreads(8, 8, 1)] void mainCS(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= pc.dstWidth || DTid.y >= pc.dstHeight)
        return;

    // Source footprint [x0,x1) x [y0,y1) covering this destination texel. 2:1 -> 2x2,
    // 1:1 -> 1x1, odd source dimension -> conservative 3-wide span on the boundary.
    uint x0 = (DTid.x * pc.srcWidth) / pc.dstWidth;
    uint y0 = (DTid.y * pc.srcHeight) / pc.dstHeight;
    uint x1 = max(x0 + 1u, ((DTid.x + 1u) * pc.srcWidth) / pc.dstWidth);
    uint y1 = max(y0 + 1u, ((DTid.y + 1u) * pc.srcHeight) / pc.dstHeight);
    x1 = min(x1, pc.srcWidth);
    y1 = min(y1, pc.srcHeight);

    float minDepth = LoadSrc(uint2(x0, y0));
    for (uint y = y0; y < y1; ++y)
        for (uint x = x0; x < x1; ++x)
            minDepth = min(minDepth, LoadSrc(uint2(x, y)));

    Dst[DTid.xy] = minDepth;
}
