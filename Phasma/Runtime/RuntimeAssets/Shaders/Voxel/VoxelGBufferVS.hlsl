// VoxelGBufferVS.hlsl — custom GBuffer vertex shader for the voxel subsystem.
//
// Reads the packed 8 B voxel vertex (see VoxelVertex in API/Vertex.h) directly from the input
// assembler (reflection derives a 2x R32_UINT, stride-8 layout from the struct below) and unpacks
// it into the stock GBuffer interpolants plus the flat tile layer the PS uses to pick its
// Texture2DArray slice. World position = the slot's AABB min (== section origin, in Mesh_Constants)
// plus the section-local position packed in w0; this is what lets positions live in 5 bits each.
// Voxel meshes are static (no skinning) and flat-shaded, so normal/tangent are rebuilt from a
// 3-bit face id and AO is rebuilt from a 2-bit level — none of it is stored per-vertex as floats.

#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"

[[vk::push_constant]] PushConstants_GBuffer pc;
[[vk::binding(0, 0)]] ByteAddressBuffer data;
[[vk::binding(1, 0)]] StructuredBuffer<Mesh_Constants> constants;

static const uint MATRIX_SIZE = 64u;

// AO level -> brightness multiplier. MUST match kAoLevel in GreedyMesher.cpp.
static const float kVoxelAo[4] = {0.45f, 0.65f, 0.82f, 1.0f};

float4x4 LoadMatrix(uint offset)
{
    float4x4 result;
    result[0] = asfloat(data.Load4(offset + 0 * 16));
    result[1] = asfloat(data.Load4(offset + 1 * 16));
    result[2] = asfloat(data.Load4(offset + 2 * 16));
    result[3] = asfloat(data.Load4(offset + 3 * 16));
    return result;
}

float4x4 GetViewProjection()            { return LoadMatrix(0); }
float4x4 GetPreviousViewProjection()    { return LoadMatrix(64); }
float4x4 GetMeshMatrix(uint id)         { return LoadMatrix(constants[id].meshDataOffset); }
float4x4 GetMeshPreviousMatrix(uint id) { return LoadMatrix(constants[id].meshDataOffset + MATRIX_SIZE); }

// Face id 0..5 (axis<<1 | (dir<0)) -> unit normal. MUST match GreedyMesher.cpp normalId encoding.
float3 VoxelNormalFromId(uint id)
{
    uint axis = id >> 1;
    float s = (id & 1u) != 0u ? -1.0f : 1.0f;
    float3 n = float3(0.0f, 0.0f, 0.0f);
    if (axis == 0u) n.x = s; else if (axis == 1u) n.y = s; else n.z = s;
    return n;
}

// Tangent along the next axis ((axis+1)%3), matching the mesher's tang[u]=1 (u=(d+1)%3).
float3 VoxelTangentFromId(uint id)
{
    uint t = ((id >> 1) + 1u) % 3u;
    float3 r = float3(0.0f, 0.0f, 0.0f);
    if (t == 0u) r.x = 1.0f; else if (t == 1u) r.y = 1.0f; else r.z = 1.0f;
    return r;
}

// Packed voxel vertex: w0 = pos5x3 | normal3 | ao2, w1 = tile16 | u8 | v8.
struct VS_INPUT_VoxelPacked
{
    uint w0 : POSITION;
    uint w1 : TEXCOORD0;
#if defined(PE_DX12)
    uint id : PE_DRAW_ID;
#else
    uint id : SV_InstanceID;
#endif
};

// Voxel VS output: stock GBuffer interpolants plus the flat tile layer.
struct VS_OUTPUT_Voxel
{
    float2 uv : TEXCOORD0;
    float3 normal : NORMAL;
    float4 tangent : TEXCOORD4;
    float4 color : COLOR;
    float4 positionCS : POSITION0;
    float4 prevPositionCS : POSITION1;
    float4 positionWS : POSITION2;
    nointerpolation uint id : TEXCOORD5;
    nointerpolation uint tileLayer : TEXCOORD6;
    float4 position : SV_POSITION;
};

VS_OUTPUT_Voxel mainVS(VS_INPUT_VoxelPacked input)
{
    VS_OUTPUT_Voxel output;

    const uint id = input.id;
    output.id = id;

    const uint w0 = input.w0;
    const uint w1 = input.w1;

    // Section-local position (0..16); uv may exceed 0..1 across a greedy-merged quad (PS uses frac()).
    float3 localPos = float3(w0 & 31u, (w0 >> 5) & 31u, (w0 >> 10) & 31u);
    uint normalId = (w0 >> 15) & 7u;
    uint ao = (w0 >> 18) & 3u;
    output.tileLayer = w1 & 0xFFFFu;
    output.uv = float2((w1 >> 16) & 0xFFu, (w1 >> 24) & 0xFFu);

    // World origin = the slot's AABB min (set to the section origin on upload).
    float3 origin = float3(constants[id].aabbMinX, constants[id].aabbMinY, constants[id].aabbMinZ);
    float4 inPos = float4(origin + localPos, 1.0f);

    float4x4 worldTransform = GetMeshMatrix(id);
    output.positionWS = mul(inPos, worldTransform);
    output.positionCS = mul(inPos, mul(GetMeshMatrix(id), GetViewProjection()));
    output.prevPositionCS = mul(inPos, mul(GetMeshPreviousMatrix(id), GetPreviousViewProjection()));
    output.position = ApplyViewportYConvention(output.positionCS);

    float3x3 worldRotationScale3x3 = (float3x3)worldTransform;
    float3x3 worldRotation3X3 = remove_scale3x3(worldRotationScale3x3);
    output.normal = normalize(mul(VoxelNormalFromId(normalId), worldRotation3X3));
    output.tangent.xyz = normalize(mul(VoxelTangentFromId(normalId), worldRotation3X3));
    output.tangent.w = 1.0f;

    float aoLevel = kVoxelAo[ao];
    output.color = float4(aoLevel, aoLevel, aoLevel, 1.0f);

    return output;
}
