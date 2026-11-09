// HingeShadowVS.hlsl - the hinge field as a shadow caster: no vertex buffer, see HingeField.hlsl.
#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"
#include "HingeField.hlsl"

struct PushConstants_HingeShadow
{
    float4x4 vp;
    float time;
    uint generation;
    uint base;
};

[[vk::push_constant]] PushConstants_HingeShadow pc;
[[vk::binding(0, 0)]] StructuredBuffer<HingeInstance> instances;

VS_OUTPUT_Position mainVS(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
    VS_OUTPUT_Position output = (VS_OUTPUT_Position)0;
    const HingeInstance inst = instances[pc.base + instance];
    if (HingeHidden(inst.state))
        return output;

    const float angle = HingeAngle(inst, pc.time, pc.generation);
    const float3 local = HingeCornerLocal(vertex / 6u, vertex % 6u);
    const float3 world = HingeWorld(inst, local, HingeSide(inst.state), angle, cos(inst.yaw), sin(inst.yaw));
    output.position = ApplyViewportYConvention(mul(float4(world, 1.0f), pc.vp));
    return output;
}
