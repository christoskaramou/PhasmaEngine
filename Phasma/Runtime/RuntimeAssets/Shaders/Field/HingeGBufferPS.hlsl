// HingeGBufferPS.hlsl - the hinge field's G-buffer pixel shader: enamel with rolled edges and painted pips.
#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"
#include "HingeField.hlsl"

struct PS_INPUT_Hinge
{
    float3 normal : NORMAL;
    float3 local : TEXCOORD0;
    float4 positionCS : POSITION0;
    float4 prevPositionCS : POSITION1;
    nointerpolation float4 rotation : TEXCOORD1;
    nointerpolation float2 side : TEXCOORD2;
    nointerpolation uint face : TEXCOORD3;
    nointerpolation uint color : TEXCOORD4;
    float height : TEXCOORD5;
    nointerpolation float distanceFade : TEXCOORD6;
};

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

// Which of a half's nine pip places (bit = column + 3 * row) are filled for 0 to 6.
static const uint kPipMasks[7] = { 0x000u, 0x010u, 0x101u, 0x111u, 0x145u, 0x155u, 0x16Du };
static const float kPipStep = 0.14f;
static const float kPipRadius = 0.043f;

// How much of this point is pip or the dividing line (0 to 1), faded out before it would shimmer.
float PipMask(float2 p, float seed)
{
    const float footprint = max(fwidth(p.x), fwidth(p.y));
    const float visible = 1.0f - saturate((footprint - 0.03f) / 0.07f);
    if (visible <= 0.0f)
        return 0.0f;

    const float divider = 1.0f - smoothstep(0.012f - footprint, 0.012f + footprint, abs(p.y));
    const float top = p.y >= 0.0f ? 1.0f : 0.0f;
    const uint hash = uint(seed * 4096.0f);
    const uint number = (top > 0.5f ? hash >> 4 : hash) % 7u;
    const float2 q = float2(p.x, abs(p.y) - 0.25f);
    const int column = clamp(int(round(q.x / kPipStep)) + 1, 0, 2);
    const int row = clamp(int(round(q.y / kPipStep)) + 1, 0, 2);
    const uint bit = uint(column + 3 * row);

    float pip = 0.0f;
    if (((kPipMasks[number] >> bit) & 1u) != 0u)
    {
        const float d = length(q - float2(column - 1, row - 1) * kPipStep);
        pip = 1.0f - smoothstep(kPipRadius - footprint, kPipRadius + footprint, d);
    }
    return max(pip, divider) * visible;
}

PS_OUTPUT_Gbuffer mainPS(PS_INPUT_Hinge input)
{
    PS_OUTPUT_Gbuffer output;

    float3 color = float3(input.color & 255u, (input.color >> 8) & 255u, (input.color >> 16) & 255u) / 255.0f;
    color *= 0.94f + 0.12f * input.side.y; // no two quite alike

    // A fallen box shows its plain back: from afar the wave sweeps the picture away and leaves a sepia ghost of it.
    const float fallen = saturate(input.rotation.w / 0.7f) * 0.9f * saturate(input.distanceFade * 4.0f);
    const float ghost = dot(color, float3(0.3f, 0.59f, 0.11f));
    color = lerp(color, float3(0.46f, 0.36f, 0.26f) * (0.25f + ghost), fallen);

    // Rolled edges: within a bevel of an edge the normal leans toward the neighbouring face.
    const float3 centred = input.local - float3(0.0f, kHingeHalf.y, 0.0f);
    const float3 toEdge = kHingeHalf - abs(centred);
    const float3 faceN = kHingeN[input.face];
    const float3 bend = (1.0f - saturate(toEdge / 0.03f)) * sign(centred) * (1.0f - abs(faceN));
    const float3 localN = normalize(faceN + bend * bend * sign(bend) * 0.9f);

    const float cosA = input.rotation.z;
    const float sinA = input.rotation.w;
    const float3 tipped = HingeTip(localN, input.side.x, 0.0f, cosA, sinA);
    // Far off, every box takes the same light: a thousand tilted specks would shimmer against the pixels.
    const float flat = saturate(input.distanceFade * 4.0f);
    const float3 worldN = HingeYaw(tipped, input.rotation.x, input.rotation.y);
    const float3 N = normalize(lerp(worldN, float3(0.0f, 1.0f, 0.0f), flat * 0.85f));

    // Enamel up close; far off a matte carpet, or its million small highlights wash the colours out.
    float roughness = lerp(0.42f, 0.95f, flat);
    if (input.face < 2u)
    {
        const float pip = PipMask(float2(centred.z, centred.y), input.side.y);
        const float luma = dot(color, float3(0.3f, 0.59f, 0.11f));
        color = lerp(color, luma < 0.16f ? float3(0.88f, 0.82f, 0.68f) : float3(0.03f, 0.03f, 0.03f), pip);
        roughness = lerp(roughness, 0.45f, pip);
    }

    // Contact shade toward the ground, where the neighbours and the floor shut out the sky.
    const float occlusion = lerp(lerp(0.58f, 1.0f, saturate(input.height / 0.32f)), 1.0f, flat);

    output.normal = float4(N * 0.5f + 0.5f, 1.0f);
    output.albedo = float4(color, 1.0f);
    output.metRough = float4(occlusion, roughness, 0.0f, 0.0f);
    output.emissive = float4(color * 0.6f * flat, 1.0f); // seen from afar the field is a painting: keep its brights
    output.transparency = 0.0f;
#ifdef GBUFFER_VISIBILITY
    output.visibility = uint4(0, 0, 0, 0); // clear the prepass payload where this surface wins depth
#endif
#ifndef GBUFFER_NO_VELOCITY
    float2 currentNDC = input.positionCS.xy / input.positionCS.w;
    float2 previousNDC = input.prevPositionCS.xy / input.prevPositionCS.w;
    currentNDC -= pc.projJitter;
    previousNDC -= pc.prevProjJitter;
    output.velocity = previousNDC - currentNDC;
#endif
    return output;
}
