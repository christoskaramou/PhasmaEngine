// VoxelGBufferVS.hlsl — custom GBuffer vertex shader for the voxel subsystem.
//
// Spike 0B / Task 8 artifact. Mirrors the stock GBuffer VS
// (RuntimeAssets/Shaders/Gbuffer/GBufferVS.hlsl) but reads the per-vertex
// block-tile index packed into joints[0] by the greedy mesher and forwards it
// to the pixel shader as a flat (nointerpolation) uint so the PS can pick the
// Texture2DArray layer. Voxel meshes are static (no skinning), so the joint
// matrix path is intentionally dropped; joints[0] is repurposed as the tile id.
//
// The position/normal/tangent transform matches the stock shader exactly so
// voxel chunks composite into the same GBuffer as ordinary meshes.

#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"

[[vk::push_constant]] PushConstants_GBuffer pc;
[[vk::binding(0, 0)]] ByteAddressBuffer data;
[[vk::binding(1, 0)]] StructuredBuffer<Mesh_Constants> constants;

static const uint MATRIX_SIZE = 64u;

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

VS_OUTPUT_Voxel mainVS(VS_INPUT_Gbuffer input)
{
    VS_OUTPUT_Voxel output;

    // uv may exceed 0..1 across a greedy-merged quad; the PS uses frac() so the
    // tile repeats per block.
    output.uv = input.texCoord.xy;

    const uint id = input.id;
    output.id = id;

    // joints[0] carries the block-tile index (voxel meshes are never skinned).
    output.tileLayer = input.joints[0];

    float4 inPos = float4(input.position, 1.0f);
    float4x4 worldTransform = GetMeshMatrix(id);
    output.positionWS = mul(inPos, worldTransform);
    output.positionCS = mul(inPos, mul(GetMeshMatrix(id), GetViewProjection()));
    output.prevPositionCS = mul(inPos, mul(GetMeshPreviousMatrix(id), GetPreviousViewProjection()));
    output.position = output.positionCS;

    float3x3 worldRotationScale3x3 = (float3x3)worldTransform;
    float3x3 worldRotation3X3 = remove_scale3x3(worldRotationScale3x3);
    output.normal = normalize(mul(input.normal, worldRotation3X3));
    output.tangent.xyz = normalize(mul(input.tangent.xyz, worldRotation3X3));
    output.tangent.w = input.tangent.w;

    output.color = input.color;

    return output;
}
