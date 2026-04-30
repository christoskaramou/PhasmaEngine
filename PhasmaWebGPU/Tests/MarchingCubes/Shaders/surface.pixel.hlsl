struct RenderUniforms
{
    column_major float4x4 modelMatrix;
    column_major float4x4 viewProjectionMatrix;
    float4 eyeTime;
};

[[vk::binding(0, 0)]] cbuffer RenderUniformsCB : register(b0, space0)
{
    RenderUniforms uniforms;
};

[[vk::binding(2, 0)]] SamplerState triSampler : register(s0, space0);
[[vk::binding(3, 0)]] Texture2D<float4> texGround : register(t0, space0);
[[vk::binding(4, 0)]] Texture2D<float4> texMud : register(t1, space0);
[[vk::binding(5, 0)]] Texture2D<float4> texRock : register(t2, space0);
[[vk::binding(6, 0)]] Texture2D<float4> texGroundNormal : register(t3, space0);
[[vk::binding(7, 0)]] Texture2D<float4> texMudNormal : register(t4, space0);
[[vk::binding(8, 0)]] Texture2D<float4> texRockNormal : register(t5, space0);

struct PSIn
{
    float4 svPos : SV_Position;
    float3 positionWS : TEXCOORD0;
    float3 normalWS : TEXCOORD1;
    float ao : TEXCOORD2;
};

float3x3 ComputeBasis(float3 normal)
{
    float3 n = abs(normal);
    float3 tangent;
    if (n.x > n.y && n.x > n.z)
        tangent = float3(0.0f, 1.0f, 0.0f);
    else if (n.y > n.x && n.y > n.z)
        tangent = float3(0.0f, 0.0f, 1.0f);
    else
        tangent = float3(1.0f, 0.0f, 0.0f);

    float3 bitangent = normalize(cross(normal, tangent));
    tangent = normalize(cross(bitangent, normal));
    return float3x3(tangent, bitangent, normal);
}

float3x3 TriplanarSample(float3 texCoord,
                         float3 normal,
                         Texture2D<float4> tX,
                         Texture2D<float4> tY,
                         Texture2D<float4> tZ)
{
    float3 x = tX.Sample(triSampler, texCoord.yz).rgb;
    float3 y = lerp(tX.Sample(triSampler, texCoord.zx).rgb,
                    tY.Sample(triSampler, texCoord.zx).rgb,
                    smoothstep(-0.8f, 0.8f, normal.y));
    float3 z = lerp(tX.Sample(triSampler, texCoord.xy).rgb,
                    tZ.Sample(triSampler, texCoord.xy).rgb,
                    smoothstep(-0.25f, 0.75f, min(normal.z, normal.y)));
    return float3x3(x, y, z);
}

float4 PSMain(PSIn input) : SV_Target0
{
    float3 normal = normalize(input.normalWS);

    float3 eye = uniforms.eyeTime.xyz;
    float3 viewDir = normalize(input.positionWS - eye);
    float3x3 basis = ComputeBasis(normal);
    float3 texCoord = 3.1f * (input.positionWS + 8.0f) / 16.0f;

    float3 blendWeights = pow(saturate((abs(normal) - 0.2f) * 7.0f), 1.5f);
    blendWeights /= max(dot(blendWeights, float3(1.0f, 1.0f, 1.0f)), 0.0001f);

    float3x3 albedos = TriplanarSample(texCoord, normal, texRock, texGround, texMud);
    float3 albedo = mul(blendWeights, albedos);

    float3x3 normals = TriplanarSample(texCoord, normal, texRockNormal, texGroundNormal, texMudNormal);
    normals[0] = normalize(2.0f * normals[0] - 1.0f);
    normals[1] = normalize(2.0f * normals[1] - 1.0f);
    normals[2] = normalize(2.0f * normals[2] - 1.0f);
    float3 detailNormal = normalize(mul(blendWeights, normals));
    float3 mappedNormal = normalize(mul(detailNormal, basis));
    normal = normalize(lerp(normal, mappedNormal, 0.35f));

    float fakeLight = saturate(0.25f +
                               (0.2f + 0.6f * blendWeights.y + 0.2f * blendWeights.z) *
                                   input.ao *
                                   (0.75f + saturate(dot(normal, normalize(float3(1.0f, 1.0f, 1.0f))))));
    float rim = pow(1.0f - saturate(dot(normal, -viewDir)), 2.0f) * 0.08f;
    float3 color = input.ao * fakeLight * albedo + rim;
    return float4(color, 1.0f);
}
