#include "DOF.hlsl"

// One group per tile: packs each pixel's colour with its inverse depth, so a gather tap is one fetch, and writes the
// tile's largest blur radius.
[[vk::binding(0, 0)]] Texture2D<float4> Frame;
[[vk::binding(1, 0)]] Texture2D<float> Depth;
[[vk::binding(2, 0)]] Texture2D<float> TransparentDepth;
[[vk::image_format("rgba16f")]][[vk::binding(3, 0)]] RWTexture2D<float4> Packed;
[[vk::image_format("r32f")]][[vk::binding(4, 0)]] RWTexture2D<float> Tiles;

[[vk::push_constant]] ConstantBuffer<DOFParams> pc;

groupshared uint g_tileRadius;

// Depth can be at the render scale while the frame is at the display's size.
int2 DepthTexel(Texture2D<float> tex, uint2 pixel, float2 frameSize)
{
    uint width, height;
    tex.GetDimensions(width, height);
    return int2((float2(pixel) + 0.5) * float2(width, height) / frameSize);
}

// The perspective projection is infinite reverse-Z (d = near / z), so d / near is the inverse view depth.
float InverseDepth(uint2 pixel, float2 frameSize)
{
    float d = Depth.Load(int3(DepthTexel(Depth, pixel, frameSize), 0));

    // A transparent surface that hides what is behind it writes no depth: the forward pass leaves it here.
    if ((pc.flags & DOF_TRANSPARENT_DEPTH) != 0u)
        d = max(d, TransparentDepth.Load(int3(DepthTexel(TransparentDepth, pixel, frameSize), 0)));

    return d / pc.nearPlane;
}

[numthreads(DOF_TILE, DOF_TILE, 1)] void mainCS(uint3 pixel : SV_DispatchThreadID,
                                                uint3 tile : SV_GroupID,
                                                uint index : SV_GroupIndex)
{
    if (index == 0)
        g_tileRadius = 0;

    GroupMemoryBarrierWithGroupSync();

    uint width, height;
    Packed.GetDimensions(width, height);
    const float2 frameSize = float2(width, height);

    if (pixel.x < width && pixel.y < height)
    {
        const float inverseDepth = InverseDepth(pixel.xy, frameSize);
        Packed[pixel.xy] = float4(Frame.Load(int3(pixel.xy, 0)).rgb, inverseDepth);

        if ((pc.flags & DOF_PERSPECTIVE) != 0u)
        {
            const float inverseFocus = InverseFocus(InverseDepth(uint2(width, height) / 2, frameSize), pc);
            const float radius = BlurRadius(inverseDepth, inverseFocus, BlurPerInverseDepth(inverseFocus, pc), pc);
            InterlockedMax(g_tileRadius, asuint(radius)); // non-negative floats order like their bits
        }
    }

    GroupMemoryBarrierWithGroupSync();

    if (index == 0)
        Tiles[tile.xy] = asfloat(g_tileRadius);
}
