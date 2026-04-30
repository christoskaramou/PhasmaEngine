// Frustum culling compute shader. One dispatch per drawable; each workgroup
// culls 64 candidate instances and atomically appends surviving indices into
// the drawable's CulledInstances list, while also atomically bumping the
// instanceCount field of the drawable's indirect-args slot.
//
// Ported 1:1 from the WGSL CULLING_SHADER in index.html. Matches upstream
// assumption that each mesh fits inside a unit-radius sphere centered at the
// origin pre-transform.

#include "common.inc.hlsl"

[[vk::binding(0, 0)]] cbuffer CameraCB : register(b0, space0)
{
    CameraUniforms camera;
};

[[vk::binding(0, 1)]] StructuredBuffer<column_major float4x4> instances : register(t0, space1);

// CulledInstances layout (byte-addressable):
//   +0  : uint indirectIndex
//   +4  : uint instances[MAX_INSTANCES_PER_DRAWABLE]
[[vk::binding(1, 1)]] RWByteAddressBuffer culled : register(u0, space1);

// IndirectArgs layout (DrawIndexedIndirectCommand/DrawIndirectCommand, 20B):
//   +0  : drawCount      (= vertex/index count)
//   +4  : instanceCount  (atomically bumped here)
//   +8  : firstIndex / firstVertex
//   +12 : reserved
//   +16 : reserved
[[vk::binding(2, 1)]] RWByteAddressBuffer indirectArgs : register(u1, space1);

#define MAX_INSTANCES_PER_DRAWABLE 1000u

bool isVisible(uint instanceIndex)
{
    float4x4 model = instances[instanceIndex];
    float4 pos = mul(model, float4(0.0, 0.0, 0.0, 1.0));
    const float radius = 1.0;

    [unroll]
    for (int i = 0; i < 6; i++)
    {
        if (dot(camera.frustum[i], pos) < -radius)
            return false;
    }
    return true;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 gid : SV_DispatchThreadID)
{
    uint instanceIndex = gid.x;
    if (instanceIndex >= MAX_INSTANCES_PER_DRAWABLE)
        return;

    if (!isVisible(instanceIndex))
        return;

    // culled.Load(0) is the per-drawable indirect-args slot index.
    uint indirectIndex = culled.Load(0);
    // Atomically bump instanceCount of indirectArgs[indirectIndex].
    // Each args entry is 20 bytes; instanceCount is at offset +4 inside it.
    uint byteOffset = indirectIndex * 20u + 4u;
    uint culledIndex;
    indirectArgs.InterlockedAdd(byteOffset, 1u, culledIndex);

    // Store instanceIndex into culled.instances[culledIndex]
    culled.Store(4u + culledIndex * 4u, instanceIndex);
}
