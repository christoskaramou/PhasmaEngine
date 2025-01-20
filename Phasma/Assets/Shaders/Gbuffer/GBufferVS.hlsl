#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"

[[vk::push_constant]] PushConstants_GBuffer pc;
[[vk::binding(0, 0)]] StructuredBuffer<float4x4> data;

// Matrices
float4x4 GetViewProjection()         { return data[0]; }
float4x4 GetPreviousViewProjection() { return data[1]; }
float4x4 GetMeshMatrix()             { return data[pc.meshIndex]; }
float4x4 GetMeshPreviousMatrix()     { return data[pc.meshIndex + 1]; }
float4x4 GetJointMatrix(uint index)  { return data[pc.meshIndex + 2 + index]; }

// Factors
float4 GetBaseColorFactor()     { return data[pc.primitiveDataIndex][0]; }
float3 GetEmissiveFactor()      { return data[pc.primitiveDataIndex][1].xyz; }
float4 GetMetRoughAlphacutOcl() { return data[pc.primitiveDataIndex][2]; }

VS_OUTPUT_Gbuffer mainVS(VS_INPUT_Gbuffer input)
{
    VS_OUTPUT_Gbuffer output;
    output.uv = input.texCoord.xy;

    float4x4 boneTransform = identity_mat;
    if (pc.jointsCount)
    {
        boneTransform = mul(GetJointMatrix(input.joints[0]), input.weights[0]) +
                        mul(GetJointMatrix(input.joints[1]), input.weights[1]) +
                        mul(GetJointMatrix(input.joints[2]), input.weights[2]) +
                        mul(GetJointMatrix(input.joints[3]), input.weights[3]);
    }

    // World space and clip space positions
    float4 inPos            = float4(input.position, 1.0f);
    float4x4 worldTransform = mul(boneTransform, GetMeshMatrix());
    output.positionWS       = mul(inPos, worldTransform);
    output.positionCS       = mul(inPos, mul(boneTransform, mul(GetMeshMatrix(), GetViewProjection())));
    output.prevPositionCS   = mul(inPos, mul(boneTransform, mul(GetMeshPreviousMatrix(), GetPreviousViewProjection())));
    output.position         = output.positionCS;
    
    // Normal
    float3x3 worldRotationScale3x3  = (float3x3)worldTransform;                       // remove translation
    float3x3 worldRotation3X3       = remove_scale3x3(worldRotationScale3x3);         // remove scale
    output.normal                   = normalize(mul(input.normal, worldRotation3X3)); // transform normal to world space

    // Color
    output.color = input.color;

    // Factors
    output.baseColorFactor      = GetBaseColorFactor();
    output.emissiveFactor       = GetEmissiveFactor();
    output.metRoughAlphacutOcl  = GetMetRoughAlphacutOcl();

    return output;
}

