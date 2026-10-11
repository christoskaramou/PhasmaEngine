#include "../Common/Common.hlsl"
#include "ScreenProbeCommon.hlsl"
[[vk::binding(3, 0)]] Texture2D<float> sp_adaptiveDepth;
[[vk::binding(4, 0)]] Texture2D<float4> sp_adaptiveNormal;
[[vk::image_format("rgba32f")]][[vk::binding(10, 0)]] RWTexture2D<float4> sp_adaptivePositions;
[[vk::image_format("rgba16f")]][[vk::binding(11, 0)]] RWTexture2D<float4> sp_adaptiveNormals;
[[vk::image_format("rgba16f")]][[vk::binding(12, 0)]] RWTexture2D<float4> sp_adaptiveDirections;
[[vk::image_format("rgba16f")]][[vk::binding(13, 0)]] RWTexture2D<float4> sp_adaptiveBinPrior;
[[vk::image_format("r32ui")]][[vk::binding(16, 0)]] RWTexture2D<uint> sp_adaptiveMap;   // per 8x8 sub-tile: slot + 1, 0 = none
[[vk::binding(17, 0)]] RWStructuredBuffer<uint> sp_adaptiveCount; // [0]: slots taken

groupshared uint s_slot;
groupshared float3 s_normal;

// Lumen's adaptive placement: one workgroup per 8x8 sub-tile, after ScreenProbePlace (which clears the map, the
// counter and the adaptive slots). Where the uniform probes cannot stand for the sub-tile's pixel (jittered every
// frame, as the uniform probes are) - their bilinear x ScreenProbeWeight sums under 0.25, where the resolve's
// coverage starts handing the pixel to the world probes - the sub-tile takes a probe of its own in the next free
// slot. No history guides its 64 rays: they are cosine-distributed, one per 8x8 stratum.
// shortcut: one placement level (8 px); Lumen also refines at 4 px, add it if 8 px sub-tiles still leave gaps.
[numthreads(64, 1, 1)]
void main(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID)
{
    const uint2 subTile = group.xy;
    const uint t = thread.x;
    if (t == 0)
    {
        s_slot = 0xFFFFFFFFu;
        const uint jitter = ProbeHash(subTile.x * 73856093u ^ subTile.y * 19349663u ^ sp_frame * 50331653u);
        const uint2 pixel = subTile * 8 + uint2(jitter & 7u, (jitter >> 3) & 7u);
        const float depth = all(pixel < uint2(sp_width, sp_height)) ? sp_adaptiveDepth.Load(int3(pixel, 0)) : 0.0;
        if (depth > 0.0)
        {
            const float3 position = GetPosFromUV((float2(pixel) + 0.5) / float2(sp_width, sp_height), depth, sp_invViewProj);
            const float3 normal = normalize(sp_adaptiveNormal.Load(int3(pixel, 0)).xyz * 2.0 - 1.0);
            const float viewDistance = length(position - sp_camPos.xyz);
            const float2 grid = (float2(pixel) + 0.5) / 16.0 - 0.5;
            const int2 base = int2(floor(grid));
            const float2 fraction = grid - float2(base);
            float total = 0.0;
            [unroll] for (uint corner = 0; corner < 4; ++corner)
            {
                const int2 offset = int2(corner & 1, corner >> 1);
                const float2 bilinear = lerp(1.0 - fraction, fraction, float2(offset));
                const int2 tile = clamp(base + offset, int2(0, 0), int2(sp_tilesX, sp_tilesY) - 1);
                total += bilinear.x * bilinear.y *
                         ScreenProbeWeight(sp_adaptivePositions[uint2(tile)], sp_adaptiveNormals[uint2(tile)].xyz, position, normal, viewDistance);
            }
            uint slot = 0xFFFFFFFFu;
            if (total < 0.25)
                InterlockedAdd(sp_adaptiveCount[0], 1u, slot);
            if (slot < sp_tilesX * sp_adaptiveRows)
            {
                const uint2 tile = uint2(slot % sp_tilesX, sp_tilesY + slot / sp_tilesX);
                sp_adaptivePositions[tile] = float4(position, viewDistance);
                sp_adaptiveNormals[tile] = float4(normal, 0.0);
                sp_adaptiveMap[subTile] = slot + 1;
                s_slot = slot;
                s_normal = normal;
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (s_slot == 0xFFFFFFFFu)
        return;
    const uint2 tile = uint2(s_slot % sp_tilesX, sp_tilesY + s_slot / sp_tilesX);
    const uint2 rayPixel = tile * 8 + uint2(t % 8, t / 8);
    const uint seed = (subTile.x * 7919u + subTile.y * 104729u) * 64u + t + sp_frame * 131071u;
    const float2 u = (float2(t % 8, t / 8) + float2(ProbeRandom(seed * 2u), ProbeRandom(seed * 2u + 1u))) / 8.0;
    const float cosine = sqrt(1.0 - u.x);
    const float radius = sqrt(u.x);
    const float phi = 6.28318531 * u.y;
    // Orthonormal basis around the normal (Duff et al. 2017).
    const float3 n = s_normal;
    const float side = n.z >= 0.0 ? 1.0 : -1.0;
    const float a = -1.0 / (side + n.z);
    const float b = n.x * n.y * a;
    const float3 tangent = float3(1.0 + side * n.x * n.x * a, side * b, -side * n.x);
    const float3 bitangent = float3(b, side + n.y * n.y * a, -n.y);
    const float3 direction = tangent * (radius * cos(phi)) + bitangent * (radius * sin(phi)) + n * cosine;
    sp_adaptiveDirections[rayPixel] = float4(normalize(direction), cosine / 3.14159265);
    sp_adaptiveBinPrior[rayPixel] = 0.0.xxxx;
}
