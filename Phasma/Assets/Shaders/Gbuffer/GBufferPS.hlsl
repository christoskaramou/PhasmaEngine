#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"

[[vk::push_constant]] PushConstants_GBuffer pc;
[[vk::binding(0, 1)]] StructuredBuffer<Constants_GBuffer> constants;
[[vk::binding(1, 1)]] SamplerState material_sampler;
[[vk::binding(2, 1)]] Texture2D textures[];

float4 SampleArray(float2 uv, float index)
{
    return textures[index].Sample(material_sampler, uv);
}

float4 GetBaseColor(uint id, float2 uv)          { return SampleArray(uv, constants[id].primitiveImageIndex[0]); }
float4 GetMetallicRoughness(uint id, float2 uv)  { return SampleArray(uv, constants[id].primitiveImageIndex[1]); }
float4 GetNormal(uint id, float2 uv)             { return SampleArray(uv, constants[id].primitiveImageIndex[2]); }
float4 GetOcclusion(uint id, float2 uv)          { return SampleArray(uv, constants[id].primitiveImageIndex[3]); }
float4 GetEmissive(uint id, float2 uv)           { return SampleArray(uv, constants[id].primitiveImageIndex[4]); }

PS_OUTPUT_Gbuffer mainPS(PS_INPUT_Gbuffer input)
{
    PS_OUTPUT_Gbuffer output;

    const uint id = input.id;
    
    float2 uv = input.uv;
    float4 basicColor = GetBaseColor(id, uv) * input.color;
    if (basicColor.a < constants[id].alphaCut)
            discard;

    float3 metRough         = GetMetallicRoughness(id, uv).xyz;
    float3 emissive         = GetEmissive(id, uv).xyz;
    float3 tangentNormal    = GetNormal(id, uv).xyz;
    float ao                = GetOcclusion(id, uv).r;

    output.normal           = CalculateNormal(input.positionWS.xyz, tangentNormal, input.normal, input.uv) * 0.5f + 0.5f;
    output.albedo           = float4(basicColor.xyz * ao, basicColor.a) * input.baseColorFactor;
    output.metRough         = float3(0.0f, metRough.y, metRough.z);
    output.emissive         = float4(emissive * input.emissiveFactor, 0.0f);
    output.transparency     = pc.transparentPass ? 1.0f : 0.0f;

    // Calculate the velocity
    // Ensure that the positions are in NDC space by dividing by the w component
    float2 currentNDC = input.positionCS.xy / input.positionCS.w;
    float2 previousNDC = input.prevPositionCS.xy / input.prevPositionCS.w;
    
    // Apply jitter correction to the NDC positions
    // Subtract current frame's jitter and add previous frame's jitter
    currentNDC -= float2(pc.projJitter.x, pc.projJitter.y);
    previousNDC += float2(pc.prevProjJitter.x, pc.prevProjJitter.y);
    
    // Calculate the velocity in NDC space
    float2 velocityNDC = currentNDC - previousNDC;
    
    // Store the velocity in the output
    output.velocity = velocityNDC;

    return output;
}
