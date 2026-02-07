#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"
#include "../Common/MaterialFlags.hlsl"

[[vk::push_constant]] PushConstants_GBuffer pc;
[[vk::binding(0, 1)]] StructuredBuffer<Mesh_Constants> constants;
[[vk::binding(1, 1)]] SamplerState material_sampler;
[[vk::binding(2, 1)]] Texture2D textures[];

float4 SampleArray(float2 uv, uint index)
{
    if (index == 0xFFFFFFFF)
        return float4(1.0, 1.0, 1.0, 1.0);
    return textures[NonUniformResourceIndex(index)].Sample(material_sampler, uv);
}

float4 GetBaseColor(uint id, float2 uv)          { return SampleArray(uv, constants[id].meshImageIndex[0]); }
float4 GetMetallicRoughness(uint id, float2 uv)  { return SampleArray(uv, constants[id].meshImageIndex[1]); }
float4 GetNormal(uint id, float2 uv)             { return SampleArray(uv, constants[id].meshImageIndex[2]); }
float4 GetOcclusion(uint id, float2 uv)          { return SampleArray(uv, constants[id].meshImageIndex[3]); }
float4 GetEmissive(uint id, float2 uv)           { return SampleArray(uv, constants[id].meshImageIndex[4]); }

PS_OUTPUT_Gbuffer mainPS(PS_INPUT_Gbuffer input)
{
    PS_OUTPUT_Gbuffer output;

    const uint id = input.id;
    
    float2 uv = input.uv;
    uint textureMask = constants[id].textureMask;

    float4 sampledBaseColor = HasTexture(textureMask, TEX_BASE_COLOR_BIT) ? GetBaseColor(id, uv) : float4(1.0f, 1.0f, 1.0f, 1.0f);
    float4 combinedColor    = sampledBaseColor * input.color * input.baseColorFactor;

    if (combinedColor.a < input.metRoughAlphacutOcl.z)
        discard;

    float3 N = normalize(input.normal);
    float3 normalWS = N;
    if (HasTexture(textureMask, TEX_NORMAL_BIT))
    {
        float3 tangentNormal = GetNormal(id, uv).xyz;
        float3 T = normalize(input.tangent.xyz);
        T = normalize(T - dot(T, N) * N);
        float3 B = cross(N, T) * input.tangent.w;
        float3x3 TBN = float3x3(T, B, N);
        normalWS = normalize(mul(tangentNormal * 2.0 - 1.0, TBN));
    }

    float metallic = input.metRoughAlphacutOcl.x;
    float roughness = input.metRoughAlphacutOcl.y;
    if (HasTexture(textureMask, TEX_METAL_ROUGH_BIT))
    {
        float3 mrSample = GetMetallicRoughness(id, uv).xyz;
        metallic *= mrSample.z;
        roughness *= mrSample.y;
    }
    metallic = saturate(metallic);
    roughness = saturate(roughness);

    float occlusion = 1.0f;
    if (HasTexture(textureMask, TEX_OCCLUSION_BIT))
    {
        float occlusionSample = GetOcclusion(id, uv).r;
        occlusion = lerp(1.0f, occlusionSample, input.metRoughAlphacutOcl.w);
    }

    float3 emissive = float3(0.0f, 0.0f, 0.0f);
    if (HasTexture(textureMask, TEX_EMISSIVE_BIT))
    {
        emissive = GetEmissive(id, uv).xyz * input.emissiveFactor;
    }

    output.normal = normalWS * 0.5f + 0.5f;
    output.albedo = float4(combinedColor.xyz, combinedColor.a);
    float transmission = (pc.passType == 2) ? 1.0f : 0.0f;
    output.metRough = float4(occlusion, roughness, metallic, transmission);
    output.emissive = float4(emissive, 0.0f);
    output.transparency = pc.passType ? 1.0f : 0.0f;

    // Calculate the velocity
    // Ensure that the positions are in NDC space by dividing by the w component
    float2 currentNDC = input.positionCS.xy / input.positionCS.w;
    float2 previousNDC = input.prevPositionCS.xy / input.prevPositionCS.w;
    
    // Apply jitter correction to the NDC positions
    currentNDC -= float2(pc.projJitter.x, pc.projJitter.y);
    previousNDC -= float2(pc.prevProjJitter.x, pc.prevProjJitter.y);
    
    // Calculate the velocity in NDC space
    float2 velocityNDC = previousNDC - currentNDC;
    
    // Store the velocity in the output
    output.velocity = velocityNDC;

    return output;
}
