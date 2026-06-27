#include "../Common/Structures.hlsl"

struct DrawIndexedIndirectCommand
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};

[[vk::binding(0, 0)]] StructuredBuffer<DrawIndexedIndirectCommand> IndirectCommandsIn;
[[vk::binding(1, 0)]] StructuredBuffer<Mesh_Constants> MeshConstants;
[[vk::binding(2, 0)]] RWStructuredBuffer<uint> Counters; // [opaqueSS, alphaCutSS, alphaBlend, transmission, selected, opaqueDS, alphaCutDS, voxels]
[[vk::binding(3, 0)]] RWStructuredBuffer<DrawIndexedIndirectCommand> IndirectOpaqueSS;
[[vk::binding(4, 0)]] RWStructuredBuffer<DrawIndexedIndirectCommand> IndirectAlphaCutSS;
[[vk::binding(5, 0)]] RWStructuredBuffer<DrawIndexedIndirectCommand> IndirectAlphaBlendOut;
[[vk::binding(6, 0)]] RWStructuredBuffer<DrawIndexedIndirectCommand> IndirectTransmissionOut;
[[vk::binding(7, 0)]] RWStructuredBuffer<DrawIndexedIndirectCommand> IndirectSelectedOut;
[[vk::binding(8, 0)]] RWStructuredBuffer<DrawIndexedIndirectCommand> IndirectOpaqueDS;
[[vk::binding(9, 0)]] RWStructuredBuffer<DrawIndexedIndirectCommand> IndirectAlphaCutDS;
[[vk::binding(10, 0)]] RWStructuredBuffer<float> SortKeysAlphaBlend;
[[vk::binding(11, 0)]] RWStructuredBuffer<float> SortKeysTransmission;
[[vk::binding(12, 0)]] ByteAddressBuffer NodeData;
#if !defined(PHASE1) && !defined(PHASE2)
[[vk::binding(17, 0)]] RWStructuredBuffer<DrawIndexedIndirectCommand> IndirectVoxelsOut;
#endif

struct PushConstants
{
    uint maxDrawCount;
    uint enableFrustumCulling;
    float cameraPositionX;
    float cameraPositionY;
    float cameraPositionZ;
    uint enableVoxelOcclusion; // VOXEL_HIZ: 1 when a voxel Hi-Z pyramid (binding 13/14) is bound
    float4 frustumPlanes[6];
};
[[vk::push_constant]] PushConstants pc;

// LOD params (Scene::UpdateLodUniforms). Present in every cull variant (push constants are full at 128B,
// so the global LOD knobs live here). lodDistances.x/y/z are the world-unit switch points LOD0->1/1->2/2->3.
[[vk::binding(16, 0)]] cbuffer LodUBO
{
    uint lodEnabled;
    uint lodPad0;
    float lodBias;
    float lodPad1;
    float4 lodDistances;
};

float4x4 LoadMatrix(uint byteOffset)
{
    float4x4 result;
    result[0] = asfloat(NodeData.Load4(byteOffset + 0 * 16));
    result[1] = asfloat(NodeData.Load4(byteOffset + 1 * 16));
    result[2] = asfloat(NodeData.Load4(byteOffset + 2 * 16));
    result[3] = asfloat(NodeData.Load4(byteOffset + 3 * 16));
    return result;
}

void TransformAABB(float3 localMin, float3 localMax, float4x4 worldMatrix,
                   out float3 worldMin, out float3 worldMax)
{
    float3 translation = float3(worldMatrix[3][0], worldMatrix[3][1], worldMatrix[3][2]);
    worldMin = translation;
    worldMax = translation;

    for (int i = 0; i < 3; i++)
    {
        float3 col = float3(worldMatrix[i][0], worldMatrix[i][1], worldMatrix[i][2]);
        float3 a = col * localMin[i];
        float3 b = col * localMax[i];
        worldMin += min(a, b);
        worldMax += max(a, b);
    }
}

bool AABBInFrustum(float3 aabbMin, float3 aabbMax)
{
    for (int i = 0; i < 6; i++)
    {
        float3 normal = pc.frustumPlanes[i].xyz;
        float d = pc.frustumPlanes[i].w;
        float3 center = (aabbMin + aabbMax) * 0.5;
        float3 halfSize = (aabbMax - aabbMin) * 0.5;
        float dist = dot(normal, center) + d;
        float radius = dot(abs(normal), halfSize);
        if (dist < -radius)
            return false;
    }
    return true;
}

#if defined(HIZ_OCCLUSION) || defined(VOXEL_HIZ)
// Hi-Z occlusion test. Used by the OcclusionCullingPass (HIZ_OCCLUSION, regular opaque phase 2) and
// the frustum CullingPass voxel emit (VOXEL_HIZ, tests voxel chunks against last frame's voxel-
// inclusive Hi-Z). Reverse-Z: device z near = 1, far = 0,
// so the pyramid stores the per-tile MINIMUM (farthest of the nearest surfaces) and an
// object is occluded when even its nearest corner is behind that minimum.
[[vk::binding(13, 0)]] Texture2D<float> HiZPyramid;
[[vk::binding(14, 0)]] cbuffer OcclusionUBO
{
    float4x4 occViewProj;
    uint occPyrWidth;
    uint occPyrHeight;
    uint occMipCount;
    float occBias;
};

bool AABBOccluded(float3 aabbMin, float3 aabbMax)
{
    float3 corners[8] = {
        float3(aabbMin.x, aabbMin.y, aabbMin.z), float3(aabbMax.x, aabbMin.y, aabbMin.z),
        float3(aabbMin.x, aabbMax.y, aabbMin.z), float3(aabbMax.x, aabbMax.y, aabbMin.z),
        float3(aabbMin.x, aabbMin.y, aabbMax.z), float3(aabbMax.x, aabbMin.y, aabbMax.z),
        float3(aabbMin.x, aabbMax.y, aabbMax.z), float3(aabbMax.x, aabbMax.y, aabbMax.z)};

    float2 ndcMin = float2(1e30, 1e30);
    float2 ndcMax = float2(-1e30, -1e30);
    float nearestZ = 0.0; // reverse-Z: nearest corner = max device depth

    [unroll] for (int i = 0; i < 8; ++i)
    {
        float4 clip = mul(float4(corners[i], 1.0), occViewProj); // row-vector convention (v*M), matches engine shaders
        if (clip.w <= 0.0)
            return false; // crosses/behind the near plane -> conservatively keep visible
        float3 ndc = clip.xyz / clip.w;
        ndcMin = min(ndcMin, ndc.xy);
        ndcMax = max(ndcMax, ndc.xy);
        nearestZ = max(nearestZ, ndc.z);
    }

    // Conservative edge rule: if the footprint leaves the viewport on any side, a clamped
    // Hi-Z rect would not faithfully bound the on-screen region (and an object straddling
    // the border may still be visible), so keep it rather than risk a false occlusion at
    // the screen edge. This also covers off-screen objects when frustum culling is disabled.
    if (ndcMin.x < -1.0 || ndcMin.y < -1.0 || ndcMax.x > 1.0 || ndcMax.y > 1.0)
        return false;

    // NDC [-1,1] -> [0,1]. Both axes use ndc*0.5+0.5: every main raster pass renders with a
    // positive-height viewport (Vulkan) / inverted-viewport-height (DX12), so window.y =
    // (ndc.y*0.5+0.5)*H, the exact mapping the depth buffer (and thus the pyramid) was built
    // with. saturate is now only a numerical-safety clamp (the edge rule kept us in-bounds).
    float2 uvMin = saturate(ndcMin * 0.5 + 0.5);
    float2 uvMax = saturate(ndcMax * 0.5 + 0.5);
    float2 pxMin = uvMin * float2(occPyrWidth, occPyrHeight);
    float2 pxMax = uvMax * float2(occPyrWidth, occPyrHeight);

    float rectW = max(1.0, pxMax.x - pxMin.x);
    float rectH = max(1.0, pxMax.y - pxMin.y);
    // ceil (not floor): pick the coarser mip so the footprint always collapses to <=2x2 texels
    // at this level. With floor, a footprint spanning 3 tiles leaves a middle tile unsampled by
    // the four corner taps, which can read a farther depth than reality and falsely occlude a
    // visible object. ceil keeps the four corners covering the whole footprint (conservative).
    int mip = (int)clamp(ceil(log2(max(rectW, rectH))), 0.0, (float)(occMipCount - 1));

    uint mipW = max(1u, occPyrWidth >> mip);
    uint mipH = max(1u, occPyrHeight >> mip);
    int2 c0 = int2(clamp(int(pxMin.x) >> mip, 0, int(mipW) - 1), clamp(int(pxMin.y) >> mip, 0, int(mipH) - 1));
    int2 c1 = int2(clamp(int(pxMax.x) >> mip, 0, int(mipW) - 1), clamp(int(pxMax.y) >> mip, 0, int(mipH) - 1));

    float tileDepth = HiZPyramid.Load(int3(c0.x, c0.y, mip));
    tileDepth = min(tileDepth, HiZPyramid.Load(int3(c1.x, c0.y, mip)));
    tileDepth = min(tileDepth, HiZPyramid.Load(int3(c0.x, c1.y, mip)));
    tileDepth = min(tileDepth, HiZPyramid.Load(int3(c1.x, c1.y, mip)));

    // RELATIVE slack, not absolute: occBias is a fraction of the occluder depth, so the only thing
    // it protects is geometry at (or within occBias of) the occluder's own depth — coplanar/touching
    // surfaces that the coarse min-reduced tile could otherwise quantize to "behind" and falsely cull.
    // Reverse-Z device depths are tiny under an infinite-far projection (~1e-4), so an absolute slack
    // is scene-scale dependent and easily swamps the whole depth; a fraction stays correct at any scale.
    return nearestZ <= tileDepth * (1.0 - occBias);
}
#endif

#if defined(PHASE1) || defined(PHASE2)
// Two-phase temporal Hi-Z occlusion culling. Per-draw visibility flag carried from the
// previous frame's phase 2. Phase 1 draws last-frame-visible opaque objects (set A) to seed
// this frame's depth/Hi-Z; phase 2 tests every object against THIS frame's Hi-Z, rewrites the
// flag, and emits only the newly-disoccluded opaque objects (set B). Occluded objects end up in
// neither set -> drawn 0x, which is the whole point (cull the depth prepass, not just GBuffer).
[[vk::binding(15, 0)]] RWStructuredBuffer<uint> Visibility;

// Emit a passing OPAQUE draw into the phase output set (descriptor binds set A or B + its
// counter). Transparents/selected are never occlusion-culled — the frustum Culling pass owns
// those — so the phase variants only handle renderType 1/2 (opaque / alpha-cut, SS + DS).
void EmitOpaque(DrawIndexedIndirectCommand cmd, uint type, bool doubleSided)
{
    uint offset = 0;
    if (type == 1)
    {
        if (doubleSided) { InterlockedAdd(Counters[5], 1, offset); IndirectOpaqueDS[offset] = cmd; }
        else { InterlockedAdd(Counters[0], 1, offset); IndirectOpaqueSS[offset] = cmd; }
    }
    else if (type == 2)
    {
        if (doubleSided) { InterlockedAdd(Counters[6], 1, offset); IndirectAlphaCutDS[offset] = cmd; }
        else { InterlockedAdd(Counters[1], 1, offset); IndirectAlphaCutSS[offset] = cmd; }
    }
}
#endif

#if !defined(PHASE1) && !defined(PHASE2)
// Wave-coalesced append into a counter-addressed buffer. The lanes of this wave with `emit==true`
// reserve a contiguous slot range with a SINGLE InterlockedAdd issued by the first active lane
// (waveCount==0 skips the atomic entirely); every lane then returns its own slot = base + (number
// of earlier active emit-lanes in the wave). Replaces the per-lane InterlockedAdd on the hot opaque
// buckets: same-address atomics are warp-coalesced on NVIDIA SPIR-V but serialize on DXIL/DX12, the
// sole cause of the ~55x CullingPass GPU-time gap at 50k draws. All active lanes must call this
// uniformly so the wave ballot sees the full set; `emit` selects which of them actually reserve.
uint WaveAppend(uint counterIndex, bool emit)
{
    uint laneSlot = WavePrefixCountBits(emit);
    uint waveCount = WaveActiveCountBits(emit);
    uint base = 0;
    if (waveCount > 0 && WaveIsFirstLane())
        InterlockedAdd(Counters[counterIndex], waveCount, base);
    base = WaveReadLaneFirst(base);
    return base + laneSlot;
}
#endif

[numthreads(64, 1, 1)] void mainCS(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    if (idx >= pc.maxDrawCount)
        return;

    DrawIndexedIndirectCommand cmd = IndirectCommandsIn[idx];

    if (cmd.indexCount == 0 || cmd.instanceCount == 0)
        return;

    Mesh_Constants constants = MeshConstants[idx];

    bool isVoxel = (constants.editorFlags & 4u) != 0u;

    // Per-instance render-visible flag (NodeGpuData byte offset 128, after the two matrices).
    // Cleared by node:set_visible(false) to cull this draw cheaply — no instance/TLAS rebuild.
    if (NodeData.Load(constants.meshDataOffset + 128u) == 0u)
        return;

    float3 localMin = float3(constants.aabbMinX, constants.aabbMinY, constants.aabbMinZ);
    float3 localMax = float3(constants.aabbMaxX, constants.aabbMaxY, constants.aabbMaxZ);

    float4x4 worldMatrix = LoadMatrix(constants.meshDataOffset);
    float3 aabbMin, aabbMax;
    TransformAABB(localMin, localMax, worldMatrix, aabbMin, aabbMax);

    // Discrete LOD: pick a level by camera distance to the world AABB center and override this draw's
    // index range. Applies to every variant (frustum, occlusion, phase1/2) since they all emit `cmd`.
    if (lodEnabled != 0u && constants.lodMeshEnabled != 0u && constants.lodCount > 1u)
    {
        float3 lodCenter = (aabbMin + aabbMax) * 0.5;
        float3 lodCam = float3(pc.cameraPositionX, pc.cameraPositionY, pc.cameraPositionZ);
        float lodDist = distance(lodCam, lodCenter) * lodBias * constants.lodMeshBias;
        uint lod = 0u;
        if (lodDist > lodDistances.x) lod = 1u;
        if (lodDist > lodDistances.y) lod = 2u;
        if (lodDist > lodDistances.z) lod = 3u;
        lod = min(lod + constants.lodShift, constants.lodCount - 1u); // per-mesh shift toward coarser
        cmd.firstIndex = constants.lodIndexOffset[lod];
        cmd.indexCount = constants.lodIndexCount[lod];
    }

#if defined(PHASE1)
    if (isVoxel)
        return; // voxels use the frustum-only voxel bucket, not the standard-pbr Hi-Z buckets.
    // Two-phase Hi-Z, phase 1: draw last-frame-visible opaque objects only (set A).
    if (pc.enableFrustumCulling)
    {
        if (!AABBInFrustum(aabbMin, aabbMax))
            return;
    }
    if (Visibility[idx] == 0u)
        return;
    cmd.firstInstance = idx;
    EmitOpaque(cmd, constants.renderType, (constants.editorFlags & 2) != 0);
#elif defined(PHASE2)
    if (isVoxel)
        return; // voxels use the frustum-only voxel bucket, not the standard-pbr Hi-Z buckets.
    // Two-phase Hi-Z, phase 2: test every frustum-visible object against THIS frame's Hi-Z,
    // rewrite its visibility flag, and emit only the newly-disoccluded opaque objects (set B).
    if (pc.enableFrustumCulling)
    {
        if (!AABBInFrustum(aabbMin, aabbMax))
            return;
    }
    uint wasVisible = Visibility[idx];
    bool visibleNow = !AABBOccluded(aabbMin, aabbMax);
    Visibility[idx] = visibleNow ? 1u : 0u;
    if (visibleNow && wasVisible == 0u)
    {
        cmd.firstInstance = idx;
        EmitOpaque(cmd, constants.renderType, (constants.editorFlags & 2) != 0);
    }
#else
    if (pc.enableFrustumCulling)
    {
        if (!AABBInFrustum(aabbMin, aabbMax))
            return;
    }

#ifdef HIZ_OCCLUSION
    if (AABBOccluded(aabbMin, aabbMax))
        return;
#endif

    cmd.firstInstance = idx;

    uint type = constants.renderType;
    bool doubleSided = (constants.editorFlags & 2) != 0;

    // Per-bucket emit predicates. Render types are mutually exclusive; "selected" is independent and
    // can fire alongside any type. Evaluated for every active lane (no early branch) so the
    // wave-coalesced appends below see the full ballot for each bucket.
    bool emitVoxel = isVoxel;
#ifdef VOXEL_HIZ
    // Temporal Hi-Z: cull voxel chunks occluded in last frame's voxel-inclusive depth pyramid.
    // Only voxels are tested here (regular opaque occlusion is owned by the two-phase passes).
    if (emitVoxel && pc.enableVoxelOcclusion != 0u && AABBOccluded(aabbMin, aabbMax))
        emitVoxel = false;
#endif
    bool emitOpaqueSS = !isVoxel && (type == 1) && !doubleSided;
    bool emitOpaqueDS = !isVoxel && (type == 1) && doubleSided;
    bool emitAlphaCutSS = !isVoxel && (type == 2) && !doubleSided;
    bool emitAlphaCutDS = !isVoxel && (type == 2) && doubleSided;
    bool emitAlphaBlend = !isVoxel && (type == 3);
    bool emitTransmission = !isVoxel && (type == 4);
    bool emitSelected = !isVoxel && ((constants.editorFlags & 1) != 0);

    // One atomic per wave per non-empty bucket instead of one per visible lane (see WaveAppend).
    uint slot;

    slot = WaveAppend(0, emitOpaqueSS);
    if (emitOpaqueSS)
        IndirectOpaqueSS[slot] = cmd;

    slot = WaveAppend(5, emitOpaqueDS);
    if (emitOpaqueDS)
        IndirectOpaqueDS[slot] = cmd;

    slot = WaveAppend(1, emitAlphaCutSS);
    if (emitAlphaCutSS)
        IndirectAlphaCutSS[slot] = cmd;

    slot = WaveAppend(6, emitAlphaCutDS);
    if (emitAlphaCutDS)
        IndirectAlphaCutDS[slot] = cmd;

    slot = WaveAppend(2, emitAlphaBlend);
    if (emitAlphaBlend)
    {
        float3 center = (aabbMin + aabbMax) * 0.5;
        float3 camPos = float3(pc.cameraPositionX, pc.cameraPositionY, pc.cameraPositionZ);
        IndirectAlphaBlendOut[slot] = cmd;
        SortKeysAlphaBlend[slot] = -distance(camPos, center); // negative: ascending sort gives back-to-front
    }

    slot = WaveAppend(3, emitTransmission);
    if (emitTransmission)
    {
        float3 center = (aabbMin + aabbMax) * 0.5;
        float3 camPos = float3(pc.cameraPositionX, pc.cameraPositionY, pc.cameraPositionZ);
        IndirectTransmissionOut[slot] = cmd;
        SortKeysTransmission[slot] = -distance(camPos, center); // negative: ascending sort gives back-to-front
    }

    slot = WaveAppend(4, emitSelected);
    if (emitSelected)
        IndirectSelectedOut[slot] = cmd;

    slot = WaveAppend(7, emitVoxel);
    if (emitVoxel)
        IndirectVoxelsOut[slot] = cmd;
#endif
}
