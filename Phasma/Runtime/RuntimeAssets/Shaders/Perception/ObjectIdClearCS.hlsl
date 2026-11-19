// Object-id visibility clear (spatial perception "decode_camera_view", Phase 2c).
// One thread per draw index: resets the NodeVis accumulators before ObjectIdReduceCS folds
// the rasterized id target in. Kept in its own file (not a second entry point in the reduce
// shader) so its descriptor layout is exactly { binding 0 : NodeVis UAV } with no unused
// sampled-texture binding.

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

[[vk::binding(0, 0)]] RWStructuredBuffer<NodeVis> NodeVisOut;

[numthreads(64, 1, 1)] void clearCS(uint3 DTid : SV_DispatchThreadID)
{
    uint i = DTid.x;
    if (i >= pc.drawCount)
        return;

    // Member by member: a whole-struct store compiles to an OpCopyLogical that the WGSL converter cannot lower.
    NodeVisOut[i].pixelCount = 0u;
    NodeVisOut[i].minX = 0xFFFFFFFFu;
    NodeVisOut[i].minY = 0xFFFFFFFFu;
    NodeVisOut[i].maxX = 0u;
    NodeVisOut[i].maxY = 0u;
    NodeVisOut[i].nearestDepthBits = 0u;
}
