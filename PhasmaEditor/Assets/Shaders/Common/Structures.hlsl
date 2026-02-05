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
        uint num_point_lights;
        uint num_spot_lights;
        uint num_area_lights;
        float2 framebufferSize;
        uint passType;
        float max_cascade_dist[SHADOWMAP_CASCADES];
    };
#endif

struct PushConstants_GBuffer
{
    uint jointsCount;
    float pad0;
    float2 projJitter;
    float2 prevProjJitter;
    uint passType;
    float pad1;
};

struct PushConstants_RayTracing
{
    uint num_point_lights;
    uint num_spot_lights;
    uint num_area_lights;
    float pad;
};

struct Mesh_Constants
{
    float alphaCut;
    uint meshDataOffset;
    uint textureMask;
    int meshImageIndex[5];
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
    float4 tangent              : TANGENT;
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
    float4 tangent              : TEXCOORD4;
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
    float4 color : COLOR;
};
// -----------------------------------------

// ------------- Pixel structs -------------
struct PS_INPUT_Gbuffer
{
    float2 uv                   : TEXCOORD0;
    float3 normal               : NORMAL;
    float4 tangent              : TEXCOORD4;
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
    float4 color                : COLOR;
};

struct PS_OUTPUT_Gbuffer
{
    float3 normal               : SV_TARGET0;
    float4 albedo               : SV_TARGET1;
    float4 metRough             : SV_TARGET2;
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
    float4 color;    // .w = intensity
    float4 position; // .w = radius
};

struct SpotLight
{
    float4 color;    // .w = intensity
    float4 position; // .w = range
    float4 rotation; // quaternion
    float4 params;   // .x = angle, .y = falloff
};

struct AreaLight
{
    float4 color;    // .w = intensity
    float4 position; // .w = range
    float4 rotation; // quaternion
    float4 size;     // .x = width, .y = height
};
// -----------------------------------------
struct Particle
{
    float4 position; // w: life
    float4 velocity; // w: size
    float4 color;
    float4 extra; // x: textureIndex
};

struct ParticleEmitter
{
    float4 position;      // xyz: position
    float4 velocity;      // xyz: base velocity direction
    float4 colorStart;    // rgba: start color
    float4 colorEnd;      // rgba: end color
    float4 sizeLife;      // x: sizeStart, y: sizeEnd, z: lifeMin, w: lifeMax
    float4 physics;       // x: spawnRate, y: spawnRadius, z: noiseStrength, w: drag
    float4 gravity;       // xyz: gravity vector
    float4 animation;     // x: rows, y: cols, z: speed (multi), w: unused
    uint textureIndex;
    uint count;
    uint offset;
    uint orientation;     // 0: Billboard, 1: Horizontal, 2: Vertical, 3: Velocity
};

// -----------------------------------------
struct PushConstants_Particle
{
    float4 cameraPosition;
    float4 cameraForward;
    float deltaTime;
    uint particleCount;
    float totalTime;
    uint emitterCount;
};

struct PerFrameData_Particle
{
    float4x4 viewProjection;
    float4 cameraRight;
    float4 cameraUp;
    float4 cameraPosition;
    float4 cameraForward;
};

struct VS_OUTPUT_Particle
{
    float4 pos : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    nointerpolation float textureIndex : TEXCOORD1;
    float2 uv2 : TEXCOORD2;
    float blendFactor : TEXCOORD3;
};

struct PS_INPUT_Particle
{
    float4 pos : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    nointerpolation float textureIndex : TEXCOORD1;
    float2 uv2 : TEXCOORD2;
    float blendFactor : TEXCOORD3;
};

struct PS_OUTPUT_Particle
{
    float4 color : SV_Target0;
};

#endif // STRUCTURES_H_
