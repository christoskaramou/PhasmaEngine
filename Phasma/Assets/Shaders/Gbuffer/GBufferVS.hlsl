#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"

// --- ByteAddressBuffer data ---
// PerFrameData                 -> 2 * sizeof(mat4)
// Constant Buffer indices      -> num of draw calls * sizeof(uint)
// for (num of draw calls)
//      MeshData                -> sizeof(mat4) * 2
//      for (mesh primitives)
//          PrimitiveData       -> sizeof(mat4)
// --- ByteAddressBuffer data ---


[[vk::push_constant]] PushConstants_GBuffer pc;
[[vk::binding(0, 0)]] ByteAddressBuffer data;
[[vk::binding(1, 0)]] StructuredBuffer<Constants_GBuffer> constants;

uint GetConstantBufferID(uint instanceID)
{
    return data.Load(128 + instanceID * 4);
}

float4x4 LoadMatrix(uint offset)
{
    float4x4 result;
    
    result[0] = asfloat(data.Load4(offset + 0 * 16));
    result[1] = asfloat(data.Load4(offset + 1 * 16));
    result[2] = asfloat(data.Load4(offset + 2 * 16));
    result[3] = asfloat(data.Load4(offset + 3 * 16));
    
    return result;
}

// Matrices
float4x4 GetViewProjection()                  { return LoadMatrix(0); }
float4x4 GetPreviousViewProjection()          { return LoadMatrix(64); }
float4x4 GetMeshMatrix(uint id)               { return LoadMatrix(constants[id].meshDataOffset); }
float4x4 GetMeshPreviousMatrix(uint id)       { return LoadMatrix(constants[id].meshDataOffset + 64); }
float4x4 GetJointMatrix(uint id, uint index)  { return LoadMatrix(constants[id].meshDataOffset + 128 + index * 64); }

// Factors
float4 GetBaseColorFactor(uint id)     { return asfloat(data.Load4(constants[id].primitiveDataOffset + 0)); }
float3 GetEmissiveFactor(uint id)      { return asfloat(data.Load3(constants[id].primitiveDataOffset + 16)); }
float4 GetMetRoughAlphacutOcl(uint id) { return asfloat(data.Load4(constants[id].primitiveDataOffset + 32)); }

VS_OUTPUT_Gbuffer mainVS(VS_INPUT_Gbuffer input)
{
    VS_OUTPUT_Gbuffer output;
    output.uv = input.texCoord.xy;

    const uint id = GetConstantBufferID(input.id);
    output.id = id;

    float4x4 boneTransform = identity_mat;
    if (pc.jointsCount) // TODO: fix this when we have a proper joint count
    {
        boneTransform = mul(GetJointMatrix(id, input.joints[0]), input.weights[0]) +
                        mul(GetJointMatrix(id, input.joints[1]), input.weights[1]) +
                        mul(GetJointMatrix(id, input.joints[2]), input.weights[2]) +
                        mul(GetJointMatrix(id, input.joints[3]), input.weights[3]);
    }

    // World space and clip space positions
    float4 inPos            = float4(input.position, 1.0f);
    float4x4 worldTransform = mul(boneTransform, GetMeshMatrix(id));
    output.positionWS       = mul(inPos, worldTransform);
    output.positionCS       = mul(inPos, mul(boneTransform, mul(GetMeshMatrix(id), GetViewProjection())));
    output.prevPositionCS   = mul(inPos, mul(boneTransform, mul(GetMeshPreviousMatrix(id), GetPreviousViewProjection())));
    output.position         = output.positionCS;
    
    // Normal
    float3x3 worldRotationScale3x3  = (float3x3)worldTransform;                       // remove translation
    float3x3 worldRotation3X3       = remove_scale3x3(worldRotationScale3x3);         // remove scale
    output.normal                   = normalize(mul(input.normal, worldRotation3X3)); // transform normal to world space

    // Color
    output.color = input.color;

    // Factors
    output.baseColorFactor      = GetBaseColorFactor(id);
    output.emissiveFactor       = GetEmissiveFactor(id);
    output.metRoughAlphacutOcl  = GetMetRoughAlphacutOcl(id);

    return output;
}

