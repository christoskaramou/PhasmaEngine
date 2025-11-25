#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"
#include "../Common/MaterialFlags.hlsl"

[[vk::push_constant]] PushConstants_GBuffer pc;
[[vk::binding(0, 1)]] StructuredBuffer<Primitive_Constants> constants;
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
    uint textureMask = constants[id].textureMask;

    float4 sampledBaseColor = HasTexture(textureMask, TEX_BASE_COLOR_BIT) ? GetBaseColor(id, uv) : float4(1.0f, 1.0f, 1.0f, 1.0f);
    float4 combinedColor    = sampledBaseColor * input.color * input.baseColorFactor;

    if (combinedColor.a < constants[id].alphaCut)
        discard;

    float3 mrSample = HasTexture(textureMask, TEX_METAL_ROUGH_BIT) ? GetMetallicRoughness(id, uv).xyz : float3(1.0f, 1.0f, 1.0f);
    float3 tangentNormal = HasTexture(textureMask, TEX_NORMAL_BIT) ? GetNormal(id, uv).xyz : float3(0.0f, 0.0f, 1.0f);
    float occlusionSample = HasTexture(textureMask, TEX_OCCLUSION_BIT) ? GetOcclusion(id, uv).r : 1.0f;
    float3 emissiveSample = HasTexture(textureMask, TEX_EMISSIVE_BIT) ? GetEmissive(id, uv).xyz : float3(1.0f, 1.0f, 1.0f);

    float metallic = saturate(input.metRoughAlphacutOcl.x * mrSample.z);
    float roughness = saturate(input.metRoughAlphacutOcl.y * mrSample.y);
    float occlusion = lerp(1.0f, occlusionSample, input.metRoughAlphacutOcl.w);

    output.normal = CalculateNormal(input.positionWS.xyz, tangentNormal, input.normal, input.uv) * 0.5f + 0.5f;
    output.albedo = float4(combinedColor.xyz * occlusion, combinedColor.a);
    output.metRough = float3(0.0f, roughness, metallic);
    output.emissive = float4(emissiveSample * input.emissiveFactor, 0.0f);
    output.transparency = pc.transparentPass ? 1.0f : 0.0f;

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
