#include "DOF.hlsl"

// Each tile's reach: the largest blur radius among the tiles within the blur cap, every pixel that can spread over it.
[[vk::image_format("r32f")]][[vk::binding(0, 0)]] RWTexture2D<float> Tiles;
[[vk::image_format("r32f")]][[vk::binding(1, 0)]] RWTexture2D<float> Reach;

[[vk::push_constant]] ConstantBuffer<DOFParams> pc;

[numthreads(8, 8, 1)] void mainCS(uint3 tile : SV_DispatchThreadID)
{
    uint width, height;
    Tiles.GetDimensions(width, height);
    if (tile.x >= width || tile.y >= height)
        return;

    const int tiles = int(ceil(pc.maxRadius / DOF_TILE));
    const int2 last = int2(width, height) - 1;
    float radius = 0.0;
    for (int y = -tiles; y <= tiles; ++y)
    {
        for (int x = -tiles; x <= tiles; ++x)
        {
            const int2 neighbour = clamp(int2(tile.xy) + int2(x, y), int2(0, 0), last);
            radius = max(radius, Tiles[neighbour]);
        }
    }

    Reach[tile.xy] = radius;
}
