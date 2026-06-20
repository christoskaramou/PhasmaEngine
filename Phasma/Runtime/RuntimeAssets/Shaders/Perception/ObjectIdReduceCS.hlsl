// Object-id visibility reduction (spatial perception "decode_camera_view", Phase 2c).
//
// One thread per pixel: reads the per-pixel (drawId+1, ndcDepth) target written by
// ObjectIdPS and atomically folds each visible pixel into its draw's NodeVis accumulator
// (run AFTER ObjectIdClearCS has reset them): visible pixel count (exact coverage),
// screen-space bounding box, and nearest visible depth.
//
// Reverse-Z note: the nearest surface has the LARGEST depth value, so "nearest" is an
// InterlockedMax over the packed depth bits (asuint is monotonic for the [0,1] depth range).

struct NodeVis
{
    uint pixelCount;
    uint minX;
    uint minY;
    uint maxX;
    uint maxY;
    uint nearestDepthBits;
};

struct PushConstants
{
    uint width;
    uint height;
    uint drawCount;
    uint pad0;
};
[[vk::push_constant]] PushConstants pc;

[[vk::binding(0, 0)]] Texture2D<uint2> IdTarget;            // .x = drawId + 1 (0 == empty), .y = asuint(ndcDepth)
[[vk::binding(1, 0)]] RWStructuredBuffer<NodeVis> NodeVisOut;

[numthreads(8, 8, 1)] void reduceCS(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= pc.width || DTid.y >= pc.height)
        return;

    uint2 texel = IdTarget.Load(int3(DTid.xy, 0));
    if (texel.x == 0u)            // 0 == empty (target clears to 0; ids are stored as draw+1)
        return;
    uint id = texel.x - 1u;
    if (id >= pc.drawCount)
        return;

    InterlockedAdd(NodeVisOut[id].pixelCount, 1u);
    InterlockedMin(NodeVisOut[id].minX, DTid.x);
    InterlockedMin(NodeVisOut[id].minY, DTid.y);
    InterlockedMax(NodeVisOut[id].maxX, DTid.x);
    InterlockedMax(NodeVisOut[id].maxY, DTid.y);
    InterlockedMax(NodeVisOut[id].nearestDepthBits, texel.y);
}
