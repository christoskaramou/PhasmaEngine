#ifndef STRUCTURES_H_
#define STRUCTURES_H_

// ------------ Push constants -------------
struct PushConstants_DepthPass
{
    uint jointsCount;
};

struct PushConstants_f2
{
    float2 values;
};

struct PushConstants_MotionBlur
{
    float oneOverDelta;
    float strength;
    float2 projJitter;
    int samples;
};

struct PushConstants_Shadows
{
    float4x4 vp;
    uint jointsCount;
};

struct PushConstants_AABB
{
    float2 projJitter;
    uint meshIndex;
    uint color;
};

struct PushConstants_Bloom
{
    float range;
    float strength;
};

struct PushConstants_Bloom_Combine
{
    float strength;
};

#ifdef SHADOWMAP_CASCADES
    struct PushConstants_Lighting
    {
        uint cast_shadows;
        uint num_point_lights;
        float2 framebufferSize;
        uint transparentPass;
        float max_cascade_dist[SHADOWMAP_CASCADES];
    };
#endif

struct PushConstants_GBuffer
{
    uint jointsCount;
    float2 projJitter;
    float2 prevProjJitter;
    uint transparentPass;
};

struct Primitive_Constants
{
    float alphaCut;
    uint meshDataOffset;
    uint primitiveDataOffset;
    uint textureMask;
    uint primitiveImageIndex[5];
};

// -----------------------------------------

// ------------ Vertex structs -------------
struct VS_INPUT_Position
{
    float3 position : POSITION;
};

struct VS_INPUT_Depth
{
    float3 position             : POSITION;
    float2 uv                   : TEXCOORD0;
    uint4 joints                : BLENDINDICES;
    float4 weights              : BLENDWEIGHT;
    uint id                     : SV_InstanceID;
};

struct VS_INPUT_Gbuffer
{
    float3 position             : POSITION;
    float2 texCoord             : TEXCOORD;
    float3 normal               : NORMAL;
    float4 color                : COLOR;
    uint4 joints                : BLENDINDICES;
    float4 weights              : BLENDWEIGHT;
    uint id                     : SV_InstanceID;
};

struct VS_OUTPUT_Position
{
    float4 position             : SV_POSITION;
};

struct VS_OUTPUT_Position_Uv
{
    float2 uv                   : TEXCOORD0;
    float4 position             : SV_POSITION;
};

struct VS_OUTPUT_Position_Uv_ID
{
    float2 uv                   : TEXCOORD0;
    float alphaFactor           : TEXCOORD1;
    float4 position             : SV_POSITION;
    uint id                     : SV_InstanceID;
};

struct VS_OUTPUT_Gbuffer
{
    float2 uv                   : TEXCOORD0;
    float3 normal               : NORMAL;
    float4 color                : COLOR;
    float4 baseColorFactor      : TEXCOORD1;
    float3 emissiveFactor       : TEXCOORD2;
    float4 metRoughAlphacutOcl  : TEXCOORD3;
    float4 positionCS           : POSITION0;
    float4 prevPositionCS       : POSITION1;
    float4 positionWS           : POSITION2;
    float4 position             : SV_POSITION;
    uint id                     : SV_InstanceID;
};

struct VS_OUTPUT_AABB
{
    float4 position : SV_POSITION;
    float3 color : COLOR;
};
// -----------------------------------------

// ------------- Pixel structs -------------
struct PS_INPUT_Gbuffer
{
    float2 uv                   : TEXCOORD0;
    float3 normal               : NORMAL;
    float4 color                : COLOR;
    float4 baseColorFactor      : TEXCOORD1;
    float3 emissiveFactor       : TEXCOORD2;
    float4 metRoughAlphacutOcl  : TEXCOORD3;
    float4 positionCS           : POSITION0;
    float4 prevPositionCS       : POSITION1;
    float4 positionWS           : POSITION2;
    uint id                     : SV_InstanceID;
};

struct PS_INPUT_Position_Uv
{
    float2 uv                   : TEXCOORD0;
    float4 position             : SV_POSITION;
};

struct PS_INPUT_Position_Uv_ID
{
    float2 uv                   : TEXCOORD0;
    float alphaFactor           : TEXCOORD1;
    float4 position             : SV_POSITION;
    uint id                     : SV_InstanceID;
};

struct PS_INPUT_UV
{
    float2 uv                   : TEXCOORD0;
};

struct PS_INPUT_Color
{
    float3 color                : COLOR;
};

struct PS_OUTPUT_Gbuffer
{
    float3 normal               : SV_TARGET0;
    float4 albedo               : SV_TARGET1;
    float3 metRough             : SV_TARGET2;
    float2 velocity             : SV_TARGET3;
    float4 emissive             : SV_TARGET4;
    float transparency          : SV_TARGET5;
};

struct PS_OUTPUT_Color
{
    float4 color                : SV_Target0;
};
// -----------------------------------------

// --------------- Lights ------------------
struct DirectionalLight
{
    float4 color; // .a is the intensity
    float4 direction;
};

struct PointLight
{
    float4 color;
    float4 position; // .w = radius
};

struct SpotLight
{
    float4 color;
    float4 start;
    float4 end; // .w = radius
};
// -----------------------------------------
#endif // STRUCTURES_H_
