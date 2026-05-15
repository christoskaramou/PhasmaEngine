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

struct PushConstants_Grid
{
    float2 projJitter;
    float2 padding;
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
    float4 max_cascade_dist;
    float4 cascade_texel_size_world;
    float shadow_distance;
    float shadow_fade_distance;
    float shadow_normal_bias;
    float shadow_filter_radius;
    uint shadow_debug_mode;
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
    uint jointsCount;
};

struct Mesh_Constants
{
    float alphaCut;
    float baseColorAlpha;
    uint meshDataOffset;
    uint textureMask;
    uint materialId;
    int meshImageIndex[5];
    uint materialByteOffset;
    uint editorFlags;
    uint renderType;
    float aabbMinX;
    float aabbMinY;
    float aabbMinZ;
    float aabbMaxX;
    float aabbMaxY;
    float aabbMaxZ;
};

struct MaterialGpuData
{
    float4 baseColorFactor;
    float4 emissiveTransmission; // .xyz = emissive, .w = transmissionFactor
    float4 pbrParams;            // .x = metallic, .y = roughness, .z = alphaCutoff, .w = occlusionStrength
    float4 transmissionVolume;   // .x = thicknessFactor, .y = attenuationDistance, .z = ior, .w = unused
    float4 attenuationColor;     // .xyz = attenuationColor, .w = unused
};

// -----------------------------------------

// ------------ Vertex structs -------------
struct VS_INPUT_Position
{
    float3 position : POSITION;
};

struct VS_INPUT_Depth
{
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
    uint4 joints : BLENDINDICES;
    float4 weights : BLENDWEIGHT;
#if defined(PE_DX12)
    uint id : SV_StartInstanceLocation;
#else
    uint id : SV_InstanceID;
#endif
};

struct VS_INPUT_Gbuffer
{
    float3 position : POSITION;
    float2 texCoord : TEXCOORD;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float4 color : COLOR;
    uint4 joints : BLENDINDICES;
    float4 weights : BLENDWEIGHT;
#if defined(PE_DX12)
    uint id : SV_StartInstanceLocation;
#else
    uint id : SV_InstanceID;
#endif
};

struct VS_OUTPUT_Position
{
    float4 position : SV_POSITION;
};

struct VS_OUTPUT_Position_Uv
{
    float2 uv : TEXCOORD0;
    float4 position : SV_POSITION;
};

struct VS_OUTPUT_Position_Uv_ID
{
    float2 uv : TEXCOORD0;
    float alphaFactor : TEXCOORD1;
    nointerpolation uint id : TEXCOORD2;
    float4 position : SV_POSITION;
};

struct VS_OUTPUT_Gbuffer
{
    float2 uv : TEXCOORD0;
    float3 normal : NORMAL;
    float4 tangent : TEXCOORD4;
    float4 color : COLOR;
    float4 positionCS : POSITION0;
    float4 prevPositionCS : POSITION1;
    float4 positionWS : POSITION2;
    nointerpolation uint id : TEXCOORD5;
    float4 position : SV_POSITION;
};

struct VS_OUTPUT_AABB
{
    float4 color : TEXCOORD0;
    float4 position : SV_POSITION;
};
// -----------------------------------------

// ------------- Pixel structs -------------
struct PS_INPUT_Gbuffer
{
    float2 uv : TEXCOORD0;
    float3 normal : NORMAL;
    float4 tangent : TEXCOORD4;
    float4 color : COLOR;
    float4 positionCS : POSITION0;
    float4 prevPositionCS : POSITION1;
    float4 positionWS : POSITION2;
    nointerpolation uint id : TEXCOORD5;
};

struct PS_INPUT_Position_Uv
{
    float2 uv : TEXCOORD0;
    float4 position : SV_POSITION;
};

struct PS_INPUT_Position_Uv_ID
{
    float2 uv : TEXCOORD0;
    float alphaFactor : TEXCOORD1;
    nointerpolation uint id : TEXCOORD2;
    float4 position : SV_POSITION;
};

struct PS_INPUT_UV
{
    float2 uv : TEXCOORD0;
};

struct PS_INPUT_Color
{
    float4 color : TEXCOORD0;
};

struct PS_OUTPUT_Gbuffer
{
    float4 normal : SV_TARGET0;
    float4 albedo : SV_TARGET1;
    float4 metRough : SV_TARGET2;
    float2 velocity : SV_TARGET3;
    float4 emissive : SV_TARGET4;
    float transparency : SV_TARGET5;
};

struct PS_OUTPUT_Color
{
    float4 color : SV_Target0;
};
// -----------------------------------------

// --------------- Lights ------------------
struct DirectionalLight
{
    float4 color; // .a is the intensity
    float4 position;
    float4 rotation; // quaternion
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
    float4 position;   // xyz: position
    float4 velocity;   // xyz: base velocity direction
    float4 colorStart; // rgba: start color
    float4 colorEnd;   // rgba: end color
    float4 sizeLife;   // x: sizeStart, y: sizeEnd, z: lifeMin, w: lifeMax
    float4 physics;    // x: spawnRate, y: spawnRadius, z: noiseStrength, w: drag
    float4 gravity;    // xyz: gravity vector
    float4 animation;  // x: rows, y: cols, z: speed (multi), w: unused
    uint textureIndex;
    uint count;
    uint offset;
    uint orientation; // 0: Billboard, 1: Horizontal, 2: Vertical, 3: Velocity
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
