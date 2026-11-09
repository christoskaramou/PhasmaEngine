// HingeGBufferVS.hlsl - the hinge field's G-buffer vertex shader: no vertex buffer, see HingeField.hlsl.
#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"
#include "HingeField.hlsl"

struct PushConstants_HingeField
{
    float2 projJitter;
    float2 prevProjJitter;
    float time;
    float prevTime;
    uint generation;
    uint base;
    float3 eye;
    uint farLod;
    float nearDistance;
};

[[vk::push_constant]] PushConstants_HingeField pc;
[[vk::binding(0, 0)]] ByteAddressBuffer data;
[[vk::binding(1, 0)]] StructuredBuffer<HingeInstance> instances;

float4x4 LoadMatrix(uint offset)
{
    float4x4 result;
    result[0] = asfloat(data.Load4(offset + 0 * 16));
    result[1] = asfloat(data.Load4(offset + 1 * 16));
    result[2] = asfloat(data.Load4(offset + 2 * 16));
    result[3] = asfloat(data.Load4(offset + 3 * 16));
    return result;
}

struct VS_OUTPUT_Hinge
{
    float3 normal : NORMAL;
    float3 local : TEXCOORD0;
    float4 positionCS : POSITION0;
    float4 prevPositionCS : POSITION1;
    nointerpolation float4 rotation : TEXCOORD1; // cos yaw, sin yaw, cos tilt, sin tilt
    nointerpolation float2 side : TEXCOORD2;     // fall side, tint seed
    nointerpolation uint face : TEXCOORD3;
    nointerpolation uint color : TEXCOORD4;
    float height : TEXCOORD5;
    nointerpolation float distanceFade : TEXCOORD6; // 0 up close, 1 where a box is a speck
    float4 position : SV_POSITION;
};

VS_OUTPUT_Hinge mainVS(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
    VS_OUTPUT_Hinge output = (VS_OUTPUT_Hinge)0;
    const uint index = pc.base + instance;
    const HingeInstance inst = instances[index];
    if (HingeHidden(inst.state))
        return output; // w = 0 on every corner: nothing rasterizes

    const float cosY = cos(inst.yaw);
    const float sinY = sin(inst.yaw);
    const float side = HingeSide(inst.state);
    const float angle = HingeAngle(inst, pc.time, pc.generation);
    const float cosA = cos(angle);
    const float sinA = sin(angle);

    uint face = vertex / 6u;
    if (pc.farLod != 0u)
    {
        const float3 toEye = pc.eye - float3(inst.x, inst.y, inst.z);
        face = HingeFarFace(face, HingeYaw(toEye, cosY, -sinY), side, angle);
    }

    float3 local = HingeCornerLocal(face, vertex % 6u);

    // Far away the boxes close up into a carpet: thin tops would alias against the pixels into moire. The shading has
    // gone flat before this starts (see the PS), or the half-closed gaps would read as stripes.
    const float toEyeLength = distance(pc.eye, float3(inst.x, inst.y, inst.z));
    const float distanceFade = pc.farLod != 0u ? saturate((toEyeLength - pc.nearDistance) / pc.nearDistance) : 0.0f;
    const float carpet = saturate((distanceFade - 0.25f) / 0.75f);
    if (carpet > 0.0f)
    {
        const float standing = 1.0f - saturate(angle / 0.35f);
        if (face == 2u)
            local.x *= lerp(1.0f, 0.31f / kHingeHalf.x, carpet * standing);
        local.z *= lerp(1.0f, 0.3f / kHingeHalf.z, carpet); // exactly side by side: any overlap would z-fight
    }

    const float3 world = HingeWorld(inst, local, side, angle, cosY, sinY);
    const float prevAngle = HingeAngle(inst, pc.prevTime, pc.generation);
    const float3 prevWorld = HingeWorld(inst, local, side, prevAngle, cosY, sinY);

    output.positionCS = mul(float4(world, 1.0f), LoadMatrix(0));
    output.prevPositionCS = mul(float4(prevWorld, 1.0f), LoadMatrix(64));
    output.position = ApplyViewportYConvention(output.positionCS);
    output.normal = HingeYaw(HingeTip(kHingeN[face], side, 0.0f, cosA, sinA), cosY, sinY);
    output.local = local;
    output.rotation = float4(cosY, sinY, cosA, sinA);
    output.side = float2(side, frac(float(index) * 0.61803398875f));
    output.face = face;
    output.color = inst.color;
    output.height = world.y - inst.y;
    output.distanceFade = distanceFade;
    return output;
}
